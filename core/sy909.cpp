// SPDX-License-Identifier: GPL-2.0-or-later
#include "sy909.h"
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <cmath>

// Keys are read from port A bits 7..15 (active high). Determined by testing in the menu:
// 9 = up, 11 = down, 10 = left, 8 = right, 13 = A (confirm), 14 = start,
// 12, 7, 15 = remaining face/system keys (labels not yet confirmed on hardware).
//                                         UP DOWN LEFT RIGHT A  B  START SELECT EXTRA
const int Sy909::s_button_portbits[9] = {  9, 11,  10,   8,  13, 12, 14,   7,     15 };

namespace {
// External SPI image is encrypted from byte 0x3000 on (same scheme as MAME's siddr, key 0x4a)
inline uint8_t decrypt_byte(uint8_t c)
{
	static const int perm[8] = { 3, 5, 0, 7, 1, 4, 2, 6 };
	c ^= 0x4a;
	uint8_t r = 0;
	for (int i = 0; i < 8; i++) r |= ((c >> perm[i]) & 1) << (7 - i);
	return r;
}

// HLE entry points in internal ROM
constexpr uint32_t ROM_SPI_READ = 0x63ae;   // r1=dest, r2=count, r4:r3 = SPI word address
constexpr uint32_t ROM_637E = 0x637e;
constexpr uint32_t ROM_6390 = 0x6390;
constexpr uint32_t ROM_639D = 0x639d;
constexpr uint32_t ROM_560A = 0x560a;
constexpr uint32_t FAKE_VECTOR_BASE = 0xff00; // our trampolines for the 10 interrupt slots
}

Sy909::Sy909()
{
	// The audio decoder's delta table lives in the undumped internal ROM.
	// Reconstructed: delta = signed nibble * 2^(row/2 - 4), row = header byte >> 3.
	// (fitted against the game's own sample data; may differ slightly from the real table)
	for (int r = 0; r < 32; r++)
		for (int n = 0; n < 16; n++)
		{
			int sn = n < 8 ? n : n - 16;
			long v = lround(sn * pow(2.0, r / 2.0 - 4.0));
			m_dtab[r * 16 + n] = (int16_t)std::max(-32768L, std::min(32767L, v));
		}
	memset(m_ram, 0, sizeof m_ram);
	memset(m_io, 0, sizeof m_io);
	memset(m_fb, 0, sizeof m_fb);
}

void Sy909::logf(const char *fmt, ...)
{
	if (!log) return;
	char b[512];
	va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
	log(b);
}

bool Sy909::load(const std::vector<uint8_t> &spi1, const std::vector<uint8_t> &spi2, std::string &err)
{
	if (spi1.size() < 0x10000 || spi1[0] != 0x47 || spi1[1] != 0x50 || spi1[2] != 0x33 || spi1[3] != 0x58)
	{
		err = "SPI1 image is not a GP3X image (expected the 1MB by25q80awt.bin dump)";
		return false;
	}
	if (spi2.size() < 0x100000)
	{
		err = "SPI2 image too small (expected the 4MB p25d32.bin dump)";
		return false;
	}
	m_flash1.set_data(spi1.data(), spi1.size(), 0x68, 0x13, 0x14); // BoyaMicro BY25Q80
	m_flash2.set_data(spi2.data(), spi2.size(), 0x85, 0x15, 0x16); // Puya P25D32
	std::vector<uint8_t> dec(spi1);
	for (size_t i = 0x3000; i < dec.size(); i++) dec[i] = decrypt_byte(dec[i]);
	m_spi1_words.resize(dec.size() / 2);
	for (size_t i = 0; i < m_spi1_words.size(); i++) m_spi1_words[i] = dec[2 * i] | (dec[2 * i + 1] << 8);
	return true;
}

uint16_t Sy909::spi_word(uint32_t word_addr) const
{
	if (m_spi1_words.empty()) return 0xffff;
	return m_spi1_words[word_addr % m_spi1_words.size()];
}

void Sy909::power_on()
{
	memset(m_ram, 0, sizeof m_ram);
	memset(m_io, 0, sizeof m_io);
	m_int_status = m_int2_status = 0;
	m_porta_out = 0; m_portb_out = 0xffff;
	for (auto &t : m_tmr) t = Timer();
	m_tmr[0].bit = 0x8000; m_tmr[1].bit = 0x4000; m_tmr[2].bit = 0x2000;
	m_lcd.reset();
	m_spi1_rx.clear(); m_spi2_rx.clear();

	// HLE boot loader: copy the boot block (header + loader + its tables) to RAM
	// and start at the loader entry point given in the header.
	const uint32_t boot_words = 0x400;
	for (uint32_t i = 0; i < boot_words; i++) m_ram[i] = spi_word(i);
	reset();
	auto &s = state();
	s.m_r[REG_SP] = spi_word(0x09) ? spi_word(0x09) : 0x0bff;
	set_lpc(spi_word(0x0c) ? spi_word(0x0c) : 0x100);
	set_ds(0);
	logf("boot: entry=%06x sp=%04x\n", lpc(), s.m_r[REG_SP]);
}

// ---------------------------------------------------------------- memory

uint16_t Sy909::read16(uint32_t a)
{
	a &= 0x3fffff;
	if (a < 0x3000) return m_ram[a];
	if (a < 0x3100) return io_r(a);
	if (a >= 0x200000) return spi_word(a - 0x200000);
	if (a >= 0x48d3 && a < 0x48d3 + 0x200) { m_dtab_hist[a - 0x48d3]++; if (dtrace) { dtrace->push_back(((m_ram[0x12] & 0xff) << 8) | ((a - 0x48d3) & 0xf) | (dtrace_ch << 4)); } return m_dtab[a - 0x48d3]; }
	if (a >= 0xfff0 && a <= 0xffff)
	{
		// vector table: 0xfff5 break, fff6 fiq, fff7 reset, fff8..ffff irq0..7
		if (a == 0xfff5) return FAKE_VECTOR_BASE + 0;
		if (a == 0xfff6) return FAKE_VECTOR_BASE + 1;
		if (a == 0xfff7) return 0x0100;
		if (a >= 0xfff8) return FAKE_VECTOR_BASE + 2 + (a - 0xfff8);
	}
	return 0x0000;
}

void Sy909::write16(uint32_t a, uint16_t d)
{
	a &= 0x3fffff;
	if (a < 0x3000) { if ((int32_t)a >= watch_lo && (int32_t)a <= watch_hi && watch_count++ < 200) logf("W [%04x]=%04x pc=%06x\n", a, d, lpc()); m_ram[a] = d; return; }
	if (a < 0x3100) { io_w(a, d); return; }
}

// ---------------------------------------------------------------- HLE

void Sy909::hle_return(uint16_t r1)
{
	auto &s = state();
	s.m_r[REG_R1] = r1;
	// retf: pop SR, pop PC
	s.m_r[REG_SR] = pop(&s.m_r[REG_SP]);
	s.m_r[REG_PC] = pop(&s.m_r[REG_SP]);
	s.m_icount -= 8;
}

bool Sy909::pre_execute(uint32_t pc)
{
	if (pc_hook) pc_hook(pc, pc_hook_ctx);
	if (pc >= 0x200000) return m_accel && accelerate(pc);
	if (pc < 0x4000 || pc >= 0x10000) return false;
	auto &s = state();

	if (pc >= FAKE_VECTOR_BASE && pc < FAKE_VECTOR_BASE + 10)
	{
		// internal ROM interrupt trampoline: push r3,r4 then jump through the
		// GP3X header vector table (handlers start with "ds=20 / pop r3,r4")
		int idx = pc - FAKE_VECTOR_BASE;
		push(s.m_r[REG_R4], &s.m_r[REG_SP]);
		push(s.m_r[REG_R3], &s.m_r[REG_SP]);
		set_lpc(0x200000 | spi_word(0x10 + idx));
		s.m_icount -= 6;
		return true;
	}

	switch (pc)
	{
	case ROM_SPI_READ:
	{
		uint32_t dst = s.m_r[REG_R1] & 0xffff, cnt = s.m_r[REG_R2] & 0xffff;
		uint32_t src = ((s.m_r[REG_R4] & 0xffff) << 16) | (s.m_r[REG_R3] & 0xffff);
		if (trace_hle) logf("HLE spi_read dst=%04x cnt=%04x src=%06x\n", dst, cnt, src);
		for (uint32_t i = 0; i < cnt; i++) write16((dst + i) & 0xffff, read16((src + i) & 0x3fffff));
		s.m_icount -= cnt * 4;
		hle_return(s.m_r[REG_R1]);
		return true;
	}
	case ROM_637E:
	case ROM_6390:
		if (trace_hle) logf("HLE %04x (nop) from %06x\n", pc, ((read16((s.m_r[REG_SP] + 1) & 0xffff) & 0x3f) << 16) | read16((s.m_r[REG_SP] + 2) & 0xffff));
		hle_return(s.m_r[REG_R1]);
		return true;
	case ROM_639D:
	case ROM_560A:
		if (trace_hle) logf("HLE %04x r1=%04x r2=%04x r3=%04x r4=%04x\n", pc, s.m_r[1], s.m_r[2], s.m_r[3], s.m_r[4]);
		hle_return(s.m_r[REG_R1]);
		return true;
	default:
		m_unknown_rom_calls++;
		if (m_unknown_rom_calls < 50)
			logf("UNKNOWN internal ROM execution at %06x (sp=%04x r1=%04x r2=%04x r3=%04x r4=%04x)\n", pc, s.m_r[0], s.m_r[1], s.m_r[2], s.m_r[3], s.m_r[4]);
		hle_return(s.m_r[REG_R1]);
		return true;
	}
}

// Native fast paths for the hottest loops of this ROM (pixel push and memcpy/memset).
// Each one checks the exact opcodes first and falls back to interpretation otherwise.
bool Sy909::match(uint32_t pc, std::initializer_list<uint16_t> ops) const
{
	for (uint16_t o : ops) if (spi_word(pc++ - 0x200000) != o) return false;
	return true;
}

bool Sy909::accelerate(uint32_t pc)
{
	auto &s = state();
	switch (pc)
	{
	case 0x227457: // r3=[r2++]; clrb [3005],9; [3001]=r3; setb [3005],9; r1-=1; jne
	case 0x22744a: // same, through palette at 0x08f9
	{
		const bool pal = pc == 0x22744a;
		if (pal ? !match(pc, { 0x96d2, 0x070b, 0x08f9, 0x96c3, 0xf269, 0x3005, 0xd71b, 0x3001, 0xf259, 0x3005, 0x2241, 0x4e4c })
		        : !match(pc, { 0x96d2, 0xf269, 0x3005, 0xd71b, 0x3001, 0xf259, 0x3005, 0x2241, 0x4e49 }))
			return false;
		if ((m_portb_out & 0x1a00) != 0x0a00) return false; // need CS low, D/C data, WR high
		uint16_t n = s.m_r[REG_R1];
		if (n == 0) return false;
		uint16_t src = s.m_r[REG_R2], v = 0;
		for (uint32_t i = 0; i < n; i++)
		{
			v = m_ram[(uint16_t)(src + i) % 0x3000];
			if (pal) v = m_ram[(uint16_t)(v + 0x08f9) % 0x3000];
			m_lcd.write_data(v);
		}
		s.m_r[REG_R2] = (uint16_t)(src + n);
		s.m_r[REG_R3] = v;
		s.m_r[REG_R1] = 0;
		m_porta_out = v; m_io[0x01] = v;
		update_nzsc(0x10000, 1, 0xfffe); // result of 1-1: Z and C set
		set_lpc(pc + (pal ? 12 : 9));
		s.m_icount -= n * (pal ? 44 : 36);
		return true;
	}
	case 0x227274: // r4=[r2++]; [r3++]=r4; bp-=1; jne   (memcpy)
	{
		if (!match(pc, { 0x98d2, 0xd8d3, 0x2a41, 0x4e44 })) return false;
		uint16_t n = s.m_r[REG_BP];
		if (n == 0) return false;
		uint16_t a = s.m_r[REG_R2], b = s.m_r[REG_R3], v = 0;
		for (uint32_t i = 0; i < n; i++) { v = read16((uint16_t)(a + i)); write16((uint16_t)(b + i), v); }
		s.m_r[REG_R2] = (uint16_t)(a + n); s.m_r[REG_R3] = (uint16_t)(b + n); s.m_r[REG_R4] = v; s.m_r[REG_BP] = 0;
		update_nzsc(0x10000, 1, 0xfffe);
		set_lpc(pc + 4);
		s.m_icount -= n * 18;
		return true;
	}
	case 0x227150: // [r2++]=r4; bp-=1; jne   (memset)
	{
		if (!match(pc, { 0xd8d2, 0x2a41, 0x4e43 })) return false;
		uint16_t n = s.m_r[REG_BP];
		if (n == 0) return false;
		uint16_t a = s.m_r[REG_R2];
		for (uint32_t i = 0; i < n; i++) write16((uint16_t)(a + i), s.m_r[REG_R4]);
		s.m_r[REG_R2] = (uint16_t)(a + n); s.m_r[REG_BP] = 0;
		update_nzsc(0x10000, 1, 0xfffe);
		set_lpc(pc + 3);
		s.m_icount -= n * 12;
		return true;
	}
	case 0x204f55: // scheduler found no runnable task on its last pass: idle until next event
		if (m_idle_skip && read16(0x03fa) == 1 && !s.m_sirq && s.m_icount > 0)
		{
			m_idle_cycles += s.m_icount;
			s.m_icount = 0;
			m_idle_hit = true;
		}
		return false;
	}
	return false;
}

void Sy909::on_irq_taken(int line)
{
	if (trace_io) logf("IRQ line %d taken at %06x\n", line, lpc());
}

// ---------------------------------------------------------------- IO

uint16_t Sy909::io_r(uint32_t a)
{
	uint32_t r = a & 0xff;
	switch (r)
	{
	case 0x00: // IOA data: buttons (or LCD read data)
	{
		uint16_t in = m_raw_keys;
		for (int i = 0; i < 9; i++)
			if (m_buttons & (1 << i)) in |= 1 << s_button_portbits[i];
		uint16_t dir = m_io[0x02];
		return (in & ~dir) | (m_porta_out & dir) | (m_lcd_latch & ~dir & 0x00ff);
	}
	case 0x01: return m_porta_out;
	case 0x04: return (m_portb_out & m_io[0x06]) | (~m_io[0x06] & 0xffff);
	case 0x05: return m_portb_out;
	case 0x30: return m_io[0x30] | 0x0002; // system clock: PLL stable
	case 0x51: return m_int_status;
	case 0x54: return m_int2_status;
	// SPI1 (boot flash) controller
	case 0xe4: { uint8_t v = 0xff; if (!m_spi1_rx.empty()) { v = m_spi1_rx.front(); m_spi1_rx.erase(m_spi1_rx.begin()); } return v; }
	case 0xe5: return (m_spi1_rx.empty() ? 0x00 : 0x04) | 0x00; // bit4 busy=0, bit2 rx ready
	case 0xe3: return m_spi1_rx.size();
	// SPI2 (data flash) controller
	case 0x94: { uint8_t v = 0xff; if (!m_spi2_rx.empty()) { v = m_spi2_rx.front(); m_spi2_rx.erase(m_spi2_rx.begin()); } return v; }
	case 0x95: return 0x0004; // bit4 busy = 0, bit2 transfer complete
	case 0x93: return m_spi2_rx.size();
	case 0x91: return 0;
	default:
		if (trace_io) logf("io_r %04x (%04x) at %06x\n", a, m_io[r], lpc());
		return m_io[r];
	}
}

void Sy909::io_w(uint32_t a, uint16_t d)
{
	uint32_t r = a & 0xff;
	uint16_t old = m_io[r];
	m_io[r] = d;
	if (trace_io && r != 0x01 && r != 0x05 && r != 0x92 && r != 0xe2 && r != 0x34 && r != 0x41) logf("io_w %04x = %04x at %06x\n", a, d, lpc());
	switch (r)
	{
	case 0x01: m_porta_out = d; break;
	case 0x02: m_lcd_latch = 0; break;
	case 0x05: { uint16_t o = m_portb_out; m_portb_out = d; portb_changed(o, d); break; }
	case 0x09: // port C: bit5 = SPI1 chip select (active low)
		m_flash1.select(!(d & 0x20));
		if (!(d & 0x20) && (old & 0x20)) m_spi1_rx.clear();
		break;
	case 0x12: m_tmr[1].reload = d; break;
	case 0x13: m_tmr[1].count = d; break;
	case 0x10: m_tmr[0].reload = d; break;
	case 0x11: m_tmr[0].count = d; break;
	case 0x14: m_tmr[2].reload = d; break;
	case 0x15: m_tmr[2].count = d; break;
	case 0x16:
		m_tmr[0].enabled = d & 0x0008; // guess
		m_tmr[1].enabled = d & 0x0080;
		m_tmr[2].enabled = d & 0x0800;
		break;
	case 0x50: update_irqs(); break;
	case 0x51: m_int_status &= ~d; update_irqs(); break;
	case 0x52: update_irqs(); break;
	case 0x53: update_irqs(); break;
	case 0x54: m_int2_status &= ~d; update_irqs(); break;
	case 0x55: update_irqs(); break;
	case 0x41: // DAC channel 1
		if (m_audio.size() < 8192) m_audio.push_back((int16_t)d);
		break;
	case 0xe2: m_spi1_rx.push_back(m_flash1.transfer(d & 0xff)); break;
	case 0x92: m_spi2_rx.push_back(m_flash2.transfer(d & 0xff)); break;
	default:
		break;
	}
}

void Sy909::portb_changed(uint16_t old, uint16_t now)
{
	// bit5 : SPI2 flash CS (active low)
	if ((old ^ now) & 0x0020)
	{
		m_flash2.select(!(now & 0x0020));
		if (!(now & 0x0020)) m_spi2_rx.clear();
	}
	// LCD 8080 bus: bit12 CS (low), bit11 D/C (0 = command), bit9 WR (rising edge), bit8 RD (falling edge), bit10 RESET (low)
	const bool cs = !(now & 0x1000);
	if (!(now & 0x0400) && (old & 0x0400)) m_lcd.reset();
	if (cs && !(old & 0x0200) && (now & 0x0200))
	{
		if (now & 0x0800) m_lcd.write_data(m_porta_out);
		else m_lcd.write_cmd(m_porta_out);
	}
	if (cs && (old & 0x0100) && !(now & 0x0100))
	{
		// LCD read: answer the ID registers so the game picks an init sequence
		static const uint8_t id[4] = { 0x00, 0x00, 0x93, 0x06 };
		static int idx = 0;
		m_lcd_latch = id[idx++ & 3];
	}
}

void Sy909::update_irqs()
{
	const uint16_t en1 = m_io[0x50], fiqsel1 = m_io[0x52];
	const uint16_t en2 = m_io[0x53], fiqsel2 = m_io[0x55];
	const uint16_t act1 = m_int_status & en1, act2 = m_int2_status & en2;
	bool fiq = (act1 & fiqsel1) || (act2 & fiqsel2);
	const uint16_t irq1 = act1 & ~fiqsel1, irq2 = act2 & ~fiqsel2;
	set_input_line(UNSP_FIQ_LINE, fiq);
	set_input_line(UNSP_IRQ0_LINE, (irq1 & 0x8000) != 0);
	set_input_line(UNSP_IRQ1_LINE, (irq1 & 0x4000) != 0);
	set_input_line(UNSP_IRQ2_LINE, (irq1 & 0x2000) != 0);
	set_input_line(UNSP_IRQ3_LINE, (irq1 & 0x1fff) != 0);
	set_input_line(UNSP_IRQ6_LINE, (irq2 & 0xff00) != 0);
	set_input_line(UNSP_IRQ7_LINE, (irq2 & 0x00ff) != 0);
}

void Sy909::tick_timers(int cycles)
{
	bool changed = false;
	for (auto &t : m_tmr)
	{
		if (!t.enabled) continue;
		uint32_t c = t.count + cycles;
		while (c >= 0x10000)
		{
			uint32_t period = 0x10000 - t.reload;
			if (period == 0) period = 0x10000;
			c = c - 0x10000 + t.reload;
			m_int_status |= t.bit;
			changed = true;
			(void)period;
		}
		t.count = c;
	}
	static const uint16_t tb_bits[2] = { 0x0001, 0x0004 };
	for (int i = 0; i < 2; i++)
	{
		m_tb_acc[i] += cycles * m_tb_rate[i];
		while (m_tb_acc[i] >= CPU_CLOCK) { m_tb_acc[i] -= CPU_CLOCK; m_int2_status |= tb_bits[i]; changed = true; }
	}
	if (changed) update_irqs();
}

uint32_t Sy909::cycles_to_next_event() const
{
	uint64_t best = CPU_CLOCK;
	for (auto &t : m_tmr)
		if (t.enabled) best = std::min<uint64_t>(best, 0x10000 - t.count);
	for (int i = 0; i < 2; i++)
		if (m_tb_rate[i]) best = std::min<uint64_t>(best, (CPU_CLOCK - m_tb_acc[i] + m_tb_rate[i] - 1) / m_tb_rate[i]);
	return (uint32_t)best;
}

void Sy909::run_frame()
{
	m_audio.clear();
	const int frame_cycles = CPU_CLOCK / FPS; // real SoC cycles per frame
	const int slice = 256 * m_speed;
	int done = 0;
	while (done < frame_cycles)
	{
		int ran = run(slice) + m_cycle_frac;
		int real = ran / m_speed;
		m_cycle_frac = ran - real * m_speed;
		tick_timers(real);
		done += real;
		m_total_cycles += real;
		if (m_idle_hit)
		{
			// CPU is idle until the next interrupt source fires: jump straight there
			m_idle_hit = false;
			uint32_t skip = std::min<uint32_t>(cycles_to_next_event(), frame_cycles - std::min(done, frame_cycles));
			if (skip > 1)
			{
				skip -= 1;
				tick_timers(skip);
				done += skip;
				m_total_cycles += skip;
				m_idle_cycles += skip;
			}
		}
	}
	m_lcd.render(m_fb);
}

// ---------------------------------------------------------------- save states

namespace {
struct Writer { std::vector<uint8_t> &v; template <typename T> void put(const T &t) { const uint8_t *p = (const uint8_t *)&t; v.insert(v.end(), p, p + sizeof(T)); } void bytes(const void *p, size_t n) { v.insert(v.end(), (const uint8_t *)p, (const uint8_t *)p + n); } };
struct Reader { const std::vector<uint8_t> &v; size_t pos = 0; bool ok = true; template <typename T> void get(T &t) { bytes(&t, sizeof(T)); } void bytes(void *p, size_t n) { if (pos + n > v.size()) { ok = false; return; } memcpy(p, v.data() + pos, n); pos += n; } };
constexpr uint32_t STATE_MAGIC = 0x39305953; // "SY09"
constexpr uint32_t STATE_VERSION = 1;
}

std::vector<uint8_t> Sy909::save_state()
{
	std::vector<uint8_t> out;
	Writer w{out};
	w.put(STATE_MAGIC); w.put(STATE_VERSION);
	w.put(state());
	w.bytes(m_ram, sizeof m_ram);
	w.bytes(m_io, sizeof m_io);
	w.put(m_int_status); w.put(m_int2_status); w.put(m_porta_out); w.put(m_portb_out); w.put(m_lcd_latch);
	w.put(m_tmr); w.put(m_tb_acc); w.put(m_cycle_frac); w.put(m_total_cycles);
	w.put(m_lcd);
	w.put(m_flash1.get_state()); w.put(m_flash2.get_state());
	uint32_t n1 = m_spi1_rx.size(), n2 = m_spi2_rx.size();
	w.put(n1); w.bytes(m_spi1_rx.data(), n1); w.put(n2); w.bytes(m_spi2_rx.data(), n2);
	return out;
}

bool Sy909::load_state(const std::vector<uint8_t> &data)
{
	Reader r{data};
	uint32_t magic = 0, ver = 0;
	r.get(magic); r.get(ver);
	if (!r.ok || magic != STATE_MAGIC || ver != STATE_VERSION) return false;
	internal_unsp_state cpu; r.get(cpu);
	uint16_t ram[0x3000]; r.bytes(ram, sizeof ram);
	uint16_t io[0x100]; r.bytes(io, sizeof io);
	uint16_t is1, is2, pa, pb, latch; r.get(is1); r.get(is2); r.get(pa); r.get(pb); r.get(latch);
	Timer tmr[3]; r.get(tmr); uint32_t tb[2]; r.get(tb); uint32_t frac; r.get(frac); uint64_t tot; r.get(tot);
	static DbiLcd lcd; r.get(lcd);
	SpiFlash::State f1, f2; r.get(f1); r.get(f2);
	uint32_t n1 = 0, n2 = 0; r.get(n1); std::vector<uint8_t> rx1(n1 < 4096 ? n1 : 0); r.bytes(rx1.data(), rx1.size());
	r.get(n2); std::vector<uint8_t> rx2(n2 < 4096 ? n2 : 0); r.bytes(rx2.data(), rx2.size());
	if (!r.ok) return false;
	state() = cpu;
	memcpy(m_ram, ram, sizeof ram); memcpy(m_io, io, sizeof io);
	m_int_status = is1; m_int2_status = is2; m_porta_out = pa; m_portb_out = pb; m_lcd_latch = latch;
	memcpy(m_tmr, tmr, sizeof tmr); memcpy(m_tb_acc, tb, sizeof tb); m_cycle_frac = frac; m_total_cycles = tot;
	m_lcd = lcd;
	m_flash1.set_state(f1); m_flash2.set_state(f2);
	m_spi1_rx = rx1; m_spi2_rx = rx2;
	update_irqs();
	return true;
}

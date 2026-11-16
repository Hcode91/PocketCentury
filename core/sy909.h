// SPDX-License-Identifier: GPL-2.0-or-later
// PocketCentury - emulation of the "100-in-1 Retro Gaming Console (SY-909)" handheld.
// GeneralPlus unSP 2.0 SoC (GPCE4-like register set), two SPI flashes, 320x240 DBI TFT.
// The SoC's internal mask ROM is not dumped, so its boot loader, interrupt trampolines
// and the few helper routines the game calls are emulated at a high level (HLE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <initializer_list>
#include "unsp/unsp.h"
#include "spiflash.h"
#include "lcd.h"

class Sy909 : public unsp_20_device
{
public:
	static constexpr uint32_t CPU_CLOCK = 96000000;
	static constexpr int FPS = 60;
	static constexpr int SCREEN_W = 320, SCREEN_H = 240;

	enum Button : uint16_t
	{
		BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
		BTN_A = 1 << 4, BTN_B = 1 << 5, BTN_START = 1 << 6, BTN_SELECT = 1 << 7, BTN_EXTRA = 1 << 8
	};
	// probe/debug helper: raw port A key bits (bypasses the Button mapping)
	void set_raw_keys(uint16_t portbits) { m_raw_keys = portbits; }

	Sy909();
	// spi1: 1MB by25q80 dump, spi2: 4MB p25d32 dump (raw, as dumped)
	bool load(const std::vector<uint8_t> &spi1, const std::vector<uint8_t> &spi2, std::string &err);
	void power_on();
	void run_frame();

	std::vector<uint8_t> save_state();
	bool load_state(const std::vector<uint8_t> &data);

	void set_buttons(uint16_t mask) { m_buttons = mask; }
	// MAME's u'nSP cycle table is for the slow 1.x cores; the GP3x core retires
	// instructions much faster, so we run `speed` emulated cycles per real cycle.
	void set_speed(int speed) { m_speed = speed < 1 ? 1 : speed; }
	int speed() const { return m_speed; }
	void set_timebase(int i, uint32_t hz) { m_tb_rate[i & 1] = hz; }
	void set_accel(bool on) { m_accel = on; }
	void set_idle_skip(bool on) { m_idle_skip = on; }
	uint64_t idle_cycles() const { return m_idle_cycles; }
	const uint16_t *framebuffer() const { return m_fb; }
	// audio: signed 16-bit mono samples produced during the last frame
	const std::vector<int16_t> &audio() const { return m_audio; }
	int audio_rate() const { return m_audio_rate; }

	// debugging
	std::function<void(const char *)> log;
	bool trace_io = false;
	bool trace_hle = true;
	void (*pc_hook)(uint32_t pc, void *ctx) = nullptr;
	void *pc_hook_ctx = nullptr;
	int32_t watch_lo = -1, watch_hi = -1; uint32_t watch_count = 0;
	uint64_t total_cycles() const { return m_total_cycles; }
	uint32_t unknown_rom_calls() const { return m_unknown_rom_calls; }
	const DbiLcd &lcd() const { return m_lcd; }

	// Button bit -> port A bit
	static const int s_button_portbits[9];

	uint16_t read16(uint32_t address) override;
	void write16(uint32_t address, uint16_t data) override;
	bool pre_execute(uint32_t lpc) override;
	void on_irq_taken(int line) override;

private:
	void logf(const char *fmt, ...);
	uint16_t io_r(uint32_t reg);
	void io_w(uint32_t reg, uint16_t data);
	void portb_changed(uint16_t old, uint16_t now);
	void update_irqs();
	void tick_timers(int cycles);
	void hle_return(uint16_t r1);
	bool accelerate(uint32_t pc);
	uint32_t cycles_to_next_event() const;
	bool m_idle_hit = false;
	bool match(uint32_t pc, std::initializer_list<uint16_t> ops) const;
	uint16_t spi_word(uint32_t word_addr) const;

	// memory
	uint16_t m_ram[0x3000];
	std::vector<uint16_t> m_spi1_words;  // decrypted, word addressed
	SpiFlash m_flash1, m_flash2;

	// io
	uint16_t m_io[0x100];
	uint16_t m_buttons = 0;
	uint16_t m_raw_keys = 0;
	uint16_t m_int_status = 0, m_int2_status = 0;
	uint16_t m_porta_out = 0, m_portb_out = 0;
	std::vector<uint8_t> m_spi1_rx, m_spi2_rx;
	uint16_t m_lcd_latch = 0;

	// timers
	struct Timer { uint16_t reload = 0; uint32_t count = 0; bool enabled = false; uint16_t bit = 0; };
	Timer m_tmr[3];
	// time base interrupt sources (INT2 status bits 0x01 / 0x04); rates are best guesses
	uint32_t m_tb_acc[2] = {0, 0};
	uint32_t m_tb_rate[2] = {16, 256};

	DbiLcd m_lcd;
	uint16_t m_fb[SCREEN_W * SCREEN_H];

	std::vector<int16_t> m_audio;
	int m_audio_rate = 9375;
	uint64_t m_total_cycles = 0;
	int m_speed = 6;
	bool m_accel = true, m_idle_skip = true;
	uint64_t m_idle_cycles = 0;
	uint32_t m_cycle_frac = 0;
	uint32_t m_unknown_rom_calls = 0;
public:
	// audio decoder delta table that lives in the (undumped) internal ROM at 0x48d3
	int16_t m_dtab[0x200] = {};
	uint32_t m_dtab_hist[0x200] = {};
	std::vector<uint16_t> *dtrace = nullptr;
	int dtrace_ch = 0;
};

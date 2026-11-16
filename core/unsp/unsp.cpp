// license:GPL-2.0+
// copyright-holders:Segher Boessenkool, Ryan Holtz, David Haywood
// Standalone port of MAME's u'nSP core glue (reset / interrupts / run loop).
#include "unsp.h"
#include <climits>

char const* const unsp_device::regs[] = { "sp", "r1", "r2", "r3", "r4", "bp", "sr", "pc" };
char const* const unsp_device::bitops[] = { "tstb", "setb", "clrb", "invb" };
char const* const unsp_device::lsft[] = { "asr", "asror", "lsl", "lslor", "lsr", "lsror", "rol", "ror" };
char const* const unsp_device::extregs[] = { "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
char const* const unsp_device::aluops[] = { "add","adc","sub","sbc","cmp","(invalid)","neg","--","xor","load","or","and","test","store","(invalid)","(invalid)" };
char const* const unsp_device::forms[] = { "[%s]", "[%s--]", "[%s++]", "[++%s]" };

unsp_device::unsp_device()
	: m_core(&m_local_core), m_iso(10), m_vectorbase(0xfff0), m_bootvectorbase(0xfff0), m_numregs(8)
{
	memset(&m_local_core, 0, sizeof(m_local_core));
}

void unsp_device::unimplemented_opcode(uint16_t op)
{
	fatalerror("UNSP: unknown opcode %04x at %06x\n", op, UNSP_LPC);
}
void unsp_device::unimplemented_opcode(uint16_t op, uint16_t ximm)
{
	fatalerror("UNSP: unknown opcode %04x %04x at %06x\n", op, ximm, UNSP_LPC);
}
void unsp_device::unimplemented_opcode(uint16_t op, uint16_t ximm, uint16_t ximm_2)
{
	fatalerror("UNSP: unknown opcode %04x %04x %04x at %06x\n", op, ximm, ximm_2, UNSP_LPC);
}

void unsp_device::reset()
{
	for (int i = 0; i < 16; i++)
		m_core->m_r[i] = (i < m_numregs) ? 0 : 0xdeadbeef;
	if (m_numregs > 8)
		for (int i = 8; i < 16; i++) m_core->m_r[i] = 0;
	m_core->m_r[REG_PC] = read16(m_bootvectorbase + 0x7);
	m_core->m_enable_irq = 0;
	m_core->m_enable_fiq = 0;
	m_core->m_fir_move = 1;
	m_core->m_sb = 0;
	m_core->m_aq = 0;
	m_core->m_fra = 0;
	m_core->m_bnk = 0;
	m_core->m_ine = 0;
	m_core->m_pri = 8;
	m_core->m_fiq = 0;
	m_core->m_irq = 0;
	m_core->m_sirq = 0;
	m_core->m_divq_bit = UINT_MAX;
}

void unsp_device::update_nzsc(uint32_t value, uint16_t r0, uint16_t r1)
{
	m_core->m_r[REG_SR] &= ~(UNSP_N | UNSP_Z | UNSP_S | UNSP_C);
	if (BIT(value, 16) != BIT((r0 ^ r1), 15))
		m_core->m_r[REG_SR] |= UNSP_S;
	if (BIT(value, 15))
		m_core->m_r[REG_SR] |= UNSP_N;
	if ((uint16_t)value == 0)
		m_core->m_r[REG_SR] |= UNSP_Z;
	if (BIT(value, 16))
		m_core->m_r[REG_SR] |= UNSP_C;
}

void unsp_device::update_nz(uint32_t value)
{
	m_core->m_r[REG_SR] &= ~(UNSP_N | UNSP_Z);
	if (value & 0x8000)
		m_core->m_r[REG_SR] |= UNSP_N;
	if ((uint16_t)value == 0)
		m_core->m_r[REG_SR] |= UNSP_Z;
}

void unsp_device::push(uint32_t value, uint32_t *reg)
{
	write16(*reg, (uint16_t)value);
	*reg = (uint16_t)(*reg - 1);
}

uint16_t unsp_device::pop(uint32_t *reg)
{
	*reg = (uint16_t)(*reg + 1);
	return (uint16_t)read16(*reg);
}

void unsp_device::trigger_fiq()
{
	if (!m_core->m_enable_fiq || m_core->m_fiq)
		return;
	on_irq_taken(UNSP_FIQ_LINE);
	m_core->m_fiq = 1;
	push(m_core->m_r[REG_PC], &m_core->m_r[REG_SP]);
	push(m_core->m_r[REG_SR], &m_core->m_r[REG_SP]);
	m_core->m_r[REG_PC] = read16(m_vectorbase + 0x06);
	m_core->m_r[REG_SR] = 0;
}

void unsp_device::trigger_irq(int line)
{
	if ((m_core->m_ine == 0 && m_core->m_irq == 1) || m_core->m_pri <= (uint32_t)line || !m_core->m_enable_irq)
		return;
	on_irq_taken(UNSP_IRQ0_LINE + line);
	m_core->m_irq = 1;
	push(m_core->m_r[REG_PC], &m_core->m_r[REG_SP]);
	push(m_core->m_r[REG_SR], &m_core->m_r[REG_SP]);
	if (m_core->m_ine)
		push(get_fr(), &m_core->m_r[REG_SP]);
	if (m_core->m_ine)
		m_core->m_pri = line;
	m_core->m_r[REG_PC] = read16(m_vectorbase + 0x08 + line);
	m_core->m_r[REG_SR] = 0;
}

void unsp_device::check_irqs()
{
	if (!m_core->m_sirq)
		return;
	int highest_irq = -1;
	for (int i = 0; i <= 8; i++)
		if (BIT(m_core->m_sirq, i)) { highest_irq = i; break; }
	if (highest_irq == UNSP_FIQ_LINE)
		trigger_fiq();
	else
		trigger_irq(highest_irq - 1);
}

void unsp_device::execute_one(const uint16_t op)
{
	const uint16_t op0 = (op >> 12) & 15;
	const uint16_t opa = (op >> 9) & 7;
	const uint16_t op1 = (op >> 6) & 7;
	if (op0 == 0xf)
		return execute_fxxx_group(op);
	if (op0 < 0xf && opa == 0x7 && op1 < 2)
		return execute_jumps(op);
	if (op0 == 0xe)
		return execute_exxx_group(op);
	execute_remaining(op);
}

int unsp_device::run(int cycles)
{
	m_core->m_icount = cycles;
	while (m_core->m_icount >= 0)
	{
		const uint32_t pc = UNSP_LPC;
		if (pre_execute(pc))
		{
			check_irqs();
			continue;
		}
		const uint32_t op = read16(pc);
		add_lpc(1);
		execute_one(op);
		if (op != 0x9a98)
			check_irqs();
	}
	return cycles - m_core->m_icount;
}

uint8_t unsp_device::get_csb()
{
	return 1 << ((UNSP_LPC >> 20) & 3);
}

void unsp_device::set_state_unsynced(int inputnum, int state)
{
	m_core->m_sirq &= ~(1 << inputnum);
	if (!state)
		return;
	if (inputnum >= UNSP_FIQ_LINE && inputnum <= UNSP_IRQ7_LINE)
		m_core->m_sirq |= (1 << inputnum);
}

uint16_t unsp_device::get_ds()
{
	return (m_core->m_r[REG_SR] >> 10) & 0x3f;
}

void unsp_device::set_ds(uint16_t ds)
{
	m_core->m_r[REG_SR] &= 0x03ff;
	m_core->m_r[REG_SR] |= (ds & 0x3f) << 10;
}

void unsp_device::set_fr(uint16_t fr)
{
	m_core->m_aq = BIT(fr, 14);
	const uint32_t old_bank = m_core->m_bnk;
	m_core->m_bnk = BIT(fr, 13);
	if (m_core->m_bnk != old_bank)
	{
		std::swap<uint32_t>(m_core->m_r[REG_R1], m_core->m_secbank[REG_SR1]);
		std::swap<uint32_t>(m_core->m_r[REG_R2], m_core->m_secbank[REG_SR2]);
		std::swap<uint32_t>(m_core->m_r[REG_R3], m_core->m_secbank[REG_SR3]);
		std::swap<uint32_t>(m_core->m_r[REG_R4], m_core->m_secbank[REG_SR4]);
	}
	m_core->m_fra = BIT(fr, 12);
	m_core->m_fir_move = BIT(fr, 11);
	m_core->m_sb = (fr >> 7) & 0xf;
	m_core->m_enable_fiq = BIT(fr, 6);
	m_core->m_enable_irq = BIT(fr, 5);
	m_core->m_ine = BIT(fr, 4);
	m_core->m_pri = fr & 0xf;
}

uint16_t unsp_device::get_fr()
{
	uint16_t fr = m_core->m_aq << 14;
	fr |= m_core->m_bnk << 13;
	fr |= m_core->m_fra << 12;
	fr |= m_core->m_fir_move << 11;
	fr |= m_core->m_sb << 7;
	fr |= m_core->m_enable_fiq << 6;
	fr |= m_core->m_enable_irq << 5;
	fr |= m_core->m_ine << 4;
	fr |= m_core->m_pri;
	return fr;
}

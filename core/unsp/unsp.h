// license:GPL-2.0+
// copyright-holders:Segher Boessenkool, Ryan Holtz, David Haywood
// Standalone (non-MAME) wrapper around MAME's SunPlus/GeneralPlus u'nSP interpreter.
// The instruction implementations (unsp_*.cpp) are taken unchanged from MAME; this header
// replaces MAME's device framework with plain virtual read/write callbacks.
#pragma once

#include "emu.h"
#include "unspdefs.h"
#include <cstdarg>

enum
{
	UNSP_FIQ_LINE = 0,
	UNSP_IRQ0_LINE,
	UNSP_IRQ1_LINE,
	UNSP_IRQ2_LINE,
	UNSP_IRQ3_LINE,
	UNSP_IRQ4_LINE,
	UNSP_IRQ5_LINE,
	UNSP_IRQ6_LINE,
	UNSP_IRQ7_LINE,
	UNSP_BRK_LINE,
	UNSP_NUM_LINES
};

class unsp_device
{
public:
	unsp_device();
	virtual ~unsp_device() = default;

	void set_vectorbase(uint16_t vector) { m_vectorbase = vector; }
	void set_bootvectorbase(uint16_t vector) { m_bootvectorbase = vector; }

	void reset();
	// run for at least `cycles` cycles; returns cycles actually consumed
	int run(int cycles);
	void set_input_line(int line, int state) { set_state_unsynced(line, state); }
	void request_stop() { m_core->m_icount = -1; }

	uint8_t get_csb();
	void set_ds(uint16_t ds);
	uint16_t get_ds();
	void set_fr(uint16_t fr);
	uint16_t get_fr();

	enum : uint32_t
	{
		REG_SP = 0, REG_R1, REG_R2, REG_R3, REG_R4, REG_BP, REG_SR, REG_PC,
		REG_SR1 = 0, REG_SR2, REG_SR3, REG_SR4
	};

	struct internal_unsp_state
	{
		uint32_t m_r[16];
		uint32_t m_secbank[4];
		uint32_t m_enable_irq;
		uint32_t m_enable_fiq;
		uint32_t m_fir_move;
		uint32_t m_fiq;
		uint32_t m_irq;
		uint32_t m_sirq;
		uint32_t m_sb;
		uint32_t m_aq;
		uint32_t m_fra;
		uint32_t m_bnk;
		uint32_t m_ine;
		uint32_t m_pri;
		uint32_t m_divq_bit;
		uint32_t m_divq_dividend;
		uint32_t m_divq_divisor;
		uint32_t m_divq_a;
		uint32_t m_arg0;
		uint32_t m_arg1;
		uint32_t m_jmpdest;
		int m_icount;
	};

	internal_unsp_state &state() { return *m_core; }
	uint32_t lpc() const { return ((m_core->m_r[REG_SR] & 0x3f) << 16) | m_core->m_r[REG_PC]; }
	void set_lpc(uint32_t a) { m_core->m_r[REG_PC] = a & 0xffff; m_core->m_r[REG_SR] = (m_core->m_r[REG_SR] & 0xffc0) | ((a >> 16) & 0x3f); }

	// host hooks
	virtual uint16_t read16(uint32_t address) = 0;
	virtual void write16(uint32_t address, uint16_t data) = 0;
	// called before each instruction is fetched; return true if the host handled
	// this PC (high-level emulation of internal ROM) and execution should skip it
	virtual bool pre_execute(uint32_t lpc) { return false; }
	virtual void on_irq_taken(int line) { }

	// public helpers for HLE
	void push(uint32_t value, uint32_t *reg);
	uint16_t pop(uint32_t *reg);

protected:
	struct machine_shim { const char *describe_context() const { return ""; } };
	machine_shim machine() const { return machine_shim(); }
	void logerror(const char *fmt, ...) { va_list ap; va_start(ap, fmt); char b[512]; vsnprintf(b, sizeof b, fmt, ap); va_end(ap); unsp_log("%s", b); }

	void add_lpc(const int32_t offset)
	{
		const uint32_t new_lpc = UNSP_LPC + offset;
		m_core->m_r[REG_PC] = (uint16_t)new_lpc;
		m_core->m_r[REG_SR] &= 0xffc0;
		m_core->m_r[REG_SR] |= (new_lpc >> 16) & 0x3f;
	}

	void execute_fxxx_000_group(uint16_t op);
	void execute_fxxx_001_group(uint16_t op);
	void execute_fxxx_010_group(uint16_t op);
	void execute_fxxx_011_group(uint16_t op);
	virtual void execute_fxxx_101_group(uint16_t op);
	void execute_fxxx_110_group(uint16_t op);
	void execute_fxxx_111_group(uint16_t op);
	void execute_fxxx_group(uint16_t op);
	void execute_fxxx_100_group(uint16_t op);
	virtual void execute_extended_group(uint16_t op);
	virtual void execute_exxx_group(uint16_t op);
	void execute_muls_ss(const uint16_t rd, const uint16_t rs, const uint16_t size);
	void unimplemented_opcode(uint16_t op);
	void unimplemented_opcode(uint16_t op, uint16_t ximm);
	void unimplemented_opcode(uint16_t op, uint16_t ximm, uint16_t ximm_2);
	virtual bool op_is_divq(const uint16_t op) { return false; }

	internal_unsp_state *m_core;
	int m_iso;
	static char const *const regs[];
	static char const *const extregs[];
	static char const *const bitops[];
	static char const *const lsft[];
	static char const *const aluops[];
	static char const *const forms[];

	void update_nz(uint32_t value);
	void update_nzsc(uint32_t value, uint16_t r0, uint16_t r1);
	bool do_basic_alu_ops(const uint16_t &op0, uint32_t &lres, uint16_t &r0, uint16_t &r1, uint32_t &r2, bool update_flags);

	void execute_jumps(const uint16_t op);
	void execute_remaining(const uint16_t op);
	void execute_one(const uint16_t op);

	void trigger_fiq();
	void trigger_irq(int line);
	void check_irqs();
	void set_state_unsynced(int inputnum, int state);

	uint16_t m_vectorbase;
	uint16_t m_bootvectorbase;
	internal_unsp_state m_local_core;
	int m_numregs;
};

class unsp_11_device : public unsp_device
{
public:
	unsp_11_device() { m_iso = 11; }
};

class unsp_12_device : public unsp_11_device
{
public:
	unsp_12_device() { m_iso = 12; }
protected:
	virtual void execute_fxxx_101_group(uint16_t op) override;
	virtual void execute_exxx_group(uint16_t op) override;
	void execute_divq(uint16_t op);
	bool op_is_divq(const uint16_t op) override;
};

class unsp_20_device : public unsp_12_device
{
public:
	unsp_20_device() { m_iso = 20; m_numregs = 16; }
protected:
	virtual void execute_extended_group(uint16_t op) override;
private:
	enum { UNSP20_R8 = 0, UNSP20_R9, UNSP20_R10, UNSP20_R11, UNSP20_R12, UNSP20_R13, UNSP20_R14, UNSP20_R15 };
};

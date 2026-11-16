// SPDX-License-Identifier: GPL-2.0-or-later
// Byte-level SPI NOR flash model (enough for read / fast read / ID / status / power commands).
#pragma once
#include <cstdint>
#include <vector>

class SpiFlash
{
public:
	void set_data(const uint8_t *d, size_t n, uint8_t mfr, uint8_t dev, uint8_t cap)
	{
		m_data.assign(d, d + n); m_mfr = mfr; m_dev = dev; m_cap = cap;
	}
	const std::vector<uint8_t> &data() const { return m_data; }

	void select(bool cs) // cs = true means asserted (active)
	{
		if (cs && !m_selected) { m_cmd = -1; m_pos = 0; m_addr = 0; }
		m_selected = cs;
	}
	bool selected() const { return m_selected; }

	uint8_t transfer(uint8_t in)
	{
		if (!m_selected) return 0xff;
		if (m_cmd < 0) { m_cmd = in; m_pos = 0; m_addr = 0; return 0xff; }
		m_pos++;
		switch (m_cmd)
		{
		case 0x03: // read
			if (m_pos <= 3) { m_addr = (m_addr << 8) | in; return 0xff; }
			return rd();
		case 0x0b: // fast read (1 dummy byte)
			if (m_pos <= 3) { m_addr = (m_addr << 8) | in; return 0xff; }
			if (m_pos == 4) return 0xff;
			return rd();
		case 0x90: // REMS: 3 address bytes then mfr, dev repeating
			if (m_pos <= 3) { m_addr = in; return 0xff; }
			return ((m_pos - 4 + (m_addr & 1)) & 1) ? m_dev : m_mfr;
		case 0x9f: // JEDEC ID
			if (m_pos == 1) return m_mfr;
			if (m_pos == 2) return 0x40;
			return m_cap;
		case 0xab: // release power down / device id
			if (m_pos <= 3) return 0xff;
			return m_dev;
		case 0x05: case 0x35: // status
			return 0x00;
		default:
			return 0xff;
		}
	}

	struct State { uint8_t selected; int32_t cmd; uint32_t pos, addr; };
	State get_state() const { return { (uint8_t)m_selected, m_cmd, m_pos, m_addr }; }
	void set_state(const State &s) { m_selected = s.selected; m_cmd = s.cmd; m_pos = s.pos; m_addr = s.addr; }

private:
	uint8_t rd()
	{
		uint8_t v = m_data.empty() ? 0xff : m_data[m_addr % m_data.size()];
		m_addr++;
		return v;
	}
	std::vector<uint8_t> m_data;
	uint8_t m_mfr = 0xef, m_dev = 0x13, m_cap = 0x14;
	bool m_selected = false;
	int m_cmd = -1;
	uint32_t m_pos = 0;
	uint32_t m_addr = 0;
};

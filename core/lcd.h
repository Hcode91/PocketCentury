// SPDX-License-Identifier: GPL-2.0-or-later
// Generic MIPI-DBI (8080 parallel, 16-bit) TFT controller, ILI9341/ST7789 style command set.
#pragma once
#include <cstdint>
#include <cstring>

class DbiLcd
{
public:
	static constexpr int W = 320, H = 240;
	// controller GRAM is kept in panel orientation (240 x 320 portrait); MADCTL MV swaps axes
	static constexpr int GW = 240, GH = 320;

	DbiLcd() { reset(); }
	void reset()
	{
		memset(m_gram, 0, sizeof m_gram);
		m_cmd = 0; m_nparam = 0; m_xs = 0; m_xe = GW - 1; m_ys = 0; m_ye = GH - 1; m_x = 0; m_y = 0;
		m_madctl = 0; m_writing = false; m_cmd_count = 0; m_pix_count = 0;
	}

	void write_cmd(uint16_t c)
	{
		m_cmd = c & 0xff; m_nparam = 0; m_cmd_count++;
		m_writing = (m_cmd == 0x2c || m_cmd == 0x3c);
		if (m_cmd == 0x2c) { m_x = m_xs; m_y = m_ys; }
	}

	void write_data(uint16_t d)
	{
		if (m_writing) { pixel(d); return; }
		uint8_t b = d & 0xff;
		switch (m_cmd)
		{
		case 0x2a:
			if (m_nparam == 0) m_xs = (m_xs & 0xff) | (b << 8);
			else if (m_nparam == 1) m_xs = (m_xs & 0xff00) | b;
			else if (m_nparam == 2) m_xe = (m_xe & 0xff) | (b << 8);
			else if (m_nparam == 3) m_xe = (m_xe & 0xff00) | b;
			break;
		case 0x2b:
			if (m_nparam == 0) m_ys = (m_ys & 0xff) | (b << 8);
			else if (m_nparam == 1) m_ys = (m_ys & 0xff00) | b;
			else if (m_nparam == 2) m_ye = (m_ye & 0xff) | (b << 8);
			else if (m_nparam == 3) m_ye = (m_ye & 0xff00) | b;
			break;
		case 0x36:
			if (m_nparam == 0) m_madctl = b;
			break;
		}
		m_nparam++;
	}

	// Render the visible 320x240 landscape image (RGB565)
	void render(uint16_t *out) const
	{
		// Address space after MADCTL is "logical"; we store logical coordinates directly
		// in a 320x320 buffer, so just copy the active landscape region.
		for (int y = 0; y < H; y++)
			memcpy(out + y * W, m_gram + y * 320, W * 2);
	}

	uint8_t madctl() const { return m_madctl; }
	uint32_t cmd_count() const { return m_cmd_count; }
	uint32_t pix_count() const { return m_pix_count; }
	uint16_t window(int i) const { return i == 0 ? m_xs : i == 1 ? m_xe : i == 2 ? m_ys : m_ye; }

private:
	void pixel(uint16_t p)
	{
		m_pix_count++;
		if (m_x < 320 && m_y < 320) m_gram[m_y * 320 + m_x] = p;
		if (++m_x > m_xe) { m_x = m_xs; if (++m_y > m_ye) m_y = m_ys; }
	}

	uint16_t m_gram[320 * 320];
	uint8_t m_cmd;
	int m_nparam;
	uint16_t m_xs, m_xe, m_ys, m_ye, m_x, m_y;
	uint8_t m_madctl;
	bool m_writing;
	uint32_t m_cmd_count, m_pix_count;
};

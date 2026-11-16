// Headless test runner: loads the two dumps, runs frames, writes PPM screenshots and logs.
#include "../core/sy909.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <cstdarg>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

void unsp_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
void unsp_fatal(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }

static std::vector<uint8_t> slurp(const char *p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {}); }

static void save_ppm(const char *p, const uint16_t *fb)
{
	FILE *f = fopen(p, "wb"); fprintf(f, "P6\n320 240\n255\n");
	for (int i = 0; i < 320 * 240; i++) { uint16_t c = fb[i]; uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) }; fwrite(rgb, 1, 3, f); }
	fclose(f);
}

#include <map>
static std::map<uint32_t,uint64_t> g_hist;
static std::vector<uint16_t> g_dtrace;
static void hook(uint32_t pc, void*) { g_hist[pc]++; }
static void chhook(uint32_t pc, void *c) { Sy909 *m = (Sy909 *)c; if (pc == 0x2018bd) m->dtrace_ch = 1; else if (pc == 0x2018f2 || pc == 0x245614) m->dtrace_ch = 0; }
int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: probe spi1.bin spi2.bin frames [shot_every] [script]\n"); return 1; }
	Sy909 m; std::string err;
	if (!m.load(slurp(argv[1]), slurp(argv[2]), err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	m.log = [](const char *s) { fputs(s, stdout); };
	m.trace_io = getenv("TRACE_IO") != nullptr;
	if (getenv("DTABMODE")) { int md = atoi(getenv("DTABMODE")); for (int r = 0; r < 32; r++) for (int n = 0; n < 16; n++) { int sn = n < 8 ? n : n - 16; double sc = md == 0 ? (1 << (r & 15)) : md == 1 ? (1 << (15 - (r & 15))) : md == 2 ? pow(2.0, r / 2.0) : md == 3 ? pow(2.0, (31 - r) / 2.0) : pow(2.0, (r & 15)); long v = lround(sn * sc); if (v > 32767) v = 32767; if (v < -32768) v = -32768; m.m_dtab[r * 16 + n] = (int16_t)v; } }
	if (getenv("HIST")) m.pc_hook = hook;
	if (getenv("DTRACE")) { m.dtrace = &g_dtrace; m.pc_hook = chhook; m.pc_hook_ctx = &m; }
	if (getenv("TB0")) m.set_timebase(0, atoi(getenv("TB0")));
	if (getenv("TB1")) m.set_timebase(1, atoi(getenv("TB1")));
	if (getenv("WATCH")) { sscanf(getenv("WATCH"), "%x-%x", &m.watch_lo, &m.watch_hi); }
	if (getenv("NOACCEL")) m.set_accel(false);
	if (getenv("NOIDLE")) m.set_idle_skip(false);
	if (getenv("SPEED")) m.set_speed(atoi(getenv("SPEED")));
	m.power_on();
	if (getenv("STATETEST")) {
		for (int f = 0; f < 400; f++) { m.set_buttons(f >= 300 && f < 306 ? Sy909::BTN_A : 0); m.run_frame(); }
		auto st = m.save_state(); printf("state %zu bytes\n", st.size());
		for (int f = 0; f < 120; f++) m.run_frame();
		std::vector<uint16_t> a(m.framebuffer(), m.framebuffer() + 320 * 240);
		Sy909 m2; m2.load(slurp(argv[1]), slurp(argv[2]), err); m2.power_on();
		if (!m2.load_state(st)) { printf("load failed\n"); return 1; }
		for (int f = 0; f < 120; f++) m2.run_frame();
		printf("state roundtrip %s\n", memcmp(a.data(), m2.framebuffer(), a.size() * 2) == 0 ? "OK" : "MISMATCH");
		return 0;
	}
	int frames = atoi(argv[3]); int every = argc > 4 ? atoi(argv[4]) : 60;
	// script: "frame:mask,frame:mask"
	std::string script = argc > 5 ? argv[5] : "";
	for (int f = 0; f < frames; f++)
	{
		uint16_t mask = 0;
		size_t p = 0;
		while (p < script.size()) { int fr, len, mk; if (sscanf(script.c_str() + p, "%d+%d:%x", &fr, &len, &mk) == 3 && f >= fr && f < fr + len) mask |= mk; p = script.find(',', p); if (p == std::string::npos) break; p++; }
		if (getenv("RAW")) m.set_raw_keys(mask); else m.set_buttons(mask);
		m.run_frame();
		if (every && (f % every == every - 1)) { char n[64]; snprintf(n, sizeof n, "shot_%04d.ppm", f + 1); save_ppm(n, m.framebuffer()); }
		if (getenv("WAV")) { static FILE *wf = nullptr; if (!wf) { wf = fopen(getenv("WAV"), "wb"); char h[44] = {0}; fwrite(h, 1, 44, wf); } for (int16_t v : m.audio()) fwrite(&v, 2, 1, wf); if (f == frames - 1) { long n = ftell(wf) - 44; fseek(wf, 0, SEEK_SET); uint32_t sr = m.audio_rate(); uint8_t h[44]; memcpy(h, "RIFF", 4); uint32_t v = n + 36; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8); v = 16; memcpy(h + 16, &v, 4); uint16_t w = 1; memcpy(h + 20, &w, 2); memcpy(h + 22, &w, 2); memcpy(h + 24, &sr, 4); v = sr * 2; memcpy(h + 28, &v, 4); w = 2; memcpy(h + 32, &w, 2); w = 16; memcpy(h + 34, &w, 2); memcpy(h + 36, "data", 4); v = n; memcpy(h + 40, &v, 4); fwrite(h, 1, 44, wf); fclose(wf); } }
		if (f % 60 == 59) printf("frame %d pc=%06x lcd cmds=%u pix=%u audio=%zu unk=%u\n", f + 1, m.lpc(), m.lcd().cmd_count(), m.lcd().pix_count(), m.audio().size(), m.unknown_rom_calls());
	}
	if (getenv("DTRACE")) { FILE *f = fopen(getenv("DTRACE"), "wb"); fwrite(g_dtrace.data(), 2, g_dtrace.size(), f); fclose(f); }
	if (getenv("DTAB")) { for (int r = 0; r < 32; r++) { uint32_t t = 0; for (int n = 0; n < 16; n++) t += m.m_dtab_hist[r*16+n]; if (t) { printf("row %2d:", r); for (int n = 0; n < 16; n++) printf(" %u", m.m_dtab_hist[r*16+n]); printf("\n"); } } }
	if (getenv("HIST")) { std::vector<std::pair<uint64_t,uint32_t>> v; for (auto &e : g_hist) v.push_back({e.second, e.first}); std::sort(v.rbegin(), v.rend()); uint64_t tot=0; for (auto &e : v) tot += e.first; printf("total instr %llu\n", (unsigned long long)tot); for (size_t i = 0; i < v.size() && i < 40; i++) printf("%06x %llu\n", v[i].second, (unsigned long long)v[i].first); }
	return 0;
}

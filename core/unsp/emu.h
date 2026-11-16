// Minimal shim replacing MAME's emu.h so the unSP core and disassembler build standalone.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>
#include <memory>
#include <bit>
#include <utility>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef uint32_t offs_t;
template <typename T> constexpr T BIT(T x, unsigned n) { return (x >> n) & T(1); }
namespace util {
template<typename... A> inline void stream_format(std::ostream &s, const char *f, A... a){ char b[512]; snprintf(b,sizeof b,f,a...); s<<b; }
inline void stream_format(std::ostream &s, const char *f){ s<<f; }
struct disasm_interface {
	enum : u32 { SUPPORTED=0x80000000, LENGTHMASK=0xffff, STEP_OVER=0x20000000, STEP_OUT=0x40000000, STEP_COND=0x10000000 };
	struct data_buffer { const uint16_t *w; uint32_t n; uint16_t r16(offs_t a) const { return a<n? w[a]:0xffff; } };
	virtual ~disasm_interface()=default; virtual u32 opcode_alignment() const=0;
	virtual offs_t disassemble(std::ostream&, offs_t, const data_buffer&, const data_buffer&)=0;
};
}
using data_buffer = util::disasm_interface::data_buffer;
#define STEP_OVER util::disasm_interface::STEP_OVER
#define STEP_OUT util::disasm_interface::STEP_OUT
#define STEP_COND util::disasm_interface::STEP_COND
#define SUPPORTED util::disasm_interface::SUPPORTED
void unsp_log(const char *fmt, ...);
void unsp_fatal(const char *fmt, ...);
#define fatalerror(...) unsp_fatal(__VA_ARGS__)

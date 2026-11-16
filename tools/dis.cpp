#include "unsp/emu.h"
#include "unsp/unspdasm.h"
#include <sstream>
#include <vector>
#include <cstdlib>
// usage: dis file byteoffset_of_word0 base_word_addr start_word count [swap]
int main(int c,char**v){ FILE*f=fopen(v[1],"rb"); std::vector<uint8_t> b; int ch; while((ch=fgetc(f))!=EOF) b.push_back(ch);
 long off=strtol(v[2],0,0); uint32_t base=strtoul(v[3],0,0), st=strtoul(v[4],0,0), cnt=strtoul(v[5],0,0); int swap=c>6;
 std::vector<uint16_t> w((b.size()-off)/2); for(size_t i=0;i<w.size();i++){uint8_t a=b[off+2*i],bb=b[off+2*i+1]; w[i]= swap? (a<<8|bb):(bb<<8|a);}
 // pad buffer so addresses = base + index
 std::vector<uint16_t> m(base+w.size(),0xffff); for(size_t i=0;i<w.size();i++) m[base+i]=w[i];
 unsp_20_disassembler d; data_buffer db{m.data(),(uint32_t)m.size()};
 uint32_t pc=st; for(uint32_t i=0;i<cnt && pc<m.size();i++){ std::ostringstream o; offs_t r=d.disassemble(o,pc,db,db); int len=r&0xffff; if(!len)len=1;
  printf("%06x: ",pc); for(int k=0;k<3;k++) if(k<len) printf("%04x ",m[pc+k]); else printf("     "); printf(" %s\n",o.str().c_str()); pc+=len; }
}

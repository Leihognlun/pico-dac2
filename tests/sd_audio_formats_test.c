#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "wav_reader.h"
#include "ac3_burst.h"

static const uint8_t *source; static size_t source_size, source_pos;
static int read_mem(void *ctx, uint8_t *dst, size_t size) {
  (void)ctx; if (size > source_size - source_pos) size = source_size - source_pos;
  memcpy(dst, source + source_pos, size); source_pos += size; return (int)size;
}
static void put16(uint8_t *p, uint16_t v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p, uint32_t v){put16(p,v);put16(p+2,v>>16);}
static void check_wav(uint32_t rate, unsigned bits) {
  uint8_t f[64] = {0}; memcpy(f,"RIFF",4);put32(f+4,56);memcpy(f+8,"WAVEfmt ",8);
  put32(f+16,16);put16(f+20,1);put16(f+22,2);put32(f+24,rate);
  put32(f+28,rate*2*bits/8);put16(f+32,2*bits/8);put16(f+34,bits);
  memcpy(f+36,"JUNK",4);put32(f+40,1);f[44]=0xaa;
  memcpy(f+46,"data",4);put32(f+50,bits==16?4:6);
  if(bits==16){put16(f+54,0x8000);put16(f+56,0x7fff);}else{f[54]=0;f[55]=0;f[56]=0x80;f[57]=0xff;f[58]=0xff;f[59]=0x7f;}
  source=f;source_size=bits==16?58:60;source_pos=0;wav_reader_t r;
  assert(wav_reader_open(&r,read_mem,NULL)==WAV_BLOCK_READY);
  assert(r.sample_rate==rate&&r.bits==bits);int32_t out[384];unsigned frames;
  assert(wav_next_block(&r,out,&frames)==WAV_BLOCK_READY&&frames==1);
  assert(out[0]==(bits==16?-32768:-8388608));assert(out[1]==(bits==16?32767:8388607));
  assert(wav_next_block(&r,out,&frames)==WAV_EOF);
}
static void check_ac3(void) {
  uint8_t f[128]={0x0b,0x77,0,0,0x00,0x40,0}; /* 48 kHz, 32 kb/s, 128 bytes */
  for(unsigned i=7;i<sizeof(f);++i)f[i]=(uint8_t)i;
  source=f;source_size=sizeof(f);source_pos=0;ac3_reader_t r;uint16_t out[AC3_BURST_WORDS];
  ac3_reader_init(&r,read_mem,NULL);assert(ac3_next_burst(&r,out)==AC3_BURST_READY);
  assert(out[0]==0xf872&&out[1]==0x4e1f&&out[2]==1&&out[3]==1024);
  assert(out[4]==0x0b77&&out[7]==0x0007&&out[67]==0x7e7f);
  assert(ac3_next_burst(&r,out)==AC3_EOF);
}
static void check_extensible(void) {
  uint8_t f[74] = {0}; memcpy(f,"RIFF",4);put32(f+4,66);memcpy(f+8,"WAVEfmt ",8);
  put32(f+16,40); put16(f+20,0xfffe); put16(f+22,2); put32(f+24,96000);
  put32(f+28,576000); put16(f+32,6); put16(f+34,24); put16(f+36,22);
  put16(f+38,24); put32(f+40,3);
  const uint8_t guid[16]={1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
  memcpy(f+44,guid,16); memcpy(f+60,"data",4); put32(f+64,6);
  f[68]=0;f[69]=0;f[70]=0x80;f[71]=0xff;f[72]=0xff;f[73]=0x7f;
  source=f;source_size=sizeof(f);source_pos=0;wav_reader_t r;int32_t out[384];unsigned frames;
  assert(wav_reader_open(&r,read_mem,NULL)==WAV_BLOCK_READY);
  assert(r.sample_rate==96000&&r.bits==24);
  assert(wav_next_block(&r,out,&frames)==WAV_BLOCK_READY&&frames==1);
  assert(out[0]==-8388608&&out[1]==8388607);
}
int main(void){uint32_t rates[]={44100,48000,96000,192000};for(unsigned r=0;r<4;++r){check_wav(rates[r],16);check_wav(rates[r],24);}check_extensible();check_ac3();puts("PASS: PCM/Extensible WAV 44.1/48/96/192 kHz 16/24-bit and 48 kHz AC-3 packing");}

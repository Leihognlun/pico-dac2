#include "ac3_burst.h"
#include <string.h>
static int exact(ac3_reader_t *r, uint8_t *p, size_t n) {
  size_t d=0; while(d<n){int g=r->read(r->ctx,p+d,n-d);if(g<0)return AC3_IO_ERROR;if(!g)return d?AC3_TRUNCATED:AC3_EOF;d+=(size_t)g;}return 1;
}
void ac3_reader_init(ac3_reader_t *r, ac3_read_fn fn, void *ctx){r->read=fn;r->ctx=ctx;r->terminal=1;}
int ac3_next_burst(ac3_reader_t *r, uint16_t out[AC3_BURST_WORDS]) {
  static const uint16_t words[3][38] = {
    {64,64,80,80,96,96,112,112,128,128,160,160,192,192,224,224,256,256,320,320,384,384,448,448,512,512,640,640,768,768,896,896,1024,1024,1152,1152,1280,1280},
    {69,70,87,88,104,105,121,122,139,140,174,175,208,209,243,244,278,279,348,349,417,418,487,488,557,558,696,697,835,836,975,976,1114,1115,1253,1254,1393,1394},
    {96,96,120,120,144,144,168,168,192,192,240,240,288,288,336,336,384,384,480,480,576,576,672,672,768,768,960,960,1152,1152,1344,1344,1536,1536,1728,1728,1920,1920}};
  if (r->terminal <= 0) return r->terminal;
  uint8_t h[7];
  int rc = exact(r, h, 7);
  if (rc <= 0) { r->terminal = rc; return rc; }
  if(h[0]!=0x0b||h[1]!=0x77){r->terminal=AC3_BAD_HEADER;return r->terminal;}
  unsigned fscod=h[4]>>6, frmsizecod=h[4]&0x3f, bsid=h[5]>>3;
  if(fscod>2||frmsizecod>37||bsid>10){r->terminal=AC3_UNSUPPORTED;return r->terminal;}
  uint32_t rate=fscod==0?48000:fscod==1?44100:32000; if(rate!=48000){r->terminal=AC3_UNSUPPORTED;return r->terminal;}
  unsigned size=words[fscod][frmsizecod]*2; if(size>AC3_BURST_WORDS*2-8){r->terminal=AC3_OVERFLOW;return r->terminal;}
  uint8_t frame[3840]; memcpy(frame,h,7); rc=exact(r,frame+7,size-7); if(rc<=0){r->terminal=AC3_TRUNCATED;return r->terminal;}
  memset(out,0,AC3_BURST_WORDS*2); out[0]=0xf872;out[1]=0x4e1f;out[2]=0x0001;out[3]=(uint16_t)(size*8);
  for(unsigned i=0;i<size/2;++i) out[4+i]=(uint16_t)frame[2*i]<<8|frame[2*i+1];
  return AC3_BURST_READY;
}
const char *ac3_result_string(int r){switch(r){case 0:return "end of file";case 1:return "burst ready";case -1:return "TF read error";case -2:return "truncated AC-3";case -3:return "invalid AC-3 sync/header";case -4:return "requires raw 48 kHz AC-3";case -5:return "AC-3 frame too large";default:return "unknown AC-3 error";}}

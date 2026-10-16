#include "cec_sad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cec_frame_t sent;
static bool accept = true;
static unsigned count;
static bool send_frame(void *ctx, const cec_frame_t *f) {
  (void)ctx;
  if (!accept) return false;
  sent = *f; ++count; return true;
}
static void start(cec_sad_t *s) {
  cec_sad_init(s, 0);
  cec_sad_task(s, true, send_frame, NULL, 0);
  cec_sad_task(s, true, send_frame, NULL, 1000000);
  assert(sent.len == 3 && sent.data[0] == 0x05 && sent.data[1] == 0xa4 && sent.data[2] == 1);
  cec_sad_tx_result(s, sent.tag, CEC_TX_OK, 1100000);
}
int main(void) {
  cec_sad_t s;
  cec_sad_init(&s, 0);
  cec_sad_task(&s, false, send_frame, NULL, 0);
  assert(!count);
  cec_sad_task(&s, true, send_frame, NULL, 0);
  cec_sad_task(&s, true, send_frame, NULL, 999999);
  assert(!count);
  accept = false;
  cec_sad_task(&s, true, send_frame, NULL, 1000000);
  assert(!count && !s.tries);
  accept = true;
  start(&s);
  cec_frame_t report = {.data={0x50, 0xa3, 0x09, 0x07, 0x07}, .len=5};
  report.data[0] = 0x40;
  assert(!cec_sad_receive(&s, &report, 1200000));
  report.data[0] = 0x50; report.len = 4;
  assert(cec_sad_receive(&s, &report, 1200000) && s.codec == 1);
  report.len = 5; report.data[2] = 0x11;
  assert(cec_sad_receive(&s, &report, 1200000) && s.codec == 1);
  report.data[2] = 0x09;
  assert(cec_sad_receive(&s, &report, 1200000));
  assert(s.codec == 2 && s.reported == 2 && !memcmp(s.descriptors[0], report.data+2, 3));
  char text[192];
  cec_sad_describe(report.data+2, text, sizeof(text));
  assert(strstr(text, "LPCM") && strstr(text, "PCM44.1=yes") && strstr(text, "max_channels=2"));
  uint8_t ac3[] = {0x15, 7, 80}, eac3[] = {0x55, 7, 1};
  cec_sad_describe(ac3, text, sizeof(text)); assert(strstr(text, "max_kbps=640"));
  cec_sad_describe(eac3, text, sizeof(text)); assert(strstr(text, "format_specific=0x01") && !strstr(text, "max_kbps"));
  start(&s);
  cec_sad_task(&s, true, send_frame, NULL, 3100000);
  assert(s.tries == 2 && s.stage == 1);
  cec_sad_tx_result(&s, sent.tag, CEC_TX_OK, 3200000);
  cec_sad_task(&s, true, send_frame, NULL, 5200000);
  assert(s.codec == 2 && s.stage == 0);
  start(&s);
  cec_frame_t abort = {.data={0x50,0,0xa4,3}, .len=4};
  assert(cec_sad_receive(&s, &abort, 1200000) && s.codec == 2);
  start(&s); abort.data[3] = 0;
  assert(cec_sad_receive(&s, &abort, 1200000) && s.codec == 15);
  unsigned before = count;
  cec_sad_task(&s, true, send_frame, NULL, 9000000);
  assert(count == before);
  cec_sad_task(&s, false, send_frame, NULL, 9000001);
  assert(!s.active && !s.reported && s.codec == 1);
  start(&s);
  for (unsigned code = 1; code <= 14; ++code) {
    report.data[2] = (uint8_t)(code << 3);
    assert(cec_sad_receive(&s, &report, 1200000));
    if (code < 14) {
      cec_sad_task(&s, true, send_frame, NULL, 1500000);
      assert(sent.data[2] == code + 1);
      cec_sad_tx_result(&s, sent.tag, CEC_TX_OK, 1600000);
    }
  }
  assert(s.codec == 15 && s.reported == 0x7ffe);
  puts("CEC SAD tests passed");
  return 0;
}

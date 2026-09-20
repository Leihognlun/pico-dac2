#include "cec_tv.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cec_frame_t sent[128];
static unsigned count;
static bool enqueue(void *ctx, const cec_frame_t *f) {
  (void)ctx;
  assert(count < 128); sent[count++] = *f; return true;
}
static void receive(cec_tv_t *tv, unsigned header, unsigned op, uint32_t now) {
  cec_frame_t f = {.data = {header, op}, .len = 2};
  cec_tv_receive(tv, &f, now);
}
static void controller(void) {
  cec_tv_t tv;
  cec_tv_init(&tv, enqueue, NULL, 0);
  cec_tv_task(&tv, 999999); assert(count == 0);
  cec_tv_task(&tv, 1000000);
  assert(count == 1 && sent[0].len == 1 && sent[0].data[0] == 0);
  cec_tv_tx_result(&tv, CEC_TAG_POLL_TV, CEC_TX_OK, 1100000);
  assert(tv.conflict && !tv.registered);
  cec_tv_task(&tv, 2000000); assert(count == 1);
  count = 0; cec_tv_init(&tv, enqueue, NULL, 0);
  cec_tv_task(&tv, 1000000);
  cec_tv_tx_result(&tv, CEC_TAG_POLL_TV, CEC_TX_NACK, 1100000);
  assert(tv.registered && sent[1].data[1] == 0x84 && sent[1].len == 5);
  cec_tv_task(&tv, 1100000);
  assert(sent[2].data[1] == 0x70 && sent[3].data[1] == 0xc3);
  assert(tv.arc == CEC_ARC_REQUESTED);
  receive(&tv, 0x40, 0xc0, 1200000); assert(tv.arc == CEC_ARC_REQUESTED);
  receive(&tv, 0x5f, 0xc0, 1200000); assert(tv.arc == CEC_ARC_REQUESTED);
  receive(&tv, 0x50, 0xc0, 1200000);
  assert(tv.arc == CEC_ARC_REPORTING && sent[count-1].data[1] == 0xc1);
  cec_tv_tx_result(&tv, CEC_TAG_ENABLE, CEC_TX_OK, 1300000);
  assert(tv.arc == CEC_ARC_ON);
  receive(&tv, 0x50, 0xc0, 1400000); assert(tv.arc == CEC_ARC_ON);
  tv.key = 0x41; cec_tv_task(&tv, 1500000);
  assert(sent[count-1].data[1] == 0x44 && sent[count-1].data[2] == 0x41);
  unsigned n = count; cec_tv_task(&tv, 1799999); assert(count == n);
  cec_tv_task(&tv, 1800000); assert(count == n+1);
  tv.key = 0; cec_tv_task(&tv, 1800100); assert(sent[count-1].data[1] == 0x45);
  cec_tv_request_arc(&tv, false, 1900000); assert(tv.arc == CEC_ARC_STOPPING);
  cec_tv_task(&tv, 1900000); assert(sent[count-2].data[1] == 0xc4);
  receive(&tv, 0x50, 0xc5, 2000000); assert(sent[count-1].data[1] == 0xc2);
  cec_tv_tx_result(&tv, CEC_TAG_DISABLE, CEC_TX_OK, 2100000);
  assert(tv.arc == CEC_ARC_OFF && !tv.desired);
  receive(&tv, 0x50, 0xc0, 2200000); assert(sent[count-1].data[1] == 0);
  receive(&tv, 0x50, 0x9f, 2300000);
  assert(sent[count-1].data[1] == 0x9e && sent[count-1].data[2] == 5);
  receive(&tv, 0x50, 0xee, 2400000); assert(sent[count-1].data[1] == 0);
  n = count; receive(&tv, 0x5f, 0xee, 2400000); assert(count == n);
  cec_tv_request_arc(&tv, true, 2500000); cec_tv_task(&tv, 2500000);
  cec_tv_task(&tv, 6500000); assert(tv.arc == CEC_ARC_OFF);
  cec_tv_task(&tv, 11500000); assert(tv.arc == CEC_ARC_REQUESTED);
  receive(&tv, 0x50, 0xc0, 11600000);
  cec_tv_tx_result(&tv, CEC_TAG_ENABLE, CEC_TX_NACK, 11700000);
  assert(tv.arc == CEC_ARC_OFF);
}

// Two independently sampled devices on a wired-AND bus, 50 us scheduling.
static void run_bus(cec_wire_t *a, cec_wire_t *b, uint32_t start, unsigned duration) {
  for (unsigned dt = 0; dt < duration; dt += 50) {
    bool high = !a->low && !b->low;
    cec_wire_tick(a, high, start + dt);
    cec_wire_tick(b, high, start + dt);
  }
}
static void transfer(unsigned header, unsigned len, bool listener, unsigned result) {
  cec_wire_t a, b;
  cec_wire_init(&a, 0); cec_wire_init(&b, 0);
  b.address = listener ? header & 15 : 14;
  cec_frame_t f = {.len = len};
  for (unsigned i = 0; i < len; ++i) f.data[i] = (uint8_t)(i * 37);
  f.data[0] = header;
  assert(cec_wire_send(&a, &f, 0));
  assert(!cec_wire_send(&a, &f, 0));
  run_bus(&a, &b, 0, 500000);
  assert(a.tx_result == result && !a.low && !b.low);
  if (result == CEC_TX_OK) {
    assert(b.rx_ready && b.received.len == len);
    assert(memcmp(b.received.data, f.data, len) == 0);
    assert(b.rx_errors == 0);
  }
}
static void wire_tests(void) {
  transfer(0x50, 2, true, CEC_TX_OK);
  transfer(0x05, 16, true, CEC_TX_OK);
  transfer(0x05, 2, false, CEC_TX_NACK);
  transfer(0x0f, 5, false, CEC_TX_OK);
  transfer(0x00, 1, true, CEC_TX_OK);
  transfer(0x00, 1, false, CEC_TX_NACK);
  cec_wire_t a, b;
  cec_wire_init(&a, 0); cec_wire_init(&b, 0);
  cec_frame_t f = {.data = {0x05, 0xc3}, .len = 2};
  assert(cec_wire_send(&a, &f, 0));
  for (unsigned t = 0; t < 2010000; t += 50) cec_wire_tick(&a, false, t);
  assert(a.tx_result == CEC_TX_TIMEOUT && !a.low);
  // Timer lateness cannot leave the bus driven indefinitely.
  cec_wire_init(&a, 0); cec_wire_send(&a, &f, 0);
  cec_wire_tick(&a, true, 20000); assert(a.low);
  cec_wire_tick(&a, false, 25000);
  assert(a.tx_result == CEC_TX_TIMEOUT && !a.low);
  // Timer wrap must not break a frame.
  uint32_t start = UINT32_MAX - 30000;
  cec_wire_init(&a, start); cec_wire_init(&b, start); b.address = 5;
  cec_wire_send(&a, &f, start); run_bus(&a, &b, start, 100000);
  assert(a.tx_result == CEC_TX_OK && b.rx_ready);
  // Simultaneous initiators with differing addresses: the high bit loses.
  cec_wire_init(&a, 0); cec_wire_init(&b, 0);
  cec_wire_send(&a, &f, 0); f.data[0] = 0x45; cec_wire_send(&b, &f, 0);
  run_bus(&a, &b, 0, 100000); assert(b.tx_result == CEC_TX_COLLISION);
}
int main(void) {
  controller(); wire_tests(); puts("CEC controller and wire tests passed"); return 0;
}

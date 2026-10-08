#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "cec_arc.c"
static bool levels[30], links[2];
static uint32_t now;
uint32_t time_us_32(void) { return now; }
void gpio_init(unsigned p) { assert(p < 30); }
void gpio_put(unsigned p, bool value) { levels[p] = value; }
void gpio_set_dir(unsigned p, bool out) { (void)p; (void)out; }
void gpio_disable_pulls(unsigned p) { (void)p; }
bool gpio_get(unsigned p) { return levels[p]; }
bool add_repeating_timer_us(int64_t period, bool (*fn)(repeating_timer_t *),
                            void *ctx, repeating_timer_t *t) {
  (void)ctx; (void)t; assert(period == -50 && fn); return true;
}
void panic(const char *s) { (void)s; abort(); }
void spdif_set_link_enabled(bool on) { links[0] = links[1] = on; }
void spdif_set_port_link_enabled(unsigned p, bool on) { links[p] = on; }
static void receive(unsigned port, uint8_t opcode) {
  ports[port].wire.received = (cec_frame_t){.data = {0x50, opcode}, .len = 2};
  ports[port].wire.rx_ready = true;
  cec_arc_task();
  board_status_task();
}
static void report_volume(unsigned port, uint8_t volume) {
  ports[port].wire.received =
      (cec_frame_t){.data = {0x50, 0x7a, volume}, .len = 3};
  ports[port].wire.rx_ready = true;
  cec_arc_task();
}
int main(void) {
  board_status_init();
  cec_arc_init();
  assert(!links[0] && !links[1] && levels[28]);
  levels[29] = true;
  cec_arc_task();
  assert(!levels[28] && !links[0] && !links[1]);
  for (unsigned i = 0; i < 2; ++i) {
    ports[i].tv.registered = true;
    ports[i].tv.arc = CEC_ARC_ON;
  }
  cec_arc_task();
  board_status_task();
  assert(links[0] && !links[1] && levels[17] && !levels[16]);
  levels[19] = true; cec_arc_task(); board_status_task();
  assert(links[0] && links[1] && levels[16]);
  ports[0].head = ports[0].tail = ports[0].count = 0;
  ports[0].tv.soundbar_present = true;
  ports[0].tv.arc = CEC_ARC_OFF;
  ports[0].volume_status_due = now;
  volume_task(&ports[0], now, true);
  assert(ports[0].count == 0); // No 05:71 while ARC is disconnected.
  ports[0].tv.arc = CEC_ARC_ON;
  // Each inactive port resets only outside the inclusive 20 +/- 2 range.
  ports[0].tv.soundbar_present = true;
  ports[0].head = ports[0].tail = ports[0].count = 0;
  ports[0].wire.tx_pending = false; ports[0].wire.tx_result = 0;
  cec_arc_set_port_playing(1, true);
  cec_arc_set_port_playing(0, true);
  now = 1000000; cec_arc_set_port_playing(0, false);
  now += 30000000; report_volume(0, 17);
  assert(!ports[0].tv.key); // ARC2 is still playing: no port may reset.
  cec_arc_set_port_playing(1, false);
  now += 29999000; cec_arc_task(); assert(!ports[0].tv.key);
  now += 1000; cec_arc_task();
  bool requested = false;
  for (unsigned i = 0, q = ports[0].tail; i < ports[0].count;
       ++i, q = (q + 1) % 8)
    requested |= ports[0].queue[q].data[1] == 0x71;
  assert(requested && !ports[0].tv.key);
  report_volume(0, 17); assert(ports[0].tv.key == 0x41);
  cec_arc_task(); assert(!ports[0].tv.key && ports[0].tv.sent_key == 0x41);
  ports[0].tv.sent_key = 0;
  report_volume(0, 18); assert(!ports[0].tv.key);
  report_volume(0, 22); assert(!ports[0].tv.key);
  report_volume(0, 23); assert(ports[0].tv.key == 0x42);
  ports[0].tv.key = ports[0].tv.sent_key = ports[0].reset_key = 0;
  ports[0].head = ports[0].tail = ports[0].count = 0;
  ports[0].wire.tx_pending = false;
  ports[0].wire.tx_result = 0;
  cec_arc_set_port_playing(0, true);
  // Directed termination on ARC1 must never terminate ARC2.
  receive(0, 0xc5);
  assert(!ports[0].tv.desired && ports[1].tv.desired);
  assert(!links[0] && links[1] && !levels[28]);
  cec_arc_request_port_playback(0);
  assert(ports[0].tv.desired && ports[1].tv.desired);
  cec_arc_port_volume_key(1, true, true);
  assert(!ports[0].tv.key && ports[1].tv.key == 0x41);
  cec_arc_port_volume_key(1, true, false);
  assert(!ports[1].tv.key);
  now = 32000000;
  levels[29] = false; cec_arc_task();
  assert(!levels[28] && links[1]);
  assert(!ports[0].tv.registered && ports[0].tv.desired &&
         !ports[0].tv.soundbar_present && ports[0].tv.arc == CEC_ARC_OFF &&
         ports[0].tv.volume == 127 && ports[0].count == 0 &&
         ports[0].wire.address == 15);
  levels[19] = false; cec_arc_task(); board_status_task();
  assert(levels[28] && !links[0] && !links[1]);
  assert(!cec_arc_audio_allowed() && !levels[17] && !levels[16]);
  puts("PASS: independent CEC ports, link gates, volume routing and shared HPD OR");
}

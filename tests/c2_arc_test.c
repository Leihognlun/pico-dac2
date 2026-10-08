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
  levels[29] = false; cec_arc_task();
  assert(!levels[28] && links[1]);
  levels[19] = false; cec_arc_task(); board_status_task();
  assert(levels[28] && !links[0] && !links[1]);
  assert(!cec_arc_audio_allowed() && !levels[17] && !levels[16]);
  puts("PASS: independent CEC ports, link gates, volume routing and shared HPD OR");
}

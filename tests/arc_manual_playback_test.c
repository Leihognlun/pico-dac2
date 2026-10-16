#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "cec_arc.c"

static bool levels[30], links[BOARD_ARC_COUNT];
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
void spdif_set_link_enabled(bool on) {
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) links[i] = on;
}
void spdif_set_port_link_enabled(unsigned p, bool on) {
  assert(p < BOARD_ARC_COUNT); links[p] = on;
}
static void sample(void) { tick(&timer); cec_arc_task(); board_status_task(); }
static void request_playback(unsigned port) {
#if BOARD_ARC_COUNT > 1
  cec_arc_request_port_playback(port);
#else
  assert(port == 0); cec_arc_request_playback();
#endif
}
static void connect(unsigned port) {
  levels[detect_pins[port]] = true;
  sample();
  now += BOARD_HDMI_DETECT_DEBOUNCE_MS * 1000u;
  sample();
  assert(ports[port].present);
}
static void establish_arc(unsigned port) {
  ports[port].tv.registered = true;
  ports[port].tv.soundbar_present = true;
  ports[port].tv.arc = CEC_ARC_ON;
  ports[port].tv.desired = true;
  sample();
  assert(ports[port].gate && links[port]);
}

int main(void) {
  const unsigned yellow[] = {BOARD_ARC1_LED,
#if BOARD_ARC_COUNT > 1
      BOARD_ARC2_LED,
#endif
  };
  board_status_init(); cec_arc_init();
  assert(!cec_arc_audio_allowed());
  // A request on an unselected port must not bypass ARC.
  for (unsigned p = 0; p < BOARD_ARC_COUNT; ++p) {
    request_playback(p);
    assert(!links[p] && !cec_arc_port_audio_allowed(p));
  }
  for (unsigned p = 0; p < BOARD_ARC_COUNT; ++p) {
    board_status_set_arc_selected(p, true);
    // Selection alone (including C1 auto-start) retains the boot ARC gate.
    cec_arc_set_port_playing(p, true);
    assert(!links[p] && !cec_arc_audio_allowed());
    request_playback(p);
    assert(links[p] && cec_arc_port_audio_allowed(p) && cec_arc_audio_allowed());
    for (unsigned other = 0; other < BOARD_ARC_COUNT; ++other)
      if (other != p) assert(!links[other]);
    // No cable: data is permitted but ARC LEDs still blink as disconnected.
    now = 1000000; sample(); assert(levels[yellow[p]]);
    now = 2000000; sample(); assert(!ports[p].gate && links[p]);
    assert(!levels[yellow[p]]);
    connect(p); assert(!ports[p].gate && links[p]); // Handshake still pending.
    establish_arc(p);
    ports[p].tv.arc = CEC_ARC_OFF;
    ports[p].tv.retry_at = now + 5000000;
    sample(); assert(!ports[p].gate && links[p]); // ARC termination keeps payload.
    levels[detect_pins[p]] = false;
    sample(); assert(!ports[p].present && links[p]); // Unplug also keeps payload.
    cec_arc_set_port_playing(p, false);
    assert(!links[p] && !cec_arc_audio_allowed());
    cec_arc_set_port_playing(p, true);
    assert(!links[p]); // Stop revoked manual permission.
    request_playback(p); assert(links[p]);
    connect(p); establish_arc(p);
    cec_arc_set_port_playing(p, false);
    assert(links[p] && cec_arc_audio_allowed()); // Connected idle carrier remains.
    levels[detect_pins[p]] = false;
    sample(); assert(!links[p] && !cec_arc_audio_allowed());
  }
#if BOARD_ARC_COUNT > 1
  cec_arc_set_port_playing(0, true); request_playback(0);
  cec_arc_set_port_playing(1, true); request_playback(1);
  cec_arc_set_port_playing(0, false); // One port still playing: clear old override.
  assert(!links[0] && links[1]);
  cec_arc_set_port_playing(1, false);
  assert(!links[0] && !links[1]);
#endif
  puts("PASS: manual playback without ARC, real connection LEDs, handshake/unplug continuity and stop revocation");
}

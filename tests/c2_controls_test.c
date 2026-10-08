#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "sd_controls.h"
static bool pins[30], enabled[2], allowed[2] = {true, true};
static unsigned requests[2], volume[2], releases[2];
static uint32_t now;
uint32_t time_us_32(void) { return now; }
void gpio_init(unsigned p) { assert(p >= 6 && p <= 15); }
void gpio_set_dir(unsigned p, int d) { (void)p; (void)d; }
void gpio_pull_up(unsigned p) { pins[p] = true; }
void gpio_put(unsigned p, int v) { pins[p] = v; }
int gpio_get(unsigned p) { return pins[p]; }
void spdif_set_port_playing(unsigned p, bool on) {
  assert(p < 2);
  if (on) enabled[1 - p] = false;
  enabled[p] = on;
}
void cec_arc_request_port_playback(unsigned p) { ++requests[p]; }
void cec_arc_set_port_playing(unsigned p, bool playing) {
  assert(p < 2); (void)playing;
}
bool cec_arc_port_audio_allowed(unsigned p) { return allowed[p]; }
void cec_arc_port_volume_key(unsigned p, bool up, bool pressed) {
  if (!pressed) ++releases[p]; else volume[p] = up ? 1 : 2;
}
static void key(unsigned pin, bool down) {
  pins[pin] = !down; sd_controls_task(); now += 21000; sd_controls_task();
}
int main(void) {
  sd_controls_init();
  assert(!sd_controls_playing() && !enabled[0] && !enabled[1]);
  assert(!pins[6] && !pins[8] && !pins[10] && !pins[12] && !pins[14]);
  key(13, true); key(13, false); key(11, true); key(11, false);
  assert(!volume[0] && !volume[1] && !sd_controls_next_generation());
  pins[7] = false; sd_controls_task(); now += 1000;
  pins[7] = true; sd_controls_task(); now += 21000; sd_controls_task();
  assert(!sd_controls_playing());
  key(7, true); key(7, false);
  assert(enabled[0] && !enabled[1] && pins[6] && !pins[8]);
  assert(sd_controls_playing() && sd_controls_audio_allowed());
  allowed[0] = false; assert(!sd_controls_audio_allowed()); allowed[0] = true;
  key(13, true); assert(volume[0] == 1 && volume[1] == 0);
  unsigned old_releases = releases[0];
  key(9, true); key(9, false); // Switch while volume-up is held.
  assert(!enabled[0] && enabled[1] && !pins[6] && pins[8]);
  assert(releases[0] == old_releases + 1);
  key(13, false);
  key(15, true); key(15, false);
  assert(volume[0] == 1 && volume[1] == 2);
  key(11, true); assert(sd_controls_next_pressed()); key(11, false);
  assert(sd_controls_next_generation() == 1);
  key(9, true); key(9, false); // Press current port: stop.
  assert(!sd_controls_playing() && !enabled[0] && !enabled[1]);
  unsigned starts = sd_controls_start_generation();
  assert(!pins[6] && !pins[8] && !pins[10]);
  key(11, true); key(11, false); assert(sd_controls_next_generation() == 1);
  key(9, true); key(9, false); // B2 also starts from stopped.
  assert(!enabled[0] && enabled[1]);
  assert(sd_controls_start_generation() == starts + 1);
  assert(sd_controls_start_from_next());
  key(7, true); key(7, false);
  assert(enabled[0] && !enabled[1]);
  assert(requests[0] == 2 && requests[1] == 2 && sd_controls_play_generation() == 4);
  sd_controls_stop();
  assert(!enabled[0] && !enabled[1] && !pins[6] && !pins[8]);
  // Simultaneous presses are processed in button order; never both selected.
  pins[7] = pins[9] = false; sd_controls_task(); now += 21000; sd_controls_task();
  assert(!enabled[0] && enabled[1] && !pins[6] && pins[8]);
  assert(sd_controls_start_from_next()); // Automatic standby also resumes next.
  pins[7] = pins[9] = true; sd_controls_task(); now += 21000; sd_controls_task();
  key(9, true); key(9, false);
  now += 10000001u;
  key(9, true); key(9, false);
  assert(!sd_controls_start_from_next());
  sd_controls_stop();
  now += 9000000u;
  sd_controls_stop(); // Already idle: do not restart the ten-second timer.
  now += 1000001u;
  key(7, true); key(7, false);
  assert(!sd_controls_start_from_next());
  sd_controls_stop();
  now += 5000000u;
  key(9, true); key(9, false);
  assert(sd_controls_start_from_next());
  puts("PASS: C2 stopped boot, exclusive ARC selection/toggle, LEDs, volume release/routing and debounce");
}

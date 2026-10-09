#include "sd_controls.h"
#include "board.h"
#include "board_status.h"
#include "cec_arc.h"
#include "spdif.h"
#include "pico/stdlib.h"
#include "button_led.h"
#include <stdatomic.h>

static const unsigned keys[] = BOARD_KEY_PINS;
static const unsigned leds[] = BOARD_LED_PINS;
#if BOARD_ARC_COUNT == 1
static const unsigned leds_n[] = BOARD_LED_N_PINS;
#endif
static uint8_t raw, stable;
static uint32_t changed[BOARD_BUTTON_COUNT];
// C2 starts with no selected output; C1 retains its single-port boot behavior.
#define INITIAL_PLAYING_MASK (BOARD_ARC_COUNT == 1 ? 1u : 0u)
static atomic_uint playing_mask = INITIAL_PLAYING_MASK;
static atomic_uint next_generation, play_generation;
static atomic_uint start_generation;
static atomic_bool start_from_next;
static uint32_t idle_started;
static bool was_idle;
static uint32_t stop_at;
static bool stop_valid;

#define QUICK_RESUME_US 10000000u

static void update_leds(void) {
  unsigned mask = atomic_load_explicit(&playing_mask, memory_order_acquire);
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i)
    board_status_set_arc_selected(i, !!(mask & (1u << i)));
  uint32_t now = time_us_32();
  if (!mask && !was_idle) idle_started = now;
  was_idle = !mask;
  for (unsigned i = 0; i < BOARD_BUTTON_COUNT; ++i) {
#if BOARD_ARC_COUNT == 1
    gpio_put(leds_n[i], false);
    bool on = i == 0 || mask;
#else
    bool on = i < BOARD_ARC_COUNT ? !!(mask & (1u << i)) : !!mask;
#endif
    button_led_write(leds[i], !mask && i < BOARD_ARC_COUNT ?
                     button_led_idle_level(i, now - idle_started) : (on ? 10000 : 0));
  }
}
static void release_volume(unsigned port) {
#if BOARD_ARC_COUNT > 1
  cec_arc_port_volume_key(port, true, false);
#else
  (void)port;
  cec_arc_volume_key(true, false);
  cec_arc_volume_key(false, false);
#endif
}
void sd_controls_init(void) {
  raw = stable = 0;
  was_idle = false;
  atomic_store(&playing_mask, INITIAL_PLAYING_MASK);
  atomic_store(&next_generation, 0);
  atomic_store(&play_generation, 0);
  atomic_store(&start_generation, 0);
  atomic_store(&start_from_next, false);
  stop_at = 0;
  stop_valid = false;
  for (unsigned i = 0; i < BOARD_BUTTON_COUNT; ++i) {
    gpio_init(keys[i]); gpio_set_dir(keys[i], GPIO_IN); gpio_pull_up(keys[i]);
    gpio_init(leds[i]); gpio_put(leds[i], false); gpio_set_dir(leds[i], GPIO_OUT);
#if BOARD_ARC_COUNT == 1
    gpio_init(leds_n[i]); gpio_put(leds_n[i], false); gpio_set_dir(leds_n[i], GPIO_OUT);
#endif
    changed[i] = time_us_32();
  }
#if BOARD_ARC_COUNT > 1
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) spdif_set_port_playing(i, false);
#endif
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i)
    cec_arc_set_port_playing(i, !!(INITIAL_PLAYING_MASK & (1u << i)));
  update_leds();
}
void sd_controls_task(void) {
  uint32_t now = time_us_32();
  for (unsigned i = 0; i < BOARD_BUTTON_COUNT; ++i) {
    unsigned bit = 1u << i;
    bool down = !gpio_get(keys[i]);
    if (down != !!(raw & bit)) { raw ^= bit; changed[i] = now; }
    if (now - changed[i] < 20000 || down == !!(stable & bit)) continue;
    stable ^= bit;
    unsigned mask = atomic_load_explicit(&playing_mask, memory_order_acquire);
    if (i < BOARD_ARC_COUNT && down) {
      unsigned previous = mask;
      mask = (mask & bit) ? 0u : bit;
      atomic_store_explicit(&playing_mask, mask, memory_order_release);
      for (unsigned port = 0; port < BOARD_ARC_COUNT; ++port)
        cec_arc_set_port_playing(port, !!(mask & (1u << port)));
#if BOARD_ARC_COUNT > 1
      // Release the old receiver's held volume key before selecting another.
      for (unsigned port = 0; port < BOARD_ARC_COUNT; ++port) {
        if ((previous & (1u << port)) && !(mask & (1u << port)))
          release_volume(port);
      }
      spdif_set_port_playing(i, !!(mask & bit));
#else
      (void)previous;
#endif
      if (mask & bit) {
        atomic_fetch_add_explicit(&play_generation, 1, memory_order_release);
        if (!previous) {
          bool resume = stop_valid &&
                        (uint32_t)(now - stop_at) <= QUICK_RESUME_US;
          atomic_store_explicit(&start_from_next, resume, memory_order_relaxed);
          atomic_fetch_add_explicit(&start_generation, 1,
                                    memory_order_release);
          stop_valid = false;
        }
#if BOARD_ARC_COUNT > 1
        cec_arc_request_port_playback(i);
#else
        cec_arc_request_playback();
#endif
      }
      else if (previous) {
        stop_at = now;
        stop_valid = true;
      }
#if BOARD_ARC_COUNT == 1
      else release_volume(i);
#endif
      update_leds();
    } else if (mask && i == BOARD_NEXT_KEY && down) {
      atomic_fetch_add_explicit(&next_generation, 1, memory_order_release);
    } else if (i == BOARD_VOLUME_UP_KEY || i == BOARD_VOLUME_DOWN_KEY) {
      for (unsigned port = 0; port < BOARD_ARC_COUNT; ++port) {
        if (!(mask & (1u << port))) continue;
#if BOARD_ARC_COUNT > 1
        cec_arc_port_volume_key(port, i == BOARD_VOLUME_UP_KEY, down);
#else
        cec_arc_volume_key(i == BOARD_VOLUME_UP_KEY, down);
#endif
      }
    }
  }
  update_leds();
}
bool sd_controls_playing(void) {
  return atomic_load_explicit(&playing_mask, memory_order_acquire) != 0;
}
bool sd_controls_port_playing(unsigned port) {
  return port < BOARD_ARC_COUNT &&
      !!(atomic_load_explicit(&playing_mask, memory_order_acquire) &
         (1u << port));
}
#if BOARD_ARC_COUNT > 1
bool sd_controls_audio_allowed(void) {
  unsigned mask = atomic_load_explicit(&playing_mask, memory_order_acquire);
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i)
    if ((mask & (1u << i)) && cec_arc_port_audio_allowed(i)) return true;
  return false;
}
bool sd_controls_next_pressed(void) {
  return sd_controls_playing() && !!(stable & (1u << BOARD_NEXT_KEY));
}
#endif
void sd_controls_stop(void) {
  // Automatic EOF standby has the same quick-resume window as a button stop.
  // Repeated stop calls while already idle must not extend the window.
  if (sd_controls_playing()) {
    stop_at = time_us_32();
    stop_valid = true;
  }
  atomic_store_explicit(&playing_mask, 0, memory_order_release);
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) {
    cec_arc_set_port_playing(i, false);
    release_volume(i);
#if BOARD_ARC_COUNT > 1
    spdif_set_port_playing(i, false);
#endif
  }
  update_leds();
}
unsigned sd_controls_next_generation(void) {
  return atomic_load_explicit(&next_generation, memory_order_acquire);
}
unsigned sd_controls_play_generation(void) {
  return atomic_load_explicit(&play_generation, memory_order_acquire);
}
unsigned sd_controls_start_generation(void) {
  return atomic_load_explicit(&start_generation, memory_order_acquire);
}
bool sd_controls_start_from_next(void) {
  (void)atomic_load_explicit(&start_generation, memory_order_acquire);
  return atomic_load_explicit(&start_from_next, memory_order_relaxed);
}

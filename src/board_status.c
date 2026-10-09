#include "board_status.h"
#include "board.h"
#include "pico/stdlib.h"
#include <stdatomic.h>

static const unsigned yellow_pins[] = {BOARD_ARC1_LED,
#if BOARD_ARC_COUNT > 1
  BOARD_ARC2_LED
#endif
};
static bool arc[BOARD_ARC_COUNT];
static bool arc_selected[BOARD_ARC_COUNT];
static unsigned errors;
static atomic_bool fatal;
static board_error_pattern_t green;
volatile unsigned board_error_code;

void board_error_pattern_set(board_error_pattern_t *p,
                             board_error_t code, uint32_t now) {
  *p = (board_error_pattern_t){.code = code, .on = code == BOARD_ERROR_NONE,
      .gap = code != BOARD_ERROR_NONE,
      .deadline = now + PICODAC_ERROR_LED_GAP_MS * 1000u};
  // Begin with an OFF separator so the first ON pulse is visible from normal ON.
}
void board_error_pattern_tick(board_error_pattern_t *p, uint32_t now) {
  if (p->code == BOARD_ERROR_NONE || (int32_t)(now - p->deadline) < 0) return;
  if (p->gap) {
    p->gap = false; p->on = true; p->remaining = (unsigned)p->code - 1u;
    p->deadline = now + PICODAC_ERROR_LED_STEP_MS * 1000u;
  } else if (p->remaining) {
    p->on = !p->on; --p->remaining;
    p->deadline = now + PICODAC_ERROR_LED_STEP_MS * 1000u;
  } else {
    p->gap = true; p->on = false;
    p->deadline = now + PICODAC_ERROR_LED_GAP_MS * 1000u;
  }
}
void board_status_init(void) {
  errors = 0;
  atomic_store(&fatal, false);
  board_error_code = BOARD_ERROR_NONE;
  board_error_pattern_set(&green, BOARD_ERROR_NONE, time_us_32());
  gpio_init(BOARD_STATUS_LED); gpio_put(BOARD_STATUS_LED, true);
  gpio_set_dir(BOARD_STATUS_LED, GPIO_OUT);
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) {
    arc[i] = false;
    arc_selected[i] = false;
    gpio_init(yellow_pins[i]); gpio_put(yellow_pins[i], false);
    gpio_set_dir(yellow_pins[i], GPIO_OUT);
  }
}
void board_status_set_arc(unsigned port, bool enabled) {
  if (port < BOARD_ARC_COUNT) arc[port] = enabled;
}
void board_status_set_arc_selected(unsigned port, bool selected) {
  if (port < BOARD_ARC_COUNT) arc_selected[port] = selected;
}
void board_status_set_error(board_error_t error, bool active) {
  if (atomic_load(&fatal) || error == BOARD_ERROR_NONE) return;
  if (active) errors |= 1u << error;
  else errors &= ~(1u << error);
  // Show one stable code at a time; clearing one source preserves other errors.
  board_error_t code = BOARD_ERROR_NONE;
  const board_error_t priority[] = {BOARD_ERROR_PANIC, BOARD_ERROR_TF_FILE,
      BOARD_ERROR_USB_UNDERRUN, BOARD_ERROR_CEC_CONFLICT};
  for (unsigned i = 0; i < sizeof(priority) / sizeof(priority[0]); ++i) {
    if (errors & (1u << priority[i])) { code = priority[i]; break; }
  }
  if (code != green.code) {
    board_error_code = code;
    board_error_pattern_set(&green, code, time_us_32());
  }
}
void board_status_task(void) {
  if (atomic_load(&fatal)) return;
  uint32_t now = time_us_32();
  board_error_pattern_tick(&green, now);
  gpio_put(BOARD_STATUS_LED, green.on);
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i)
    gpio_put(yellow_pins[i], arc[i] ? arc_selected[i] :
             !!((now / 1000000u) & 1u));
}
void board_status_latch_panic(void) {
  atomic_store(&fatal, true);
  board_error_code = BOARD_ERROR_PANIC;
}

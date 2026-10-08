#include <assert.h>
#include <stdio.h>
#include "board.h"
#include "board_status.h"
static uint32_t now;
static bool pins[30];
uint32_t time_us_32(void) { return now; }
void gpio_init(unsigned pin) { assert(pin < 30); }
void gpio_set_dir(unsigned pin, int out) { (void)pin; assert(out == 1); }
void gpio_put(unsigned pin, int on) { pins[pin] = on; }
static void advance(uint32_t us) { now += us; board_status_task(); }
static void check_round(board_error_t code) {
  unsigned on = 0, off = 0;
  const uint32_t step = PICODAC_ERROR_LED_STEP_MS * 1000u;
  for (unsigned i = 0; i < (unsigned)code; ++i) {
    assert(pins[25] == !(i & 1u));
    if (pins[25]) ++on; else ++off;
    advance(step);
  }
  assert(on == (unsigned)code / 2 && off == on && !pins[25]);
  advance(PICODAC_ERROR_LED_GAP_MS * 1000u - 1);
  assert(!pins[25]); advance(1); assert(pins[25]);
}
int main(void) {
  board_status_init(); board_status_task();
  assert(pins[25] && !pins[BOARD_ARC1_LED]);
#if BOARD_ARC_COUNT == 1
  assert(BOARD_ARC1_LED == 15);
#else
  assert(BOARD_ARC1_LED == 17 && BOARD_ARC2_LED == 16);
#endif
  advance(1000000); assert(pins[BOARD_ARC1_LED] && pins[25]);
  board_status_set_arc(0, true);
  advance(1000000); assert(pins[BOARD_ARC1_LED] && pins[25]);
#if BOARD_ARC_COUNT > 1
  assert(!pins[BOARD_ARC2_LED]);
#endif
  const board_error_t codes[] = {BOARD_ERROR_PANIC, BOARD_ERROR_USB_UNDERRUN,
                                BOARD_ERROR_CEC_CONFLICT, BOARD_ERROR_TF_FILE};
  for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
    board_status_set_error(codes[i], true); board_status_task();
    assert(!pins[25] && board_error_code == (unsigned)codes[i]);
    advance(PICODAC_ERROR_LED_GAP_MS * 1000u - 1); assert(!pins[25]);
    // Reporting the same persistent error must not restart its phase timer.
    board_status_set_error(codes[i], true);
    advance(1); check_round(codes[i]); check_round(codes[i]);
    board_status_set_error(codes[i], false); board_status_task();
    assert(pins[25] && board_error_code == BOARD_ERROR_NONE);
  }
  board_status_set_error(BOARD_ERROR_CEC_CONFLICT, true);
  board_status_set_error(BOARD_ERROR_TF_FILE, true);
  assert(board_error_code == BOARD_ERROR_TF_FILE);
  board_status_set_error(BOARD_ERROR_TF_FILE, false);
  assert(board_error_code == BOARD_ERROR_CEC_CONFLICT);
  board_status_set_error(BOARD_ERROR_CEC_CONFLICT, false);
  now = UINT32_MAX - 99;
  board_status_set_error(BOARD_ERROR_TF_FILE, true);
  board_status_task(); advance(PICODAC_ERROR_LED_GAP_MS * 1000u);
  check_round(BOARD_ERROR_TF_FILE); // Includes 32-bit timer rollover.
  board_status_latch_panic();
  board_status_set_error(BOARD_ERROR_PANIC, false);
  board_status_set_error(BOARD_ERROR_TF_FILE, true);
  assert(board_error_code == BOARD_ERROR_PANIC);
  puts("PASS: green normal ON, exact error counts/gaps/recovery, yellow ARC, priority and timer wrap");
}

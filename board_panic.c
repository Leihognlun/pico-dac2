#include "board_status.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

// Preserve the format string for debugger inspection without depending on
// stdio locks, timers, the scheduler or PIO resources in a fatal context.
const char *volatile board_panic_message;
void __attribute__((noreturn)) board_panic(const char *fmt, ...) {
  (void)save_and_disable_interrupts();
  board_panic_message = fmt;
  board_status_latch_panic();
  gpio_init(BOARD_STATUS_LED);
  gpio_put(BOARD_STATUS_LED, false);
  gpio_set_dir(BOARD_STATUS_LED, GPIO_OUT);
  board_error_pattern_t pattern;
  board_error_pattern_set(&pattern, BOARD_ERROR_PANIC, time_us_32());
  for (;;) {
    board_error_pattern_tick(&pattern, time_us_32());
    gpio_put(BOARD_STATUS_LED, pattern.on);
    tight_loop_contents();
  }
}

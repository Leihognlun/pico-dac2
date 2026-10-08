#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef PICODAC_ERROR_LED_GAP_MS
#define PICODAC_ERROR_LED_GAP_MS 2000u
#endif
#ifndef PICODAC_ERROR_LED_STEP_MS
#define PICODAC_ERROR_LED_STEP_MS 250u
#endif

// Counts are ON + OFF phases, not complete flashes: 8 means 4 ON/OFF pairs.
typedef enum {
  BOARD_ERROR_NONE = 0,
  BOARD_ERROR_PANIC = 2,
  BOARD_ERROR_USB_UNDERRUN = 4,
  BOARD_ERROR_CEC_CONFLICT = 6,
  BOARD_ERROR_TF_FILE = 8,
} board_error_t;

typedef struct {
  uint32_t deadline;
  unsigned remaining;
  board_error_t code;
  bool on, gap;
} board_error_pattern_t;

void board_error_pattern_set(board_error_pattern_t *pattern,
                             board_error_t code, uint32_t now);
void board_error_pattern_tick(board_error_pattern_t *pattern, uint32_t now);
void board_status_init(void);
void board_status_set_arc(unsigned port, bool enabled);
void board_status_set_error(board_error_t error, bool active);
void board_status_task(void);
void board_status_latch_panic(void);
extern volatile unsigned board_error_code;

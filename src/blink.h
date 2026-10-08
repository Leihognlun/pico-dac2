#pragma once

#include <stdint.h>

#include "hardware/pio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the blink PIO program and state machine.
 *
 * @param pio The PIO instance to use (e.g., pio0, pio1).
 * @param led_pin The GPIO pin to control.
 */
void blink_init(PIO pio, uint led_pin);

/**
 * @brief Deinitializes the blink PIO program, releasing hardware resources.
 */
void blink_deinit();

/**
 * @brief Starts the blinking.
 */
void blink_start();

/**
 * @brief Stops the blinking and sets the LED pin to LOW.
 */
void blink_stop();

/**
 * @brief Sets the blinking period.
 *
 * @param period_us The total period of one blink cycle (on + off) in
 * microseconds. A value of 0 will effectively pause updates.
 */
void blink_set_period_us(uint32_t period_us);

/**
 * @brief Set LED on.
 */
void blink_led_on();

/**
 * @brief Set LED on.
 */
void blink_led_off();

/**
 * @brief Notifies the blink module that the system clock frequency has changed.
 *
 * This function recalculates and applies the correct PIO clock divider to
 * maintain the previously set blink period.
 */
void blink_notify_cpu_freq_change();

#ifdef __cplusplus
}
#endif

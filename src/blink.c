#include "blink.h"

#include "blink.pio.h"
#include "hardware/pio.h"
#include "pico/assert.h"

// Module-level state
static PIO pio_instance = NULL;
static uint sm = 0;
static uint pio_offset = 0;
static uint led_pin;
static uint32_t current_period_us = 500000;  // Default period
static bool is_blinking = false;  // Tracks the running state of the blinker

void blink_init(PIO pio, uint pin) {
  // Ensure we are not re-initializing without de-initializing first
  assert(pio_instance == NULL);

  pio_instance = pio;
  led_pin = pin;
  is_blinking = false;

  pio_offset = pio_add_program(pio_instance, &blink_program);
  sm = pio_claim_unused_sm(pio_instance, true);

  // Use the init function from the .pio file (which no longer sets clkdiv)
  blink_program_init(pio_instance, sm, pio_offset, led_pin);

  // Set the initial clock divider and period by calling the consolidated
  // function.
  blink_notify_cpu_freq_change();
}

void blink_deinit() {
  assert(pio_instance != NULL);
  assert(pio_sm_is_claimed(pio_instance, sm));

  // Use the cleanup helper from the .pio file to stop the SM and reset the pin
  blink_program_deinit(pio_instance, sm, led_pin);

  // Unclaim hardware resources
  pio_sm_unclaim(pio_instance, sm);
  pio_remove_program(pio_instance, &blink_program, pio_offset);

  pio_instance = NULL;
  is_blinking = false;
}

void blink_start() {
  assert(pio_instance != NULL);
  if (is_blinking) {
    return;  // Already started
  }

  // The pin should already be configured for PIO by blink_program_init.
  // We just need to re-enable the state machine.
  pio_sm_set_enabled(pio_instance, sm, true);
  is_blinking = true;
}

void blink_stop() {
  assert(pio_instance != NULL);
  if (!is_blinking) {
    return;  // Already stopped
  }

  // Disable the state machine
  pio_sm_set_enabled(pio_instance, sm, false);

  // To ensure the pin is left low, we can manually execute a `set pins, 0`
  // instruction. This is cleaner than changing the pin function to SIO. The
  // `set` instruction is configured by blink_program_init to only affect our
  // led_pin.
  pio_sm_exec(pio_instance, sm, pio_encode_set(pio_pins, 0));

  is_blinking = false;
}

void blink_set_period_us(uint32_t period_us) {
  assert(pio_instance != NULL);
  assert(pio_sm_is_claimed(pio_instance, sm));

  blink_start();

  current_period_us = period_us;

  pio_sm_clear_fifos(pio_instance, sm);

  if (period_us < 10) {  // A safe minimum
    pio_sm_put_blocking(pio_instance, sm, 0);
    return;
  }
  // The PIO program takes 2 * (x + 4) cycles for a full blink.
  // With our 1MHz clock, 1 cycle = 1us. So, period_us = 2 * (x + 4).
  // The value to send to the PIO state machine is x.
  uint32_t x = (period_us / 2) - 4;
  pio_sm_put_blocking(pio_instance, sm, x);
}

void blink_led_on() {
  assert(pio_instance != NULL);
  blink_stop();
  pio_sm_exec(pio_instance, sm, pio_encode_set(pio_pins, 1));
}

void blink_led_off() {
  assert(pio_instance != NULL);
  blink_stop();
  pio_sm_exec(pio_instance, sm, pio_encode_set(pio_pins, 0));
}

void blink_notify_cpu_freq_change() {
  assert(pio_instance != NULL);
  assert(pio_sm_is_claimed(pio_instance, sm));

  // Temporarily disable the state machine to update the clock divider
  if (is_blinking) {
    pio_sm_set_enabled(pio_instance, sm, false);
  }

  // Update the clock divider by calling the helper from the .pio file
  pio_sm_set_clkdiv(pio_instance, sm, blink_get_clkdiv());

  // Re-enable if it was running before
  if (is_blinking) {
    pio_sm_set_enabled(pio_instance, sm, true);
  }
}
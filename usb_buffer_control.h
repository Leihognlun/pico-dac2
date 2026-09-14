#pragma once

#include <stdint.h>
#include "hardware/structs/usb_dpram.h"
#include "pico/platform.h"

// RP2040 datasheet 4.1.2.7.1: publish metadata before transferring
// ownership to the USB controller (same ordering as the SDK's TinyUSB).
static inline void usb_buffer_control_publish(volatile uint32_t *reg,
                                               uint32_t value) {
  *reg = value & ~USB_BUF_CTRL_AVAIL;
  if (value & USB_BUF_CTRL_AVAIL) {
    busy_wait_at_least_cycles(12);
    *reg = value;
  }
}

// Preserve an already armed packet (including its length/data), but restart
// this endpoint direction at DATA0. Never reinitialize the whole interface:
// its opposite direction has an independent sequence maintained by the host.
static inline void usb_buffer_control_clear_halt(volatile uint32_t *reg,
                                                  uint8_t *next_pid) {
  uint32_t value = *reg & ~(USB_BUF_CTRL_STALL | USB_BUF_CTRL_DATA1_PID);
  // An armed DATA0 consumes the first PID; a completed/idle buffer does not.
  *next_pid = (value & USB_BUF_CTRL_AVAIL) ? 1 : 0;
  usb_buffer_control_publish(reg, value);
}

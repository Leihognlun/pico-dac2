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

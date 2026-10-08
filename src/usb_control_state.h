#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool read;
  bool need_zlp;
  uint16_t requested;
} usb_control_state_t;

static inline void usb_control_begin(usb_control_state_t *s, uint8_t request_type,
                                     uint16_t requested) {
  s->read = (request_type & 0x80) && requested;
  s->requested = requested;
  s->need_zlp = false;
}

static inline void usb_control_response(usb_control_state_t *s, uint16_t length) {
  s->need_zlp = s->read && length && length < s->requested && !(length % 64);
}

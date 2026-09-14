#include <assert.h>
#include <stdio.h>
#include "usb_control_state.h"

int main(void) {
  usb_control_state_t s;
  for (unsigned i = 0; i < 10; ++i) {
    usb_control_begin(&s, 0xA1, 6); // LED GET_REPORT
    usb_control_response(&s, 6);
    assert(s.read && !s.need_zlp); // data IN -> status OUT
    usb_control_begin(&s, 0x21, 6); // LED SET_REPORT
    usb_control_response(&s, 0);
    assert(!s.read && !s.need_zlp); // status IN -> idle, never arm OUT
  }
  usb_control_begin(&s, 0x80, 64);
  usb_control_response(&s, 64);
  assert(!s.need_zlp); // exact host request, no extra packet
  usb_control_begin(&s, 0x80, 255);
  usb_control_response(&s, 64);
  assert(s.need_zlp);
  usb_control_response(&s, 0);
  assert(!s.need_zlp); // only one terminating ZLP
  usb_control_begin(&s, 0x00, 0);
  usb_control_response(&s, 0);
  assert(!s.read && !s.need_zlp);
  puts("PASS: repeated control read/write status directions and terminating ZLP");
}

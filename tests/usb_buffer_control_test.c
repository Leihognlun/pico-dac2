#include <assert.h>
#include <stdio.h>
#include "usb_buffer_control.h"

static volatile uint32_t control;
static uint32_t expected_metadata;
static unsigned waits;

void busy_wait_at_least_cycles(uint32_t cycles) {
  assert(cycles >= 12);
  // The controller must see the new length/PID/FULL before ownership.
  assert(control == expected_metadata);
  assert(!(control & USB_BUF_CTRL_AVAIL));
  ++waits;
}

int main(void) {
  const uint32_t cases[] = {0, 4, 64 | 0x2000, 776, 4 | 0x8000};
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    expected_metadata = cases[i];
    control = 17; // stale completed transfer length
    unsigned before = waits;
    usb_buffer_control_publish(&control, cases[i] | USB_BUF_CTRL_AVAIL);
    assert(waits == before + 1);
    assert(control == (cases[i] | USB_BUF_CTRL_AVAIL));
  }
  unsigned before = waits;
  usb_buffer_control_publish(&control, 0);
  assert(control == 0 && waits == before);
  // Clear IN halt between LED writes: OUT must retain its DATA1 expectation.
  volatile uint32_t out_control = 2 | USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_DATA1_PID;
  uint8_t out_pid = 0, in_pid = 0;
  control = 17 | 0x8000 | USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_DATA1_PID | USB_BUF_CTRL_STALL;
  expected_metadata = 17 | 0x8000;
  usb_buffer_control_clear_halt(&control, &in_pid);
  assert(control == (expected_metadata | USB_BUF_CTRL_AVAIL) && in_pid == 1);
  assert(out_control == (2 | USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_DATA1_PID));
  assert(out_pid == 0);
  // OUT clear preserves the armed two-byte receive, using DATA0 then DATA1.
  control = out_control | USB_BUF_CTRL_STALL;
  expected_metadata = 2;
  usb_buffer_control_clear_halt(&control, &out_pid);
  assert(control == (2 | USB_BUF_CTRL_AVAIL) && out_pid == 1);
  // A completed packet must not be submitted again; the callback arms DATA0.
  control = 2 | USB_BUF_CTRL_DATA1_PID | USB_BUF_CTRL_STALL;
  before = waits;
  usb_buffer_control_clear_halt(&control, &out_pid);
  assert(control == 2 && out_pid == 0 && waits == before);
  puts("PASS: directional halt clear preserves pending packets and restarts DATA0");
  puts("PASS: USB buffer metadata published before AVAILABLE");
}

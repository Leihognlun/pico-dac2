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
  puts("PASS: USB buffer metadata published before AVAILABLE");
}

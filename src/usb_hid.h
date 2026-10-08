#pragma once
#include <stdbool.h>

#ifdef HID_ENABLE
void usb_hid_init();
void usb_hid_led_task(bool playing);
// Reset software key state without arming unconfigured USB endpoints.
void usb_hid_reset(void);
#endif

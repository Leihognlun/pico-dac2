#pragma once

#ifdef HID_ENABLE
void usb_hid_init();
// Reset software key state without arming unconfigured USB endpoints.
void usb_hid_reset(void);
#endif

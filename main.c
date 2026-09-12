#include <stdint.h>

#include "audio_device.h"
#include "blink.h"
#include "hardware/clocks.h"
#include "log.h"
#include "pico/stdlib.h"
#include "usb.h"
#include "usb_audio.h"
#include "usb_hid.h"

int main() {
  // 1584 MHz VCO / 6 / 2 = 132 MHz, within the RP2040 PLL limits.
  // The audio PIO backends derive their dividers from clk_sys.
  set_sys_clock_pll(1584 * MHZ, 6, 2);

  stdio_init_all();

  blink_init(pio1, PICO_DEFAULT_LED_PIN);
  audio_device_init();
  usb_device_init();
  usb_audio_init();
#ifdef HID_ENABLE
  usb_hid_init();
#endif

  LOG_INFO("Booted");

  while (true) {
    usb_device_task();

    audio_device_task();
  }
}

#include <stdint.h>
#include "board.h"
#if PICODAC_BOARD_C2 && !PICODAC_INPUT_SD
#include "sd_controls.h"
#endif

#include "audio_device.h"
#include "board_status.h"
#include "cec_arc.h"
#include "ddc_edid.h"
#include "hardware/clocks.h"
#include "log.h"
#include "pico/stdlib.h"
#include "usb.h"
#include "usb_audio.h"
#include "usb_hid.h"
#if PICODAC_INPUT_SD
#include "sd_eac3_player.h"
#endif

int main() {
#if PICODAC_INPUT_SD
  // 120 MHz / 192 kHz = 625 divider units: exact average carrier frequency.
  // A local file has no USB feedback loop to compensate clock-rate error.
  set_sys_clock_pll(1440 * MHZ, 6, 2);
#else
  // 1584 MHz VCO / 6 / 2 = 132 MHz, within the RP2040 PLL limits.
  // The audio PIO backends derive their dividers from clk_sys.
  set_sys_clock_pll(1584 * MHZ, 6, 2);
#endif

  stdio_init_all();

  board_status_init();
  ddc_edid_init();
  cec_arc_init();
#if PICODAC_INPUT_SD
  sd_eac3_player_run();
#else
  audio_device_init();
  usb_device_init();
  usb_audio_init();
#ifdef HID_ENABLE
  usb_hid_init();
#endif
#if PICODAC_BOARD_C2
  sd_controls_init();
#endif

  LOG_INFO("Booted");

  while (true) {
    usb_device_task();

    audio_device_task();
#if PICODAC_BOARD_C2
    for (unsigned port = 0; port < BOARD_ARC_COUNT; ++port)
      cec_arc_set_port_playing(port, audio_device_stream_active() &&
                                    sd_controls_port_playing(port));
#else
    cec_arc_set_port_playing(0, audio_device_stream_active());
    board_status_set_arc_selected(0, audio_device_stream_active());
#endif
#if HID_ENABLE && !PICODAC_BOARD_C2
    usb_hid_led_task(audio_device_stream_active());
#endif
#if PICODAC_BOARD_C2
    sd_controls_task();
#endif
    cec_arc_task();
    board_status_task();
  }
#endif
}

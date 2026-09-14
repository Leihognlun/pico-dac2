#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "usb_descriptor.h"

int main(void) {
  const uint8_t *base = (const void *)&configuration_descriptor;
  size_t size = sizeof(configuration_descriptor);
  assert(configuration_descriptor.config.wTotalLength == size);
  assert(configuration_descriptor.ac.cs_ac_output_terminal.wTerminalType ==
         (PICODAC_OUTPUT_SPDIF ? 0x0605 : 0x0304));
  int alt = -1;
  unsigned seen = 0, formats = 0, endpoints = 0;
  for (size_t offset = 0; offset < size;) {
    const uint8_t *d = base + offset;
    assert(d[0] >= 2 && offset + d[0] <= size);
    if (d[1] == USB_DT_INTERFACE) {
      alt = d[2] == INTERFACE_AUDIO_STREAM ? d[3] : -1;
      if (alt >= 0) { assert(alt == (int)seen++); assert(d[4] == (alt ? 2 : 0)); }
    } else if (alt > 0 && d[1] == USB_DT_CS_INTERFACE) {
      if (d[2] == 1) {
        assert(d[0] == 16 && d[3] == AUDIO_CONTROL_ID_INPUT);
        assert(d[5] == (alt >= 4 ? 3 : 1));
        uint32_t mask = d[6] | (d[7] << 8) | (d[8] << 16) | ((uint32_t)d[9] << 24);
        const uint32_t expected[] = {1, 1, 1, 1, 1, 128, 256, 512};
        assert(mask == expected[alt]);
        assert(d[10] == 2);
      } else if (d[2] == 2) {
        assert(d[0] == 6 && d[3] == (alt >= 4 ? 3 : 1));
        assert(d[4] == (alt == 2 || alt == 3 ? 4 : 2));
        assert(d[5] == (alt == 2 ? 24 : alt == 3 ? 32 : 16));
        ++formats;
      }
    } else if (alt > 0 && d[1] == USB_DT_ENDPOINT) {
      assert(d[0] == 7);
      unsigned max_packet = d[4] | (d[5] << 8);
      if (d[2] == EP_AUDIO_STREAM_OUT) {
        assert(d[3] == 5 && max_packet == (alt >= 4 ? 388 : 776));
      } else {
        assert(d[2] == EP_AUDIO_FEEDBACK_IN && d[3] == 0x11 && max_packet == 4);
      }
      ++endpoints;
    }
    offset += d[0];
  }
  assert(seen == (PICODAC_OUTPUT_SPDIF ? 8 : 4));
  assert(formats == seen - 1 && endpoints == formats * 2);
  assert(device_descriptor.bcdDevice == 0x0103);
  puts("PASS: USB descriptor lengths, topology, formats and endpoints");
}

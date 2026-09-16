#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "usb_descriptor.h"

int main(void) {
#if HID_ENABLE
  unsigned id = 0, bits = 0, count = 0, page = 0, usage = 0;
  unsigned input_bits[5] = {0}, output_bits[5] = {0}, feature_bits[5] = {0}, media_usages = 0;
  for (size_t offset = 0; offset < sizeof(report_descriptor);) {
    uint8_t tag = report_descriptor[offset++];
    unsigned length = (tag & 3) == 3 ? 4 : tag & 3;
    assert(offset + length <= sizeof(report_descriptor));
    uint32_t value = 0;
    for (unsigned i = 0; i < length; ++i)
      value |= (uint32_t)report_descriptor[offset++] << (8 * i);
    switch (tag & 0xFC) {
      case 0x04: page = value; break;
      case 0x08:
        usage = value;
        if (page == 0x0C && id == HID_REPORT_MEDIA) {
          if (usage == 0xCD) media_usages |= 1;
          if (usage == 0xE9) media_usages |= 2;
          if (usage == 0xEA) media_usages |= 4;
        }
        break;
      case 0x84: id = value; assert(id > 0 && id < 5); break;
      case 0x74: bits = value; break;
      case 0x94: count = value; break;
      case 0x80: input_bits[id] += bits * count; break;
      case 0x90: output_bits[id] += bits * count; break;
      case 0xB0: feature_bits[id] += bits * count; break;
    }
  }
  assert(input_bits[1] == 128 && input_bits[2] == 8 && input_bits[3] == 0);
  assert(output_bits[1] == 0 && output_bits[2] == 0 && output_bits[3] == 8);
  assert(media_usages == 7);
  assert(feature_bits[4] == 40);
  assert(configuration_descriptor.hid.hid_in_descriptor.wMaxPacketSize == 17);
  assert(configuration_descriptor.hid.hid_out_descriptor.wMaxPacketSize == 2);
  assert(configuration_descriptor.hid.hid_in_descriptor.bInterval == 10);
#endif
  const uint8_t *base = (const void *)&configuration_descriptor;
  size_t size = sizeof(configuration_descriptor);
  assert(configuration_descriptor.config.wTotalLength == size);
  assert(configuration_descriptor.ac.cs_ac_output_terminal.wTerminalType ==
         (PICODAC_OUTPUT_SPDIF ? 0x0605 : 0x0304));
  int alt = -1;
  unsigned seen = 0, formats = 0, endpoints = 0, clocks = 0;
  for (size_t offset = 0; offset < size;) {
    const uint8_t *d = base + offset;
    assert(d[0] >= 2 && offset + d[0] <= size);
    if (d[1] == USB_DT_INTERFACE) {
      alt = d[2] == INTERFACE_AUDIO_STREAM ? d[3] : -1;
      if (alt >= 0) { assert(alt == (int)seen++); assert(d[4] == (alt ? 2 : 0)); }
    } else if (alt == -1 && d[1] == USB_DT_CS_INTERFACE && d[2] == 0x0a) {
      ++clocks;
      assert(d[3] == AUDIO_CONTROL_ID_CLOCK);
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
        assert(d[3] == 5 && max_packet == (alt >= 4 ? 772 : alt == 1 ? 388 : 776));
      } else {
        assert(d[2] == EP_AUDIO_FEEDBACK_IN && d[3] == 0x11 && max_packet == 4);
      }
      ++endpoints;
    }
    offset += d[0];
  }
  assert(seen == (PICODAC_OUTPUT_SPDIF ? 8 : 4));
  assert(formats == seen - 1 && endpoints == formats * 2);
  assert(configuration_descriptor.ac.cs_ac_interface.wTotalLength ==
         sizeof(struct ac) - sizeof(usb_standard_ac_interface_descriptor));
  assert(clocks == 1);
  assert(configuration_descriptor.ac.cs_ac_input_terminal.bCSourceID == AUDIO_CONTROL_ID_CLOCK);
  assert(configuration_descriptor.ac.cs_ac_output_terminal.bCSourceID == AUDIO_CONTROL_ID_CLOCK);
  assert(device_descriptor.bcdDevice == 0x010a);
  puts("PASS: USB descriptor lengths, topology, formats and endpoints");
}

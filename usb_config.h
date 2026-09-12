#pragma once

#ifndef VENDOR_ID
#define VENDOR_ID 0xcafe
#endif

#ifndef PRODUCT_ID
#define PRODUCT_ID 0xbabe
#endif

enum INTERFACE_ID {
  INTERFACE_AUDIO_CONTROL = 0,
  INTERFACE_AUDIO_STREAM,
#if HID_ENABLE
  INTERFACE_HID,
#endif
  INTERFACE_NUM,
};

// UAC
#define AUDIO_INTERFACE_NUM \
  ((INTERFACE_AUDIO_STREAM - INTERFACE_AUDIO_CONTROL) + 1)

#define AUDIO_MAX_PACKET_SIZE ((96 + 1) * 4 * 2)

// UAC2 Audio Data Formats, Table A-4. Type III transports IEC 61937
// in two 16-bit subslots; these bits are NOT IEC 61937 burst type codes.
#define AUDIO_FORMAT_III_AC3 (1u << 0)
#define AUDIO_FORMAT_III_DTS_I (1u << 7)
#define AUDIO_FORMAT_III_DTS_II (1u << 8)
#define AUDIO_FORMAT_III_DTS_III (1u << 9)
#define AUDIO_ALT_AC3 4
#define AUDIO_ALT_DTS_I 5
#define AUDIO_ALT_DTS_II 6
#define AUDIO_ALT_DTS_III 7
#if PICODAC_OUTPUT_SPDIF
#define AUDIO_ALT_MAX AUDIO_ALT_DTS_III
#else
#define AUDIO_ALT_MAX 3
#endif
#define AUDIO_IEC61937_MAX_PACKET_SIZE ((96 + 1) * 2 * 2)

#define EP_AUDIO_STREAM_OUT 0x01
#define EP_AUDIO_FEEDBACK_IN 0x81

#define EP_HID_OUT 0x02
#define EP_HID_IN 0x82

#define AUDIO_CONTROL_ID_INPUT 0x01
#define AUDIO_CONTROL_ID_FEATURE_UNIT 0x02
#define AUDIO_CONTROL_ID_OUTPUT 0x03
#define AUDIO_CONTROL_ID_CLOCK 0x04

#define HID_INTERVAL_MS 200

#pragma once

#include "usb_common.h"
#include "usb_config.h"
// TODO set string descriptor index

// HID
#if HID_ENABLE
static const uint8_t report_descriptor[] = {
    0x06, 0x00, 0xFF,  // USAGE_PAGE (Vendor Defined)
    0x09, 0x01,        // USAGE (0x01)
    // Collection
    0xA1, 0x01,  // COLLECTION (Application)
    0x85, HID_REPORT_DIAGNOSTICS,
    // Input Report (16byte)
    0x09, 0x02,        // USAGE (0x02)
    0x15, 0x00,        // LOGICAL_MINIMUM(0)
    0x26, 0xFF, 0x00,  // LOGICAL_MAXIMUM(255)
    0x75, 0x08,        // REPORT_SIZE(8bit)
    0x95, 0x10,        // REPORT_COUNT(16byte)
    0x81, 0x02,        // INPUT(Data,Var,Abs)

    // Output Report: LED mask, bits 0..2 = GPIO 9, 12, 26.
    0x85, HID_REPORT_LEDS,
    0x09, 0x03,        // USAGE (0x03)
    0x15, 0x00,        // LOGICAL_MINIMUM(0)
    0x26, 0xFF, 0x00,  // LOGICAL_MAXIMUM(255)
    0x75, 0x08,        // REPORT_SIZE(8bit)
    0x95, 0x01,        // REPORT_COUNT(1byte)
    0x91, 0x02,        // OUTPUT(Data,Var,Abs)
    // EP0 LED control/readback: mask + little-endian accepted-command count.
    0x85, HID_REPORT_LED_FEATURE,
    0x09, 0x04,
    0x95, 0x05,
    0xB1, 0x02,        // FEATURE(Data,Var,Abs)

    // End Collection
    0xC0,  // END_COLLECTION
    0x05, 0x0C,        // Consumer usage page
    0x09, 0x01,        // Consumer Control
    0xA1, 0x01,
    0x85, HID_REPORT_MEDIA,
    0x15, 0x00, 0x25, 0x01,
    0x09, 0xCD,        // Play/Pause
    0x09, 0xE9,        // Volume Increment
    0x09, 0xEA,        // Volume Decrement
    0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x75, 0x05, 0x95, 0x01, 0x81, 0x03, // Padding
    0xC0,
};
#endif

static const struct usb_device_descriptor device_descriptor = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,         // USB 2.0 device
    .bDeviceClass = 0xEF,     // Miscellaneous Device Class
    .bDeviceSubClass = 0x02,  // Common Class
    .bDeviceProtocol = 0x01,  // Interface Association Descriptor
    .bMaxPacketSize0 = 64,    // Max packet size for ep0
    .idVendor = VENDOR_ID,    // Your vendor id
    .idProduct = PRODUCT_ID,  // Your product ID
    .bcdDevice = PICODAC_EAC3_PASSTHROUGH ? 0x0111 : 0x0108,
    .iManufacturer = 0,       // Manufacturer string index
    .iProduct = 0,            // Product string index
    .iSerialNumber = 0,       // No serial number
    .bNumConfigurations = 1   // One configuration
};

#if PICODAC_OUTPUT_SPDIF
// Type III uses the same six-byte format layout as Type I, with a
// different bFormatType. A Type III alternate may declare multiple codecs.
#define USB_CARRIER_ALT(alt, formats, type, terminal, packet_size) { \
    .as_interface = { \
        .bLength = sizeof(usb_standard_as_interface_descriptor), \
        .bDescriptorType = USB_DT_INTERFACE, \
        .bInterfaceNumber = INTERFACE_AUDIO_STREAM, \
        .bAlternateSetting = (alt), .bNumEndpoint = 2, \
        .bInterfaceClass = 1, .bInterfaceSubClass = 2, \
        .bInterfaceProtocol = 0x20, \
    }, \
    .cs_as_interface = { \
        .bLength = sizeof(struct usb_class_specific_as_interface_descriptor), \
        .bDescriptorType = USB_DT_CS_INTERFACE, .bDescriptorSubtype = 1, \
        .bTerminalLink = (terminal), .bFormatType = (type), \
        .bmFormats = (formats), .bNrChannels = 2, .bmChannelConfig = 3, \
    }, \
    .cs_as_format_type = { \
        .bLength = sizeof(struct usb_class_specific_as_type_i_format_descriptor), \
        .bDescriptorType = USB_DT_CS_INTERFACE, .bDescriptorSubtype = 2, \
        .bFormatType = (type), .bSubslotSize = 2, .bBitResolution = 16, \
    }, \
    .as_audio_data_endpoint = { \
        .bLength = sizeof(struct usb_standard_as_isochronous_audio_data_endpoint_descriptor), \
        .bDescriptorType = USB_DT_ENDPOINT, .bEndpointAddress = EP_AUDIO_STREAM_OUT, \
        .bmAttributes = 0x05, .wMaxPacketSize = (packet_size), \
        .bInterval = 1, \
    }, \
    .cs_as_audio_data_endpoint = { \
        .bLength = sizeof(struct usb_class_specific_as_isochronous_audio_data_endpoint_descriptor), \
        .bDescriptorType = USB_DT_CS_ENDPOINT, .bDescriptorSubtype = 1, \
    }, \
    .as_isochronous_feedback_endpoint = { \
        .bLength = sizeof(struct usb_standard_as_isochronous_feedback_endpoint_descriptor), \
        .bDescriptorType = USB_DT_ENDPOINT, .bEndpointAddress = EP_AUDIO_FEEDBACK_IN, \
        .bmAttributes = 0x11, .wMaxPacketSize = 4, .bInterval = 1, \
    }, \
}
#define USB_IEC61937_ALT(alt, formats) USB_CARRIER_ALT(alt, formats, 3, \
    AUDIO_CONTROL_ID_INPUT, AUDIO_IEC61937_MAX_PACKET_SIZE)
#endif

struct configuration_descriptor {
  struct usb_configuration_descriptor config;
  struct usb_interface_association_descriptor iad;
  struct ac {
    usb_standard_ac_interface_descriptor ac_interface;
    struct usb_class_specific_ac_interface_header_descriptor cs_ac_interface;
    struct usb_class_specific_ac_clock_source_descriptor cs_ac_clock_source;
    struct usb_class_specific_ac_input_terminal_descriptor cs_ac_input_terminal;
    struct usb_class_specific_ac_output_terminal_descriptor
        cs_ac_output_terminal;
    struct usb_class_specific_ac_feature_unit_descriptor_stereo
        cs_ac_feature_unit;
  } __attribute__((packed)) ac;
  struct as {
    struct as_alt0 {
      usb_standard_as_interface_descriptor as_interface;
    } __attribute__((packed)) as_alt0;
    struct as_alt {
      usb_standard_as_interface_descriptor as_interface;
      struct usb_class_specific_as_interface_descriptor cs_as_interface;
      struct usb_class_specific_as_type_i_format_descriptor cs_as_format_type;
      struct usb_standard_as_isochronous_audio_data_endpoint_descriptor
          as_audio_data_endpoint;
      struct usb_class_specific_as_isochronous_audio_data_endpoint_descriptor
          cs_as_audio_data_endpoint;
      struct usb_standard_as_isochronous_feedback_endpoint_descriptor
          as_isochronous_feedback_endpoint;
    } __attribute__((packed)) as_alt1;
    struct as_alt as_alt2;
    struct as_alt as_alt3;
#if PICODAC_OUTPUT_SPDIF
    struct as_alt as_ac3;
    struct as_alt as_dts_i;
    struct as_alt as_dts_ii;
    struct as_alt as_dts_iii;
#if PICODAC_EAC3_PASSTHROUGH
    struct as_alt as_eac3;
#endif
#endif
  } __attribute__((packed)) as;
#if HID_ENABLE
  struct hid {
    struct usb_interface_descriptor_t hid_interface;
    struct usb_hid_descriptor_t hid_descriptor;
    struct usb_endpoint_descriptor_t hid_in_descriptor;
    struct usb_endpoint_descriptor_t hid_out_descriptor;
  } __attribute__((packed)) hid;
#endif
} __attribute__((packed)) configuration_descriptor = {
    .config =
        {
            .bLength = sizeof(struct usb_configuration_descriptor),
            .bDescriptorType = USB_DT_CONFIG,
            .wTotalLength = sizeof(configuration_descriptor),
            .bNumInterfaces = INTERFACE_NUM,
            .bConfigurationValue = 1,
            .iConfiguration = 0,
            .bmAttributes = 0x80,
            .bMaxPower = 100 >> 1,  // 100mA
        },
    .iad =
        {
            .bLength = sizeof(struct usb_interface_association_descriptor),

            .bDescriptorType = USB_DT_IAD,
            .bFirstInterface = INTERFACE_AUDIO_CONTROL,
            .bInterfaceCount = AUDIO_INTERFACE_NUM,
            .bFunctionClass = 0x01,     // USB Audio Class
            .bFunctionSubClass = 0x00,  // Subclass Undefined
            .bFunctionProtocol = 0x20,  // UAC 2.0
            .iFunction = 0,             // TODO string index
        },
    .ac =
        {
            .ac_interface =
                {
                    .bLength = sizeof(usb_standard_ac_interface_descriptor),
                    .bDescriptorType = USB_DT_INTERFACE,
                    .bInterfaceNumber = INTERFACE_AUDIO_CONTROL,
                    .bAlternateSetting = 0,      // Alt 0
                    .bNumEndpoint = 0,           // No endpoints
                    .bInterfaceClass = 0x01,     // AUDIO
                    .bInterfaceSubClass = 0x01,  // AUDIO_CONTROL
                    .bInterfaceProtocol = 0x20,  // UAC 2.0
                    .iInterface = 0,             // TODO string index
                },
            .cs_ac_interface =
                {
                    .bLength = sizeof(
                        struct
                        usb_class_specific_ac_interface_header_descriptor),
                    .bDescriptorType = USB_DT_CS_INTERFACE,
                    .bDescriptorSubtype = 0x01,  // HEADER
                    .bcdACD = 0x0200,            // ADC version
                    .bCategory = PICODAC_OUTPUT_SPDIF ? 0x06 : 0x01,
                    // Digital converter / desktop speaker
                    .wTotalLength =
                        sizeof(struct ac) -
                        sizeof(usb_standard_ac_interface_descriptor),
                    .bmControls = 0x00,
                },
            .cs_ac_clock_source =
                {
                    .bLength = sizeof(
                        struct usb_class_specific_ac_clock_source_descriptor),
                    .bDescriptorType = USB_DT_CS_INTERFACE,
                    .bDescriptorSubtype = 0x0A,          // CLOCK_SOURCE
                    .bClockID = AUDIO_CONTROL_ID_CLOCK,  // Clock Source ID
                    // TODO
                    .bmAttributes = 0x03,    // Internal Programmable Clock
                    .bmControls = 0x03,      // RW (Sampling Freq)
                    .bAssocTerminal = 0x01,  // Assoc (Input Terminal)
                    .iClockSource = 0,
                },
            .cs_ac_input_terminal =
                {
                    .bLength = sizeof(
                        struct usb_class_specific_ac_input_terminal_descriptor),
                    .bDescriptorType = USB_DT_CS_INTERFACE,
                    .bDescriptorSubtype = 0x02,             // INPUT_TERMIKNAL
                    .bTerminalID = AUDIO_CONTROL_ID_INPUT,  // Input Terminal ID
                    .wTerminalType = 0x0101,                // USB STREAMING
                    .bAssocTerminal = 0x00,                 // Assoc (None)
                    .bCSourceID = AUDIO_CONTROL_ID_CLOCK,   // Clock Source ID
                    .bNrChannels = 0x02,                    // Channels
                    .bmChannelConfig = 0x03,                // Front LR
                    .iChannelNames = 0,
                    .bmControls = 0,
                    .iTerminal = 0,
                },
            .cs_ac_output_terminal =
                {
                    .bLength = sizeof(
                        struct
                        usb_class_specific_ac_output_terminal_descriptor),
                    .bDescriptorType = USB_DT_CS_INTERFACE,
                    .bDescriptorSubtype = 0x03,  // OUTPUT_TERMINAL
                    .bTerminalID = AUDIO_CONTROL_ID_OUTPUT,
                    .wTerminalType = PICODAC_OUTPUT_SPDIF ? 0x0605 : 0x0304,
                    // External SPDIF / desktop speaker
                    .bAssocTerminal = 0x001,  // Assoc (Input)
                    .bSourceID = AUDIO_CONTROL_ID_FEATURE_UNIT,
                    .bCSourceID = AUDIO_CONTROL_ID_CLOCK,  // Clock Source
                    .bmControls = 0,
                    .iTerminal = 0,
                },
            .cs_ac_feature_unit =
                {
                    .bLength = sizeof(
                        struct
                        usb_class_specific_ac_feature_unit_descriptor_stereo),
                    .bDescriptorType = USB_DT_CS_INTERFACE,
                    .bDescriptorSubtype = 0x06,  // FEATURE_UNIT
                    .bUnitID = AUDIO_CONTROL_ID_FEATURE_UNIT,
                    .bSourceID = AUDIO_CONTROL_ID_INPUT,
                    .bmaControls =
                        {
                            0b1111,  // Master (Mute, Vol)
                            0b1111,  // Left (Mute, Vol)
                            0b1111,  // Right (Mute, Vol)
                        },
                    .iFeature = 0,
                },
        },
    .as =
        {
            .as_alt0 =
                {
                    .as_interface =
                        {
                            .bLength =
                                sizeof(usb_standard_as_interface_descriptor),
                            .bDescriptorType = USB_DT_INTERFACE,
                            .bInterfaceNumber = INTERFACE_AUDIO_STREAM,
                            .bAlternateSetting = 0,      // Alt 0
                            .bNumEndpoint = 0,           // No endpoints
                            .bInterfaceClass = 0x01,     // AUDIO
                            .bInterfaceSubClass = 0x02,  // AUDIO_STREAMING
                            .bInterfaceProtocol = 0x20,  // UAC 2.0
                            .iInterface = 0,             // TODO string index
                        },
                },
            .as_alt1 =
                {
                    .as_interface =
                        {
                            .bLength =
                                sizeof(usb_standard_as_interface_descriptor),
                            .bDescriptorType = USB_DT_INTERFACE,
                            .bInterfaceNumber = INTERFACE_AUDIO_STREAM,
                            .bAlternateSetting = 1,      // Alt 1 (16bit)
                            .bNumEndpoint = 2,           // AS and Feedback
                            .bInterfaceClass = 0x01,     // AUDIO
                            .bInterfaceSubClass = 0x02,  // AUDIO_STREAMING
                            .bInterfaceProtocol = 0x20,  // UAC 2.0
                            .iInterface = 0,             // TODO string index
                        },
                    .cs_as_interface =
                        {
                            .bLength = sizeof(
                                struct
                                usb_class_specific_as_interface_descriptor),
                            .bDescriptorType = USB_DT_CS_INTERFACE,
                            .bDescriptorSubtype = 0x01,  // AS_GENERAL
                            .bTerminalLink = AUDIO_CONTROL_ID_INPUT,
                            .bmControls = 0,          // None
                            .bFormatType = 0x01,      // Type I
                            .bmFormats = 0x01,        // PCM
                            .bNrChannels = 2,         // Channels
                            .bmChannelConfig = 0x03,  // Front LR
                            .iChannelNames = 0,       // TODO string index
                        },
                    .cs_as_format_type =
                        {
                            .bLength = sizeof(
                                struct
                                usb_class_specific_as_type_i_format_descriptor),
                            .bDescriptorType = USB_DT_CS_INTERFACE,
                            .bDescriptorSubtype = 0x02,  // FORMAT_TYPE
                            .bFormatType = 0x01,         // TYPE_I
                            .bSubslotSize = 2,           // bytes per sample
                            .bBitResolution = 16,        // bits per sample
                        },
                    .as_audio_data_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_standard_as_isochronous_audio_data_endpoint_descriptor),
                            .bDescriptorType = USB_DT_ENDPOINT,
                            .bEndpointAddress = EP_AUDIO_STREAM_OUT,
                            .bmAttributes = 0b0101,  // asyncrhnous(0b100) and
                                                     // ishochronous(0b01)
                            .wMaxPacketSize = AUDIO_MAX_PACKET_SIZE,
                            .bInterval = 0x01,  // 1ms
                        },
                    .cs_as_audio_data_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_class_specific_as_isochronous_audio_data_endpoint_descriptor),
                            .bDescriptorType = USB_DT_CS_ENDPOINT,
                            .bDescriptorSubtype = 0x01,  // EP_GENERAL
                            .bmAttributes = 0,           // Non Max Packet Only
                            .bmControls = 0x00,          // None
                            .bLockDelayUnits = 0,
                            .wLockDelay = 0,
                        },
                    .as_isochronous_feedback_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_standard_as_isochronous_feedback_endpoint_descriptor),
                            .bDescriptorType = USB_DT_ENDPOINT,
                            .bEndpointAddress = EP_AUDIO_FEEDBACK_IN,
                            .bmAttributes = 0x11,
                            .wMaxPacketSize = 0x04,
                            .bInterval = 1,
                        },
                },
            .as_alt2 =
                {
                    .as_interface =
                        {
                            .bLength =
                                sizeof(usb_standard_as_interface_descriptor),
                            .bDescriptorType = USB_DT_INTERFACE,
                            .bInterfaceNumber = INTERFACE_AUDIO_STREAM,
                            .bAlternateSetting = 2,      // Alt 2 (24bit)
                            .bNumEndpoint = 2,           // AS and Feedback
                            .bInterfaceClass = 0x01,     // AUDIO
                            .bInterfaceSubClass = 0x02,  // AUDIO_STREAMING
                            .bInterfaceProtocol = 0x20,  // UAC 2.0
                            .iInterface = 0,             // TODO string index
                        },
                    .cs_as_interface =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_class_specific_as_interface_descriptor),
                            .bDescriptorType = USB_DT_CS_INTERFACE,
                            .bDescriptorSubtype = 0x01,  // AS_GENERAL
                            .bTerminalLink = AUDIO_CONTROL_ID_INPUT,
                            .bmControls = 0,          // None
                            .bFormatType = 0x01,      // Type I
                            .bmFormats = 0x01,        // PCM
                            .bNrChannels = 2,         // Channels
                            .bmChannelConfig = 0x03,  // Front LR
                            .iChannelNames = 0,
                        },
                    .cs_as_format_type =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_class_specific_as_type_i_format_descriptor),
                            .bDescriptorType = USB_DT_CS_INTERFACE,
                            .bDescriptorSubtype = 0x02,  // FORMAT_TYPE
                            .bFormatType = 0x01,         // TYPE_I
                            .bSubslotSize = 4,           // bytes per sample
                            .bBitResolution = 24,        // bits per sample
                        },
                    .as_audio_data_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_standard_as_isochronous_audio_data_endpoint_descriptor),
                            .bDescriptorType = USB_DT_ENDPOINT,
                            .bEndpointAddress = EP_AUDIO_STREAM_OUT,
                            .bmAttributes = 0b0101,  // asyncrhnous(0b100) and
                                                     // ishochronous(0b01)
                            .wMaxPacketSize = AUDIO_MAX_PACKET_SIZE,
                            .bInterval = 0x01,  // 1ms
                        },
                    .cs_as_audio_data_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_class_specific_as_isochronous_audio_data_endpoint_descriptor),
                            .bDescriptorType = USB_DT_CS_ENDPOINT,
                            .bDescriptorSubtype = 0x01,  // EP_GENERAL
                            .bmAttributes = 0,           // Non Max Packet Only
                            .bmControls = 0x00,          // None
                            .bLockDelayUnits = 0,
                            .wLockDelay = 0,
                        },
                    .as_isochronous_feedback_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_standard_as_isochronous_feedback_endpoint_descriptor),
                            .bDescriptorType = USB_DT_ENDPOINT,
                            .bEndpointAddress = EP_AUDIO_FEEDBACK_IN,
                            .bmAttributes = 0x11,
                            .wMaxPacketSize = 0x04,
                            .bInterval = 1,
                        },
                },
            .as_alt3 =
                {
                    .as_interface =
                        {
                            .bLength =
                                sizeof(usb_standard_as_interface_descriptor),
                            .bDescriptorType = USB_DT_INTERFACE,
                            .bInterfaceNumber = INTERFACE_AUDIO_STREAM,
                            .bAlternateSetting = 3,      // Alt 3 (32bit)
                            .bNumEndpoint = 2,           // AS and Feedback
                            .bInterfaceClass = 0x01,     // AUDIO
                            .bInterfaceSubClass = 0x02,  // AUDIO_STREAMING
                            .bInterfaceProtocol = 0x20,  // UAC 2.0
                            .iInterface = 0,             // TODO string index
                        },
                    .cs_as_interface =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_class_specific_as_interface_descriptor),
                            .bDescriptorType = USB_DT_CS_INTERFACE,
                            .bDescriptorSubtype = 0x01,  // AS_GENERAL
                            .bTerminalLink = AUDIO_CONTROL_ID_INPUT,
                            .bmControls = 0,          // None
                            .bFormatType = 0x01,      // Type I
                            .bmFormats = 0x01,        // PCM
                            .bNrChannels = 2,         // Channels
                            .bmChannelConfig = 0x03,  // Front LR
                            .iChannelNames = 0,
                        },
                    .cs_as_format_type =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_class_specific_as_type_i_format_descriptor),
                            .bDescriptorType = USB_DT_CS_INTERFACE,
                            .bDescriptorSubtype = 0x02,  // FORMAT_TYPE
                            .bFormatType = 0x01,         // TYPE_I
                            .bSubslotSize = 4,           // bytes per sample
                            .bBitResolution = 32,        // bits per sample
                        },
                    .as_audio_data_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_standard_as_isochronous_audio_data_endpoint_descriptor),
                            .bDescriptorType = USB_DT_ENDPOINT,
                            .bEndpointAddress = EP_AUDIO_STREAM_OUT,
                            .bmAttributes = 0b0101,  // asyncrhnous(0b100) and
                                                     // ishochronous(0b01)
                            .wMaxPacketSize = AUDIO_MAX_PACKET_SIZE,
                            .bInterval = 0x01,  // 1ms
                        },
                    .cs_as_audio_data_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_class_specific_as_isochronous_audio_data_endpoint_descriptor),
                            .bDescriptorType = USB_DT_CS_ENDPOINT,
                            .bDescriptorSubtype = 0x01,  // EP_GENERAL
                            .bmAttributes = 0,           // Non Max Packet Only
                            .bmControls = 0x00,          // None
                            .bLockDelayUnits = 0,
                            .wLockDelay = 0,
                        },
                    .as_isochronous_feedback_endpoint =
                        {
                            .bLength =
                                sizeof(
                                    struct
                                    usb_standard_as_isochronous_feedback_endpoint_descriptor),
                            .bDescriptorType = USB_DT_ENDPOINT,
                            .bEndpointAddress = EP_AUDIO_FEEDBACK_IN,
                            .bmAttributes = 0x11,
                            .wMaxPacketSize = 0x04,
                            .bInterval = 1,
                        },
                },
            #if PICODAC_OUTPUT_SPDIF
            .as_ac3 = USB_IEC61937_ALT(AUDIO_ALT_AC3, AUDIO_FORMAT_III_AC3),
            .as_dts_i = USB_IEC61937_ALT(AUDIO_ALT_DTS_I, AUDIO_FORMAT_III_DTS_I),
            .as_dts_ii = USB_IEC61937_ALT(AUDIO_ALT_DTS_II, AUDIO_FORMAT_III_DTS_II),
            .as_dts_iii = USB_IEC61937_ALT(AUDIO_ALT_DTS_III, AUDIO_FORMAT_III_DTS_III),
#if PICODAC_EAC3_PASSTHROUGH
            // CM6646-style Type III carrier, excluding unsupported WMA D12.
            // AC3/DTS compatibility mask, NOT a native E-AC-3 declaration.
            // UAC3 E-AC-3 D21 must NOT be inserted into this UAC2 descriptor.
            .as_eac3 = USB_CARRIER_ALT(AUDIO_ALT_EAC3,
                AUDIO_FORMAT_III_AC3 | AUDIO_FORMAT_III_DTS_I |
                AUDIO_FORMAT_III_DTS_II | AUDIO_FORMAT_III_DTS_III, 3,
                AUDIO_CONTROL_ID_INPUT, AUDIO_EAC3_MAX_PACKET_SIZE),
#endif
            #endif
        },
#if HID_ENABLE
    .hid =
        {
            .hid_interface =
                {
                    .bLength = sizeof(struct usb_interface_descriptor_t),
                    .bDescriptorType = USB_DT_INTERFACE,
                    .bInterfaceNumber = INTERFACE_HID,
                    .bAlternateSetting = 0,
                    .bNumEndpoints = 2,  // IN, OUT
                    .bInterfaceClass = 0x03,
                    .bInterfaceSubClass = 0,
                    .bInterfaceProtocol = 0,
                    .iInterface = 0,
                },
            .hid_descriptor =
                {
                    .bLength = sizeof(struct usb_hid_descriptor_t),
                    .bDescriptorType = USB_DT_HID,
                    .bcdHID = 0x0111,
                    .bCountryCode = 0,
                    .bNumDescriptors = 1,
                    .bDescriptorType2 =
                        USB_DT_HID_REPORT,  // Report Descriptor Type
                    .wDescriptorLength = sizeof(report_descriptor),
                },
            .hid_in_descriptor =
                {
                    .bLength = sizeof(struct usb_endpoint_descriptor_t),
                    .bDescriptorType = USB_DT_ENDPOINT,
                    .bEndpointAddress = EP_HID_IN,  // EP2 IN
                    .bmAttributes = 0x03,           // Interrupt
                    .wMaxPacketSize = HID_IN_PACKET_SIZE,
                    .bInterval = HID_INTERVAL_MS,   // TODO 10ms
                },
            .hid_out_descriptor =
                {
                    .bLength = sizeof(struct usb_endpoint_descriptor_t),
                    .bDescriptorType = USB_DT_ENDPOINT,
                    .bEndpointAddress = EP_HID_OUT,  // EP2 OUT
                    .bmAttributes = 0x03,            // Interrupt
                    .wMaxPacketSize = HID_OUT_PACKET_SIZE,
                    .bInterval = HID_INTERVAL_MS,    // TODO 10ms
                },
        },
#endif
};

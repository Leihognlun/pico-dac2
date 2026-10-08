#pragma once

#define USB_DIR_IN 0x80
#define USB_DIR_OUT 0x00

// Common
#define USB_DT_DEVICE 0x01
#define USB_DT_CONFIG 0x02
#define USB_DT_STRING 0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT 0x05
#define USB_DT_QUALIFIER 0x06
#define USB_DT_IAD 0x0B

// HID
#define USB_DT_HID 0x21
#define USB_DT_HID_REPORT 0x22

// UAC
#define USB_DT_CS_INTERFACE 0x24
#define USB_DT_CS_ENDPOINT 0x25

// #define USB_REQUEST_GET_STATUS 0x00
// #define USB_REQUEST_SET_ADDRESS 0x05
// #define USB_REQUEST_GET_DESCRIPTOR 0x06
// #define USB_REQUEST_SET_CONFIGURATION 0x09
// #define USB_REQUEST_SET_INTERFACE 0x0B

struct usb_setup_packet {
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} __attribute__((packed));

struct usb_device_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
} __attribute__((packed));

struct usb_interface_descriptor_t {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor_t {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
} __attribute__((packed));

struct usb_configuration_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t wTotalLength;
  uint8_t bNumInterfaces;
  uint8_t bConfigurationValue;
  uint8_t iConfiguration;
  uint8_t bmAttributes;
  uint8_t bMaxPower;
} __attribute__((packed));

struct usb_interface_association_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bFirstInterface;
  uint8_t bInterfaceCount;
  uint8_t bFunctionClass;
  uint8_t bFunctionSubClass;
  uint8_t bFunctionProtocol;
  uint8_t iFunction;
} __attribute__((packed));

struct usb_standard_interface_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoint;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
} __attribute__((packed));

typedef struct usb_standard_interface_descriptor
    usb_standard_ac_interface_descriptor;

// Class-Specific AC Interface Header Descriptor (ADC 2.0)
struct usb_class_specific_ac_interface_header_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint16_t bcdACD;
  uint8_t bCategory;
  uint16_t wTotalLength;
  uint8_t bmControls;
} __attribute__((packed));

// Clock Source Descriptor (ADC 2.0)
struct usb_class_specific_ac_clock_source_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bClockID;
  uint8_t bmAttributes;
  uint8_t bmControls;
  uint8_t bAssocTerminal;
  uint8_t iClockSource;
} __attribute__((packed));

// Input Terminal Descriptor (ADC 2.0)
struct usb_class_specific_ac_input_terminal_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bTerminalID;
  uint16_t wTerminalType;
  uint8_t bAssocTerminal;
  uint8_t bCSourceID;
  uint8_t bNrChannels;
  uint32_t bmChannelConfig;
  uint8_t iChannelNames;
  uint16_t bmControls;
  uint8_t iTerminal;
} __attribute__((packed));

// Output Terminal Descriptor (ADC 2.0)
struct usb_class_specific_ac_output_terminal_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bTerminalID;
  uint16_t wTerminalType;
  uint8_t bAssocTerminal;
  uint8_t bSourceID;
  uint8_t bCSourceID;
  uint16_t bmControls;
  uint8_t iTerminal;
} __attribute__((packed));

// Feature Unit Descriptor (ADC 2.0)
struct usb_class_specific_ac_feature_unit_descriptor_stereo {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bUnitID;
  uint8_t bSourceID;
  uint32_t bmaControls[3];
  uint8_t iFeature;
} __attribute__((packed));

// Standard AS Interface Descriptor
typedef struct usb_standard_interface_descriptor
    usb_standard_as_interface_descriptor;

// Class-Specific AS Interface Descriptor (ADC 2.0)
struct usb_class_specific_as_interface_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bTerminalLink;
  uint8_t bmControls;
  uint8_t bFormatType;
  uint32_t bmFormats;
  uint8_t bNrChannels;
  uint32_t bmChannelConfig;
  uint8_t iChannelNames;
} __attribute__((packed));

// Type I Format Type Descriptor (ADC 2.0)
struct usb_class_specific_as_type_i_format_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bFormatType;
  uint8_t bSubslotSize;
  uint8_t bBitResolution;
} __attribute__((packed));

// Standard AS Isochronous Audio Data Endpoint Descriptor (ADC 2.0)
struct usb_standard_as_isochronous_audio_data_endpoint_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
} __attribute__((packed));

// Class-Specific AS Isochronous Audio Data Endpoint Descriptor (ADC 2.0)
struct usb_class_specific_as_isochronous_audio_data_endpoint_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bmAttributes;
  uint8_t bmControls;
  uint8_t bLockDelayUnits;
  uint16_t wLockDelay;
} __attribute__((packed));

// Standard AS Isochronous Feedback Endpoint Descriptor
struct usb_standard_as_isochronous_feedback_endpoint_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
} __attribute__((packed));

// HID Descriptor
struct usb_hid_descriptor_t {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdHID;
  uint8_t bCountryCode;
  uint8_t bNumDescriptors;
  uint8_t bDescriptorType2;
  uint16_t wDescriptorLength;
} __attribute__((packed));

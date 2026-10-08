#pragma once

#include <stdbool.h>
#include <stdint.h>

struct usb_setup_packet_t {
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
};

// 枚举值与端点描述符 bmAttributes 的低 2 位保持一致，
// 因此可以直接赋值完成类型转换
enum endpoint_type_t {
  USB_ENDPOINT_CONTROL = 0,
  USB_ENDPOINT_ISOCHRONOUS = 1,
  USB_ENDPOINT_BULK = 2,
  USB_ENDPOINT_INTERRUPT = 3,
};

typedef bool (*usb_ep0_out_handler)(const struct usb_setup_packet_t* pkt,
                                    const uint8_t* buf, uint16_t len);
typedef void (*usb_ep_out_handler)(const uint8_t* buf, uint16_t len);
typedef void (*usb_ep_in_handler)();

typedef bool (*usb_control_interface_in_handler)(
    const struct usb_setup_packet_t* pkt);
typedef bool (*usb_control_interface_out_handler)(
    const struct usb_setup_packet_t* pkt, const uint8_t* buf, uint16_t len);

typedef bool (*usb_device_set_interfacec_handler)(uint8_t alt);

void usb_device_init();
void usb_device_task();

void usb_device_set_ep_in_handler(uint8_t ep_num, usb_ep_in_handler handler);
void usb_device_set_ep_out_handler(uint8_t ep_num, usb_ep_out_handler handler);

void usb_device_set_control_in_handler(
    uint8_t interface_num, usb_control_interface_in_handler handler);
void usb_device_set_control_out_handler(
    uint8_t interface_num, usb_control_interface_out_handler handler);

void usb_device_set_set_interface_handler(
    uint8_t interface_num, usb_device_set_interfacec_handler handler);

void usb_ep_n_start_transfer(uint8_t ep_num, bool in, const uint8_t* buf,
                             uint16_t len);
void usb_ep0_start_transfer(const uint8_t* buf, uint16_t len);

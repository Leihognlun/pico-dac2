#if HID_ENABLE

#include "usb_hid.h"

#include <assert.h>

#include "audio_device.h"
#include "audio_diagnostics.h"
#include "log.h"
#include "usb.h"
#include "usb_config.h"

static void send_diagnostics(void) {
  static uint8_t page;
  uint8_t report[16];
  uint32_t silence = 0, spdif_stalls = 0, i2s_stalls = 0;
#if PICODAC_OUTPUT_SPDIF
  silence = spdif_silence_block_count;
  spdif_stalls = spdif_tx_stall_count;
  i2s_stalls = i2s_tx_stall_count;
#endif
  // 每页字段独立采样，不作为跨页原子快照；计数从芯片启动累计。
  if (page == 0)
    audio_diagnostic_report(report, page, audio_underrun_count,
                            audio_dropped_frames, silence);
  else if (page == 1)
    audio_diagnostic_report(report, page, spdif_stalls, i2s_stalls,
                            usb_audio_bad_packets);
  else if (page == 2)
    audio_diagnostic_report(report, page, audio_device_get_sampling_freq(),
                            usb_audio_rx_packets, usb_audio_rx_frames);
  else
    audio_diagnostic_report(report, page, usb_audio_queue_drops, 0, 0);
  // USB 发送函数会立即复制报告内容到 DPRAM。
  usb_ep_n_start_transfer(EP_HID_IN & 0x7F, true, report, sizeof(report));
  page = (page + 1) % 4;
}

bool usb_hid_set_interface(uint8_t alt) {
  // 设置新的备用接口设置 alt
  LOG_INFO("Set interface HID alt %d\r", alt);

  assert(alt == 0);
  send_diagnostics();
  usb_ep_n_start_transfer(EP_HID_OUT, false, NULL, 16);
  return true;
}

bool usb_hid_control_out_request(const struct usb_setup_packet_t *pkt,
                                 const uint8_t *buf, uint16_t len) {
  (void)buf;
  (void)len;
  if (pkt->bmRequestType == 0x21 && pkt->bRequest == 0x0A && pkt->wValue == 0 &&
      (pkt->wIndex & 0xFF) == INTERFACE_HID) {
    LOG_DEBUG("unhandled CLEAR");
    return true;
  }

  LOG_INFO(
      "unhandled OUT request(type=0x%02x, request=0x%02x, value=0x%04x, "
      "index=0x%04x, length=0x%04x)",
      pkt->bmRequestType, pkt->bRequest, pkt->wValue, pkt->wIndex,
      pkt->wLength);
  // 默认不处理
  return false;
}

static void ep_hid_out_handler(const uint8_t *buf, uint16_t len) {
  (void)buf;
  (void)len;
  LOG_INFO("ep_hid_out_handler: %d byte", len);
  usb_ep_n_start_transfer(EP_HID_OUT, false, NULL, 16);
}

static void ep_hid_in_handler() {
  // LOG_INFO("ep_hid_in_handler");
  send_diagnostics();
}

void usb_hid_init() {
  usb_device_set_ep_out_handler(EP_HID_OUT, ep_hid_out_handler);
  usb_device_set_ep_in_handler(EP_HID_IN & 0x7F, ep_hid_in_handler);

  usb_device_set_control_out_handler(INTERFACE_HID,
                                     usb_hid_control_out_request);

  usb_device_set_set_interface_handler(INTERFACE_HID, usb_hid_set_interface);
}

#endif

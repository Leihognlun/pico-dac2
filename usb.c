#include "usb.h"

#include <stdint.h>
#include <string.h>

#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/structs/usb.h"
#include "log.h"
#include "pico/util/queue.h"
#include "usb_common.h"
#include "usb_buffer_control.h"
#include "usb_audio_packet.h"
#include "hardware/sync.h"
// TODO 改为支持外部配置
#include "usb_descriptor.h"

#ifndef USB_QUEUE_LENGTH
#define USB_QUEUE_LENGTH 16
#endif

#ifndef USB_QUEUE_PCORESSES_MAX
#define USB_QUEUE_PCORESSES_MAX 16
#endif

struct endpoint_handler_config {
  usb_ep_in_handler in[16];
  usb_ep_out_handler out[16];
} static ep_handler;

#ifndef MUSB_MAX_INTERFACES
#define MUSB_MAX_INTERFACES 8
#endif

struct control_handler_config {
  usb_control_interface_in_handler in[MUSB_MAX_INTERFACES];
  usb_control_interface_out_handler out[MUSB_MAX_INTERFACES];
  usb_device_set_interfacec_handler set[MUSB_MAX_INTERFACES];
} static interface_handler;

#define MUSB_WEAK __attribute__((weak))

static queue_t queue;

enum {
  USB_EVENT_TYPE_SETUP_PACKET,
  USB_EVENT_BUFF_DONE,
  USB_EVENT_TYPE_BUS_RESET,
};

struct usb_buff_done_t {
  uint8_t ep_num;
  bool in;
  volatile void* buf;
  uint16_t len;
  bool audio_snapshot;
  usb_audio_packet_t audio;
};

struct usb_event_t {
  uint8_t type;
  union {
    struct usb_setup_packet_t setup_packet;
    struct usb_buff_done_t buff_done;
  };
};

static uint8_t device_address = 0;
static bool should_set_address = false;
static volatile bool configured = false;
static uint8_t alt_settings[MUSB_MAX_INTERFACES];
static volatile bool audio_rx_enabled;
static volatile uint32_t audio_rx_generation;
volatile uint32_t usb_audio_queue_drops;

#define usb_hw_set hw_set_alias(usb_hw)
#define usb_hw_clear hw_clear_alias(usb_hw)

#define LOG_USB_DEBUG(...) LOG_BASE("USB", __VA_ARGS__)

static void isr_usbctrl_handler();

static void usb_setup_endpoints();

void usb_device_init() {
  // 复位 USB 控制器
  reset_unreset_block_num_wait_blocking(RESET_USBCTRL);
  memset(usb_dpram, 0, sizeof(*usb_dpram));

  // 设置 USB 中断处理函数
  irq_set_exclusive_handler(USBCTRL_IRQ, isr_usbctrl_handler);

  // 启用中断
  irq_set_enabled(USBCTRL_IRQ, true);

  // 配置多路复用器
  // 正常使用时指定以下值
  // 其他位用于 RP2040 芯片开发阶段的验证
  usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;

  // 配置 VBUS 检测
  // 通过软件将 VBUS 标记为已检测到
  // 对于使用特定 GPIO 检测 VBUS 的开发板，无需此设置
  usb_hw->pwr =
      USB_USB_PWR_VBUS_DETECT_BITS | USB_USB_PWR_VBUS_DETECT_OVERRIDE_EN_BITS;

  // 设置为设备模式
  // 主机模式下还需指定 USB_MAIN_CTRL_HOST_NDEVICE_BITS
  usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;

  // 在 EP0 缓冲区状态变化时触发中断
  // 这里假定使用单缓冲区
  usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;

  // 指定触发中断的事件
  // - 缓冲区状态变化
  // - 总线复位
  // - 设置请求
  usb_hw->inte = USB_INTS_BUFF_STATUS_BITS | USB_INTS_BUS_RESET_BITS |
                 USB_INTS_SETUP_REQ_BITS;

  // 配置端点
  usb_setup_endpoints();

  // 配置为 USB 全速设备
  queue_init(&queue, sizeof(struct usb_event_t), USB_QUEUE_LENGTH);
  usb_hw_set->sie_ctrl = USB_SIE_CTRL_PULLUP_EN_BITS;
}

static void event_put(struct usb_event_t* event) {
  if (!queue_try_add(&queue, event)) {
    panic("FATAL: internal queue of musb is full");
  }
}

static void usb_handle_buff_status_isr();

static void isr_usbctrl_handler() {
  uint32_t status = usb_hw->ints;
  uint32_t handled = 0;

  static struct usb_event_t event;

  if (status & USB_INTS_SETUP_REQ_BITS) {
    handled |= USB_INTS_SETUP_REQ_BITS;
    usb_hw_clear->sie_status = USB_SIE_STATUS_SETUP_REC_BITS;
    event.type = USB_EVENT_TYPE_SETUP_PACKET;
    memcpy(&event.setup_packet, (void*)usb_dpram->setup_packet, 8);
    event_put(&event);
  }

  if (status & USB_INTS_BUFF_STATUS_BITS) {
    handled |= USB_INTS_BUFF_STATUS_BITS;
    usb_handle_buff_status_isr();
  }

  if (status & USB_INTS_BUS_RESET_BITS) {
    // LOG_USB_DEBUG("bus reset");
    handled |= USB_INTS_BUS_RESET_BITS;
    usb_hw_clear->sie_status = USB_SIE_STATUS_BUS_RESET_BITS;
    event.type = USB_EVENT_TYPE_BUS_RESET;
    event_put(&event);
  }

  if (status ^ handled) {
    panic("Unhandled IRQ 0x%x", (uint)(status ^ handled));
  }
}

static bool queue_get(struct usb_event_t* event) {
  return queue_try_remove(&queue, event);
}

#define USB_REQ_DIRECTION_MASK 0x80
#define USB_REQ_TYPE_MASK 0x60
#define USB_REQ_RECIPIENT_MASK 0x1F

#define USB_REQ_DIRECTION_OUT 0x00
#define USB_REQ_DIRECTION_IN 0x80

#define USB_REQ_TYPE_STANDARD 0x00
#define USB_REQ_TYPE_CLASS 0x20
#define USB_REQ_TYPE_VENDOR 0x40

#define USB_REQ_RECIPIENT_DEVICE 0x00
#define USB_REQ_RECIPIENT_INTERFACE 0x01
#define USB_REQ_RECIPIENT_ENDPOINT 0x02

// 标准请求 bRequest
#define USB_REQ_GET_STATUS 0x00
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_REQ_SET_FEATURE 0x03
#define USB_REQ_SET_ADDRESS 0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE 0x0A
#define USB_REQ_SET_INTERFACE 0x0B
#define USB_REQ_SYNCH_FRAME 0x12
static void usb_ep0_stall();

// 控制调试输出启用或禁用的宏
#define USB_DEBUG_SETUP 1

#if USB_DEBUG_SETUP
static inline const char* req_type_str(uint8_t bmRequestType) {
  switch ((bmRequestType >> 5) & 0x03) {
    case 0:
      return "Standard";
    case 1:
      return "Class";
    case 2:
      return "Vendor";
    case 3:
      return "Reserved";
    default:
      return "?";
  }
}

static inline const char* recipient_str(uint8_t bmRequestType) {
  switch (bmRequestType & 0x1F) {
    case 0:
      return "Device";
    case 1:
      return "Interface";
    case 2:
      return "Endpoint";
    case 3:
      return "Other";
    default:
      return "Reserved";
  }
}

static inline const char* bRequest_str(uint8_t bRequest) {
  switch (bRequest) {
    case 0x00:
      return "GET_STATUS";
    case 0x01:
      return "CLEAR_FEATURE";
    case 0x03:
      return "SET_FEATURE";
    case 0x05:
      return "SET_ADDRESS";
    case 0x06:
      return "GET_DESCRIPTOR";
    case 0x07:
      return "SET_DESCRIPTOR";
    case 0x08:
      return "GET_CONFIGURATION";
    case 0x09:
      return "SET_CONFIGURATION";
    case 0x0A:
      return "GET_INTERFACE";
    case 0x0B:
      return "SET_INTERFACE";
    case 0x0C:
      return "SYNCH_FRAME";
    case 0x30:
      return "SET_SEL";
    case 0x31:
      return "SET_ISOCH_DELAY";
    default:
      return "Unknown/Custom";
  }
}

static inline void debug_print_setup(const struct usb_setup_packet_t* pkt) {
#if USB_DEBUG_SETUP
  LOG_USB_DEBUG("SETUP packet:");
  LOG_USB_DEBUG("  bmRequestType: 0x%02X", pkt->bmRequestType);
  LOG_USB_DEBUG("    Direction:   %s", (pkt->bmRequestType & 0x80)
                                           ? "Device->Host (IN)"
                                           : "Host->Device (OUT)");
  LOG_USB_DEBUG("    Type:        %s", req_type_str(pkt->bmRequestType));
  LOG_USB_DEBUG("    Recipient:   %s", recipient_str(pkt->bmRequestType));
  LOG_USB_DEBUG("  bRequest:      0x%02X (%s)", pkt->bRequest,
                bRequest_str(pkt->bRequest));
  LOG_USB_DEBUG("  wValue:        0x%04X", pkt->wValue);
  LOG_USB_DEBUG("  wIndex:        0x%04X", pkt->wIndex);
  LOG_USB_DEBUG("  wLength:       %u", pkt->wLength);
#else
  (void)pkt;  // 抑制未使用参数的警告
#endif
}
#endif

static bool usb_set_configuration(uint8_t config);
static bool usb_set_interface(uint8_t itf, uint8_t alt);
static void usb_ep0_out_ack();
static void usb_reset_next_id(uint8_t ep_num);

static struct usb_setup_packet_t last_packet;
typedef bool (*in_callback)(const struct usb_setup_packet_t* pkt);
typedef bool (*out_callback)(const struct usb_setup_packet_t* pkt,
                             const uint8_t* buf, uint16_t len);

static void usb_handle_standard_request(const struct usb_setup_packet_t* pkt) {
  uint8_t type = pkt->bmRequestType & USB_REQ_TYPE_MASK;
  uint8_t recipient = pkt->bmRequestType & USB_REQ_RECIPIENT_MASK;
  uint8_t direction = pkt->bmRequestType & USB_REQ_DIRECTION_MASK;

  switch (recipient) {
    case USB_REQ_RECIPIENT_DEVICE:
      if (direction == USB_REQ_DIRECTION_IN) {
        switch (pkt->bRequest) {
          case USB_REQ_GET_STATUS: {
            // bit0 = Self Powered, bit1 = Remote Wakeup

            static const uint16_t status = 0;
            usb_ep0_start_transfer((void*)&status, 2);
          } break;
          case USB_REQ_GET_DESCRIPTOR:
            uint16_t descriptor_type = pkt->wValue >> 8;
            switch (descriptor_type) {
              case USB_DT_DEVICE:
                usb_ep0_start_transfer(
                    (uint8_t*)&device_descriptor,
                    MIN(pkt->wLength, sizeof(device_descriptor)));
                return;
              case USB_DT_CONFIG:
                usb_ep0_start_transfer(
                    (uint8_t*)&configuration_descriptor,
                    MIN(pkt->wLength, sizeof(configuration_descriptor)));
                return;
              case USB_DT_STRING:
                static const uint8_t dummy_string_descriptor[] = {
                    // Header
                    12,
                    USB_DT_STRING,  // bDescriptorType: 0x03
                    'd',
                    0,
                    'u',
                    0,
                    'm',
                    0,
                    'm',
                    0,
                    'y',
                    0,
                };
                usb_ep0_start_transfer(
                    dummy_string_descriptor,
                    MIN(pkt->wLength, sizeof(dummy_string_descriptor)));
                return;
              case USB_DT_QUALIFIER:
                // 仅支持全速模式，因此返回 STALL
                usb_ep0_stall();
                return;
              default:
                usb_ep0_stall();
                LOG_USB_DEBUG("unknown descriptor type(%d) at GET_DESCRIPTOR",
                              descriptor_type);
#if USB_DEBUG_SETUP
                debug_print_setup(pkt);
#endif
                return;
            }
          default:
            usb_ep0_stall();
            LOG_USB_DEBUG("unhandled STANDARD DEVICE REQUEST(IN): %d",
                          pkt->bRequest);
#if USB_DEBUG_SETUP
            debug_print_setup(pkt);
#endif
            return;
        }
      } else {
        switch (pkt->bRequest) {
          case USB_REQ_SET_ADDRESS:
            device_address = pkt->wValue & 0x7F;
            should_set_address = true;
            usb_ep0_out_ack();
            LOG_USB_DEBUG("SET_ADDRESS: %d", device_address);
            return;
          case USB_REQ_SET_CONFIGURATION:
            if (usb_set_configuration(pkt->wValue)) {
              usb_ep0_out_ack();
            } else {
              usb_ep0_stall();
            }
            break;
          default:
            usb_ep0_stall();
            LOG_USB_DEBUG("unhandled STANDARD DEVICE REQUEST(OUT): %d",
                          pkt->bRequest);
#if USB_DEBUG_SETUP
            debug_print_setup(pkt);
#endif
            return;
        }
      }
    case USB_REQ_RECIPIENT_INTERFACE:
      if (direction == USB_REQ_DIRECTION_OUT) {
        switch (pkt->bRequest) {
          case USB_REQ_SET_INTERFACE: {
            uint8_t itf = pkt->wIndex & 0xFF;
            uint8_t alt = pkt->wValue & 0xFF;
            if (pkt->wIndex < INTERFACE_NUM && pkt->wValue <= UINT8_MAX &&
                pkt->wLength == 0 && interface_handler.set[itf] &&
                usb_set_interface(itf, alt) &&
                interface_handler.set[itf](alt)) {
              usb_ep0_out_ack();
            } else {
              usb_ep0_stall();
            }
            return;
          }
          default:
            return;
        }
      } else {
        switch (pkt->bRequest) {
          case USB_REQ_GET_INTERFACE:
            if (pkt->wIndex >= INTERFACE_NUM || pkt->wValue != 0 ||
                pkt->wLength != 1 || !configured) {
              usb_ep0_stall();
            } else {
              usb_ep0_start_transfer(&alt_settings[pkt->wIndex], 1);
            }
            return;
          case USB_REQ_GET_STATUS:
            static const uint16_t zero = 0;
            usb_ep0_start_transfer((void*)&zero, 2);
            return;
#if HID_ENABLE
          // TODO 定义常量
          case 0x06:
            if ((pkt->wValue >> 8) == 0x22) {
              usb_ep0_start_transfer(
                  report_descriptor,
                  MIN(pkt->wLength, sizeof(report_descriptor)));
              return;
            }
#endif
          default:
            usb_ep0_stall();
            LOG_USB_DEBUG("unhandled STANDARD INTERFACE REQUEST(IN)");
#if USB_DEBUG_SETUP
            debug_print_setup(pkt);
#endif
            return;
        }
      }
      break;
    case USB_REQ_RECIPIENT_ENDPOINT:
      if (pkt->bRequest == 0x01 && pkt->wIndex == 0x82) {
        // TODO
        usb_reset_next_id(2);
        static uint8_t zero[16] = {0};
        usb_ep_n_start_transfer(2, true, zero, 16);
        usb_ep_n_start_transfer(2, false, zero, 16);
        usb_ep0_out_ack();
      } else {
        usb_ep0_stall();
      }
      return;
    default:
      usb_ep0_stall();
      LOG_USB_DEBUG("unhandled recipient: %d", recipient);
#if USB_DEBUG_SETUP
      debug_print_setup(pkt);
#endif
      return;
  }
}

static void usb_handle_custom_request(const struct usb_setup_packet_t* pkt) {
  uint8_t direction = pkt->bmRequestType & USB_REQ_DIRECTION_MASK;
  uint8_t interface_num = pkt->wIndex & 0xFF;

  bool handled = false;
  if (direction == USB_REQ_DIRECTION_IN) {
    assert(interface_num < MUSB_MAX_INTERFACES);
    if (interface_handler.in[interface_num]) {
      handled |= interface_handler.in[interface_num](pkt);
    }
  } else {
    if (pkt->wLength == 0) {
      // 长度为 0 字节时立即通知
      assert(interface_num < MUSB_MAX_INTERFACES);
      if (interface_handler.out[interface_num]) {
        handled |= interface_handler.out[interface_num](pkt, NULL, 0);
      }
      if (handled) {
        usb_ep0_out_ack();
      }
    } else {
      // TODO 当前实现仅能接收一个数据包
      assert(pkt->wLength <= 64);
      memcpy(&last_packet, pkt, 8);
      usb_ep_n_start_transfer(0, false, NULL, pkt->wLength);
      // 实际处理在数据阶段进行，此处视为成功
      // TODO 提供回调，以决定是否在此时返回 STALL
      handled = true;
    }
  }

  if (!handled) {
    usb_ep0_stall();
    LOG_USB_DEBUG("unhandled request");
#if USB_DEBUG_SETUP
    debug_print_setup(pkt);
#endif
    return;
  }
}

void usb_handle_setup_packet(const struct usb_setup_packet_t* pkt) {
  uint8_t type = pkt->bmRequestType & USB_REQ_TYPE_MASK;

  // debug_print_setup(pkt);

  if (type == USB_REQ_TYPE_STANDARD) {
    usb_handle_standard_request(pkt);
    return;
  }

  if (type == USB_REQ_TYPE_CLASS || type == USB_REQ_TYPE_VENDOR) {
    usb_handle_custom_request(pkt);
    return;
  }

  usb_ep0_stall();
  LOG_USB_DEBUG("unknown request type: %02x", type);
#if USB_DEBUG_SETUP
  debug_print_setup(pkt);
#endif
}

static void usb_handle_buff_done(const struct usb_buff_done_t* buff_done);

static void usb_bus_reset();

void usb_device_task() {
  static struct usb_event_t event;
  for (int i = 0; i < USB_QUEUE_PCORESSES_MAX; ++i) {
    if (!queue_get(&event)) {
      break;
    }

    switch (event.type) {
      case USB_EVENT_TYPE_SETUP_PACKET:
        usb_handle_setup_packet(&event.setup_packet);
        break;
      case USB_EVENT_BUFF_DONE:
        usb_handle_buff_done(&event.buff_done);
        break;
      case USB_EVENT_TYPE_BUS_RESET:
        usb_bus_reset();
        break;
      default:
        panic("unknown event type @ usb_device_task");
    }
  }
}

// transfer
typedef struct {
  const uint8_t* data;
  uint16_t total_len;
  uint16_t sent_len;
} usb_transfer_state_t;

static usb_transfer_state_t transfer_state_ep0_out;

struct endpoint_config {
  volatile uint8_t* buf;
  volatile uint32_t* buf_ctrl;
  uint8_t next_pid;
  uint16_t max_packet_size;
  enum endpoint_type_t type;
};

struct endpoint_config ep_in[16];
struct endpoint_config ep_out[16];

static void usb_reset_next_id(uint8_t ep_num) {
  ep_in[ep_num].next_pid = ep_out[ep_num].next_pid = 0;
}

static void usb_start_transfer(struct endpoint_config* ep, bool in,
                               const uint8_t* buf, size_t len) {
  // 超过一个数据包的传输需要额外的缓冲区管理
  if (ep->max_packet_size < len) {
    LOG_USB_DEBUG("len: %d, max: %d", len, ep->max_packet_size);
  }
  assert(len <= ep->max_packet_size);
  if (ep->type == USB_ENDPOINT_ISOCHRONOUS) {
    assert(len < 1024);
  } else {
    assert(len <= 64);
  }

  // LOG_USB_DEBUG("%d bytes %s", len, in ? "IN" : "OUT");

  uint32_t val = len | USB_BUF_CTRL_AVAIL;

  // 发送数据时
  if (in) {
    memcpy((void*)ep->buf, buf, len);
    // 设置缓冲区已填充标志
    val |= USB_BUF_CTRL_FULL;
  }

  // 设置 PID，交替使用 0 和 1
  val |= ep->next_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;
  ep->next_pid ^= 0x01;

  // 启动传输
  usb_buffer_control_publish(ep->buf_ctrl, val);
}

void usb_ep_n_start_transfer(uint8_t ep_num, bool in, const uint8_t* buf,
                             uint16_t len) {
  // 音频 OUT 由中断自动重新准备，主循环不可再次改写控制寄存器。
  if (ep_num == EP_AUDIO_STREAM_OUT && !in &&
      ep_out[ep_num].type == USB_ENDPOINT_ISOCHRONOUS) return;
  if (ep_num == 0 && !in) {
    ep_out->next_pid = 1;
  }
  usb_start_transfer(in ? &ep_in[ep_num] : &ep_out[ep_num], in, buf, len);
}

static void usb_ep0_continue_transfer() {
  uint16_t remaining =
      transfer_state_ep0_out.total_len - transfer_state_ep0_out.sent_len;
  uint16_t size_to_send = remaining > 64 ? 64 : remaining;

  usb_start_transfer(
      ep_in, true,
      transfer_state_ep0_out.data + transfer_state_ep0_out.sent_len,
      size_to_send);
  transfer_state_ep0_out.sent_len += size_to_send;
}

void usb_ep0_start_transfer(const uint8_t* buf, uint16_t len) {
  transfer_state_ep0_out.data = buf;
  transfer_state_ep0_out.total_len = len;
  transfer_state_ep0_out.sent_len = 0;
  ep_in->next_pid = 1;

  usb_ep0_continue_transfer();
}

static void usb_ep0_out_ack() { usb_ep0_start_transfer(NULL, 0); }

static void usb_ep0_stall() {
  usb_hw->ep_stall_arm =
      USB_EP_STALL_ARM_EP0_IN_BITS | USB_EP_STALL_ARM_EP0_OUT_BITS;
  usb_dpram->ep_buf_ctrl->in = USB_BUF_CTRL_STALL;
  usb_dpram->ep_buf_ctrl->out = USB_BUF_CTRL_STALL;
}

static void usb_bus_reset() {
  audio_rx_enabled = false;
  ++audio_rx_generation;
  usb_hw->dev_addr_ctrl = device_address = 0;
  should_set_address = false;
  configured = false;
  memset(alt_settings, 0, sizeof(alt_settings));
  for (unsigned i = 0; i < INTERFACE_NUM; ++i) {
    if (interface_handler.set[i]) interface_handler.set[i](0);
  }

  // TODO 复位端点，目前仅复位 next_pid
  for (int i = 0; i < USB_NUM_ENDPOINTS; ++i) {
    ep_in[i].next_pid = ep_out[i].next_pid = 0;
  }
}

static void handle_ep0(const struct usb_buff_done_t* buff_done) {
  if (buff_done->ep_num == 0 && buff_done->in && should_set_address) {
    usb_hw->dev_addr_ctrl = device_address;
    should_set_address = false;
    return;
  }

  // TODO 将多包传输支持扩展到 EP0 OUT 以外的端点
  if (buff_done->ep_num == 0 && buff_done->in) {
    if (transfer_state_ep0_out.sent_len < transfer_state_ep0_out.total_len) {
      // 如果还有剩余数据，则继续发送
      usb_ep0_continue_transfer();
    } else {
      if (transfer_state_ep0_out.total_len != 0 &&
          (transfer_state_ep0_out.total_len % 64) == 0) {
        // 最后一个数据包为 64 字节时，发送零长度包作为结束标记
        usb_ep0_start_transfer(NULL, 0);
        return;
      }

      // 接收零长度状态包
      ep_out->next_pid = 1;
      usb_start_transfer(ep_out, false, NULL, 0);

      if (ep_handler.in[0]) {
        ep_handler.in[0]();
      }
    }
  }
}

static void call_handler(uint8_t ep_num, bool in, const void* buf,
                         uint16_t len) {
  if (ep_num == 0 && !in && len != 0) {
    bool handled = false;
    uint8_t type = last_packet.bmRequestType & USB_REQ_TYPE_MASK;
    if (type == USB_REQ_TYPE_CLASS || type == USB_REQ_TYPE_VENDOR) {
      uint8_t interface_num = last_packet.wIndex & 0xFF;
      assert(interface_num < MUSB_MAX_INTERFACES);
      if (interface_handler.out[interface_num]) {
        handled |= interface_handler.out[interface_num](&last_packet, buf, len);
      }
    }

    if (handled) {
      usb_ep0_out_ack();
    } else {
      usb_ep0_stall();
      LOG_USB_DEBUG("unhandled");
#if USB_DEBUG_SETUP
      debug_print_setup(&last_packet);
#endif
    }
    return;
  }

  if (ep_num != 0) {
    if (in) {
      if (ep_handler.in[ep_num]) {
        ep_handler.in[ep_num]();
      }
    } else {
      if (ep_handler.out[ep_num]) {
        ep_handler.out[ep_num](buf, len);
      }
    }
  }
}

static void usb_handle_buff_done(const struct usb_buff_done_t* buff_done) {
  if (buff_done->audio_snapshot) {
    if (usb_audio_packet_current(&buff_done->audio, audio_rx_enabled,
                                  audio_rx_generation)) {
      call_handler(buff_done->ep_num, false, (uint8_t *)buff_done->audio.data,
                   buff_done->audio.length);
    }
    return;
  }
  struct endpoint_config* ep =
      buff_done->in ? &ep_in[buff_done->ep_num] : &ep_out[buff_done->ep_num];

  handle_ep0(buff_done);

  call_handler(buff_done->ep_num, buff_done->in, ((uint8_t*)ep->buf),
               buff_done->len);
}

static void usb_handle_buff_done_isr(uint ep_num, bool in) {
  struct endpoint_config* ep = in ? &ep_in[ep_num] : &ep_out[ep_num];
  uint16_t len = *ep->buf_ctrl & USB_BUF_CTRL_LEN_MASK;

  static struct usb_event_t event;
  event.type = USB_EVENT_BUFF_DONE;

  event.buff_done.ep_num = ep_num;
  event.buff_done.in = in;
  event.buff_done.buf = ep->buf;
  event.buff_done.len = len;
  event.buff_done.audio_snapshot = false;
  if (ep_num == EP_AUDIO_STREAM_OUT && !in &&
      ep->type == USB_ENDPOINT_ISOCHRONOUS) {
    if (!audio_rx_enabled) return;
    event.buff_done.audio_snapshot = true;
    bool captured = usb_audio_packet_capture(&event.buff_done.audio,
        (const void *)ep->buf, len, audio_rx_generation);
    // 先保存当前包并开放接收，再交给主循环解包、音量处理和编码。
    usb_start_transfer(ep, false, NULL, ep->max_packet_size);
    // 音频积压时丢弃新包并计数，预留队列位置给控制和反馈事件。
    if (!captured || queue_get_level(&queue) >= USB_QUEUE_LENGTH - 4 ||
        !queue_try_add(&queue, &event)) ++usb_audio_queue_drops;
    return;
  }
  event_put(&event);
}

static void usb_handle_buff_status_isr() {
  uint32_t buffers = usb_hw->buf_status;
  uint32_t remaining = buffers;

  uint bit = 1;
  for (uint i = 0; remaining && i < USB_NUM_ENDPOINTS * 2; ++i) {
    if (remaining & bit) {
      usb_hw_clear->buf_status = bit;
      usb_handle_buff_done_isr(i >> 1, !(i & 1));
      remaining &= ~bit;
    }
    bit <<= 1;
  }
}

// setup
static void usb_setup_endpoints() {
  // 初始化端点管理数据
  memset(ep_in, 0, sizeof(ep_in));
  memset(ep_out, 0, sizeof(ep_out));
  memset(&transfer_state_ep0_out, 0, sizeof(transfer_state_ep0_out));
  memset(&ep_handler, 0, sizeof(ep_handler));
  memset(&interface_handler, 0, sizeof(interface_handler));

  // 初始化 EP0 IN
  ep_in[0].buf = usb_dpram->ep0_buf_a;
  ep_in[0].buf_ctrl = &usb_dpram->ep_buf_ctrl[0].in;
  ep_in[0].max_packet_size = 64;

  // 初始化 EP0 OUT
  ep_out[0].buf = usb_dpram->ep0_buf_a;
  ep_out[0].buf_ctrl = &usb_dpram->ep_buf_ctrl[0].out;
  ep_out[0].next_pid = 1;
  ep_out[0].max_packet_size = 64;
  usb_buffer_control_publish(ep_out[0].buf_ctrl,
                             USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_DATA1_PID | 64);
}

static void usb_device_enable_endpoint(uint8_t ep_num, bool in,
                                       uint16_t max_packet_size,
                                       enum endpoint_type_t type) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (ep_num == EP_AUDIO_STREAM_OUT && !in) {
    ++audio_rx_generation;
    audio_rx_enabled = type == USB_ENDPOINT_ISOCHRONOUS;
  }
  if (type == USB_ENDPOINT_ISOCHRONOUS) {
    assert(max_packet_size < 1024);
  } else {
    assert(max_packet_size <= 64);
  }

  struct endpoint_config* ep = in ? &ep_in[ep_num] : &ep_out[ep_num];

  ep->buf_ctrl = in ? &usb_dpram->ep_buf_ctrl[ep_num].in
                    : &usb_dpram->ep_buf_ctrl[ep_num].out;

  ep->next_pid = 0;
  ep->max_packet_size = max_packet_size;
  ep->type = type;

  uint32_t dpram_offset = (uint32_t)ep->buf - (uint32_t)usb_dpram;
  uint32_t reg =
      EP_CTRL_ENABLE_BITS | EP_CTRL_INTERRUPT_PER_BUFFER | dpram_offset;
  reg |= (type == USB_ENDPOINT_CONTROL       ? 0
          : type == USB_ENDPOINT_ISOCHRONOUS ? 1
          : type == USB_ENDPOINT_BULK        ? 2
                                             : 3)
         << EP_CTRL_BUFFER_TYPE_LSB;
  *(in ? &usb_dpram->ep_ctrl[ep_num - 1].in
       : &usb_dpram->ep_ctrl[ep_num - 1].out) = reg;
  usb_buffer_control_publish(ep->buf_ctrl,
      max_packet_size | USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_DATA0_PID);
  restore_interrupts(interrupts);
}

void usb_device_disable_endpoint(uint8_t ep_num, bool in,
                                 uint16_t max_packet_size,
                                 enum endpoint_type_t type) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (ep_num == EP_AUDIO_STREAM_OUT && !in) {
    audio_rx_enabled = false;
    ++audio_rx_generation;
  }
  *(in ? &usb_dpram->ep_ctrl[ep_num - 1].in
       : &usb_dpram->ep_ctrl[ep_num - 1].out) = 0;
  *(in ? &usb_dpram->ep_buf_ctrl[ep_num].in
       : &usb_dpram->ep_buf_ctrl[ep_num].out) &= ~USB_BUF_CTRL_AVAIL;
  usb_hw_clear->buf_status = 1u << (2 * ep_num + (in ? 0 : 1));
  restore_interrupts(interrupts);
}

void usb_device_set_ep_in_handler(uint8_t ep_num, usb_ep_in_handler handler) {
  ep_handler.in[ep_num] = handler;
}

void usb_device_set_ep_out_handler(uint8_t ep_num, usb_ep_out_handler handler) {
  ep_handler.out[ep_num] = handler;
}

void usb_device_set_control_in_handler(
    uint8_t interface_num, usb_control_interface_in_handler handler) {
  assert(interface_num < MUSB_MAX_INTERFACES);
  interface_handler.in[interface_num] = handler;
}

void usb_device_set_control_out_handler(
    uint8_t interface_num, usb_control_interface_out_handler handler) {
  assert(interface_num < MUSB_MAX_INTERFACES);
  interface_handler.out[interface_num] = handler;
}

void usb_device_set_set_interface_handler(
    uint8_t interface_num, usb_device_set_interfacec_handler handler) {
  assert(interface_num < MUSB_MAX_INTERFACES);
  interface_handler.set[interface_num] = handler;
}

void walk_descriptor(
    void* descriptor, uint16_t len, uint8_t itf, uint8_t alt,
    void (*callback)(const struct usb_interface_descriptor_t* itf,
                     const struct usb_endpoint_descriptor_t* edp)) {
  struct descriptor_header_t {
    uint8_t bLength;
    uint8_t bDescriptorType;
  } __attribute__((packed));

  struct usb_interface_descriptor_t* last_seen_interface = NULL;
  const uint8_t* p = (uint8_t*)descriptor;
  const uint8_t* end = ((uint8_t*)descriptor) + len;

  while (p + sizeof(struct descriptor_header_t) <= end) {
    const struct descriptor_header_t* desc = (struct descriptor_header_t*)p;
    assert(desc->bLength != 0);
    assert(p + desc->bLength <= end);

    if (desc->bDescriptorType == USB_DT_INTERFACE) {
      last_seen_interface = (struct usb_interface_descriptor_t*)p;
    } else if (desc->bDescriptorType == USB_DT_ENDPOINT) {
      assert(last_seen_interface);
      const struct usb_endpoint_descriptor_t* endpoint =
          (struct usb_endpoint_descriptor_t*)p;
      if ((itf == 0xFF || itf == last_seen_interface->bInterfaceNumber) &&
          (alt == 0xFF || alt == last_seen_interface->bAlternateSetting)) {
        callback(last_seen_interface, endpoint);
      }
    }
    p += desc->bLength;
  }
}

static uint16_t max_packet_size_in[16];
static uint16_t max_packet_size_out[16];

static void change_ep(const struct usb_interface_descriptor_t* itf,
                      const struct usb_endpoint_descriptor_t* edp,
                      void (*func)(uint8_t ep_num, bool in,
                                   uint16_t max_packet_size,
                                   enum endpoint_type_t type)) {
  assert(itf->bInterfaceNumber < MUSB_MAX_INTERFACES);
  alt_settings[itf->bInterfaceNumber] = itf->bAlternateSetting;

  bool in =
      (edp->bEndpointAddress & USB_REQ_DIRECTION_MASK) == USB_REQ_DIRECTION_IN;
  uint8_t addr = edp->bEndpointAddress & 0x0F;
  assert(addr != 0);
  enum endpoint_type_t type = edp->bmAttributes & 0x03;

  func(addr, in, edp->wMaxPacketSize, type);
}

static void set_interface(const struct usb_interface_descriptor_t* itf,
                          const struct usb_endpoint_descriptor_t* edp) {
  assert(itf->bInterfaceNumber < MUSB_MAX_INTERFACES);
  assert(interface_handler.set[itf->bInterfaceNumber]);
  interface_handler.set[itf->bInterfaceNumber](0);
}

static void enable_ep(const struct usb_interface_descriptor_t* itf,
                      const struct usb_endpoint_descriptor_t* edp) {
  change_ep(itf, edp, usb_device_enable_endpoint);
}

static void disable_ep(const struct usb_interface_descriptor_t* itf,
                       const struct usb_endpoint_descriptor_t* edp) {
  change_ep(itf, edp, usb_device_disable_endpoint);
}

static void usb_set_max_packet(const struct usb_interface_descriptor_t* itf,
                               const struct usb_endpoint_descriptor_t* edp) {
  assert(itf->bInterfaceNumber < MUSB_MAX_INTERFACES);
  bool in =
      (edp->bEndpointAddress & USB_REQ_DIRECTION_MASK) == USB_REQ_DIRECTION_IN;
  uint8_t addr = edp->bEndpointAddress & 0x0F;
  assert(addr != 0);
  if (in) {
    max_packet_size_in[addr] =
        MAX(max_packet_size_in[addr], edp->wMaxPacketSize);
  } else {
    max_packet_size_out[addr] =
        MAX(max_packet_size_out[addr], edp->wMaxPacketSize);
  }
}

static void usb_layout_buffers() {
  uint16_t dpram_pos = 0;

  for (int i = 1; i < USB_MAX_ENDPOINTS; ++i) {
    if (0 < max_packet_size_in[i]) {
      dpram_pos = (dpram_pos + 63) & ~63;
      ep_in[i].buf = &usb_dpram->epx_data[dpram_pos];
      LOG_USB_DEBUG("ep %d(in): dpram[%d:%d]", i, dpram_pos,
                    max_packet_size_in[i]);
      dpram_pos += max_packet_size_in[i];
    }
    if (0 < max_packet_size_out[i]) {
      dpram_pos = (dpram_pos + 63) & ~63;
      ep_out[i].buf = &usb_dpram->epx_data[dpram_pos];
      LOG_USB_DEBUG("ep %d(out): dpram[%d:%d]", i, dpram_pos,
                    max_packet_size_out[i]);
      dpram_pos += max_packet_size_out[i];
    }
  }
  assert(dpram_pos <= sizeof(usb_dpram->epx_data));
}

uint8_t current_config = 0;
static bool usb_set_configuration(uint8_t config) {
  audio_rx_enabled = false;
  ++audio_rx_generation;
  if (config > 1) return false;
  // Closing/reconfiguring USB must also discard the old audio stream.
  for (unsigned i = 0; i < INTERFACE_NUM; ++i) {
    if (interface_handler.set[i]) interface_handler.set[i](0);
  }
  // 初始化现有端点
  for (int i = 0;
       i < sizeof(usb_dpram->ep_ctrl) / sizeof(usb_dpram->ep_ctrl[0]); ++i) {
    usb_dpram->ep_ctrl[i].in &= ~EP_CTRL_ENABLE_BITS;
    usb_dpram->ep_ctrl[i].out &= ~EP_CTRL_ENABLE_BITS;
  }
  memset(alt_settings, 0, sizeof(alt_settings));
  memset(max_packet_size_in, 0, sizeof(max_packet_size_in));
  memset(max_packet_size_out, 0, sizeof(max_packet_size_out));

  if (config == 0) {
    configured = false;
    current_config = config;
    return true;
  }

  // TODO 支持多个配置
  assert(config == 1);
  // 应根据 config 的值切换配置
  void* desc = (void*)&configuration_descriptor;
  uint16_t len = sizeof(configuration_descriptor);

  // 读取各端点的最大数据包长度并分配 DPRAM
  walk_descriptor(desc, len, 0xFF, 0xFF, usb_set_max_packet);
  usb_layout_buffers();

  // 启用端点
  walk_descriptor(desc, len, 0xFF, 0, enable_ep);
  walk_descriptor(desc, len, 0xFF, 0, set_interface);
  configured = true;
  current_config = config;

  return true;
}

static bool usb_set_interface(uint8_t itf, uint8_t alt) {
  // TODO 支持多个配置
  // 切换到指定配置
  void* desc = (void*)&configuration_descriptor;
  uint16_t len = sizeof(configuration_descriptor);

  // Reject unknown alternate settings before disabling the active endpoints.
  bool found = false;
  const uint8_t *p = desc;
  const uint8_t *end = p + len;
  while (p < end) {
    if (p[1] == USB_DT_INTERFACE) {
      const struct usb_interface_descriptor_t *id = (const void *)p;
      if (id->bInterfaceNumber == itf && id->bAlternateSetting == alt) {
        found = true;
        break;
      }
    }
    p += p[0];
  }
  if (!found) return false;

  // 启用端点
  walk_descriptor(desc, len, itf, alt_settings[itf], disable_ep);
  walk_descriptor(desc, len, itf, alt, enable_ep);
  alt_settings[itf] = alt;

  return true;
}

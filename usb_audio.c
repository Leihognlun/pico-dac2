#include "usb_audio.h"

#include <assert.h>

#include "audio_device.h"
#include "audio_diagnostics.h"
#include "log.h"
#include "usb.h"
#include "usb_config.h"

#define MIN(a, b) (a < b ? a : b)

typedef enum {
  USB_SAMPLE_FORMAT_16,
  USB_SAMPLE_FORMAT_24,
  USB_SAMPLE_FORMAT_32,
} usb_sample_format_t;

static usb_sample_format_t g_format = USB_SAMPLE_FORMAT_16;
static uint8_t audio_stream_current_alt = 0;
volatile uint32_t usb_audio_bad_packets;
volatile uint32_t usb_audio_rx_packets;
volatile uint32_t usb_audio_rx_frames;

static void ep_audio_out_handler(const uint8_t* buf, uint16_t len) {
  // LOG_DEBUG("ep_audio_out_handler: %d bytes received", len);

  // Ignore a late completion after the host has closed the stream.
  if (audio_stream_current_alt == 0) return;
  const uint16_t max_packet = audio_stream_current_alt == AUDIO_ALT_EAC3 ?
      AUDIO_EAC3_MAX_PACKET_SIZE : audio_stream_current_alt >= AUDIO_ALT_AC3 ?
      AUDIO_IEC61937_MAX_PACKET_SIZE : AUDIO_MAX_PACKET_SIZE;
  const unsigned frame_bytes = g_format == USB_SAMPLE_FORMAT_16 ? 4 : 8;
  if (len > max_packet || len % frame_bytes) {
    // 播放期间仅计数，避免日志阻塞收包。
    ++usb_audio_bad_packets;
    usb_ep_n_start_transfer(EP_AUDIO_STREAM_OUT, false, NULL, max_packet);
    return;
  }

  const uint32_t* usb_buf = (const uint32_t*)buf;
  ++usb_audio_rx_packets;
  usb_audio_rx_frames += len / frame_bytes;

  // samples 的元素数在 16 位模式下是 usb_buf 的两倍，24 位模式下相同
  // 这里按两倍大小预留空间
  static int32_t samples[512 * 2];

  if (g_format == USB_SAMPLE_FORMAT_16) {
    // 1 frame = 2 samples (L+R) = 4 bytes.
    // usb_buf[i] contains one frame (L in lower 16 bits, R in upper 16 bits).
    const int num_frames = len / sizeof(usb_buf[0]);
    const int num_samples = num_frames * 2;
    for (int i = 0; i < num_frames; ++i) {
      // Unpack L and R samples from the frame.
      samples[2 * i] = (int16_t)(usb_buf[i] & 0xFFFF);
      samples[2 * i + 1] = (int16_t)(usb_buf[i] >> 16);
    }
    audio_device_on_usb_rx(samples, num_samples);
  } else if (g_format == USB_SAMPLE_FORMAT_24) {
    // 1 sample (L or R) = 4 bytes. 1 frame = 2 samples = 8 bytes.
    // usb_buf contains a flat stream of samples (L, R, L, R, ...).
    const int num_samples = len / sizeof(usb_buf[0]);
    for (int i = 0; i < num_samples; ++i) {
      // The 24-bit sample is in the MSBs of the 32-bit container.
      // We do an arithmetic right shift by 8 to get the top 24 bits.
      samples[i] = ((int32_t)usb_buf[i]) >> 8;
    }
    audio_device_on_usb_rx(samples, num_samples);
  } else if (g_format == USB_SAMPLE_FORMAT_32) {
    // 1 sample (L or R) = 4 bytes. 1 frame = 2 samples = 8 bytes.
    // usb_buf contains a flat stream of samples (L, R, L, R, ...).
    const int num_samples = len / sizeof(usb_buf[0]);
    for (int i = 0; i < num_samples; ++i) {
      samples[i] = (int32_t)usb_buf[i];
    }
    audio_device_on_usb_rx(samples, num_samples);
  }
  // 准备下一次传输
  usb_ep_n_start_transfer(EP_AUDIO_STREAM_OUT, false, NULL, max_packet);
}

static void feedback() {
  static float filtered_buffer_ratio = 0.5;
  static const float lpf_alpha = 0.01;
  static const float feedback_rate = 0.01;

  uint32_t sample_rate = audio_device_get_sampling_freq();
  float adjusted_rate_per_ms;
  if (audio_device_is_playing()) {
    float steady_buffer_fill_ratio =
        audio_device_get_steady_buffer_fill_ratio();
    filtered_buffer_ratio = steady_buffer_fill_ratio * lpf_alpha +
                            filtered_buffer_ratio * (1 - lpf_alpha);
    float error = filtered_buffer_ratio - 0.5;
    adjusted_rate_per_ms =
        (sample_rate / 1000.0f) * (1 - error * feedback_rate);
  } else {
    adjusted_rate_per_ms = sample_rate / 1000.0f;
    filtered_buffer_ratio = 0.5;
  }
  uint32_t feedback_value = (uint32_t)(adjusted_rate_per_ms * (1 << 16));
  usb_ep_n_start_transfer(EP_AUDIO_FEEDBACK_IN & 0x7F, true,
                          (void*)&feedback_value, sizeof(feedback_value));
}

static void ep_audio_in_handler() {
  //  feedback
  // LOG_DEBUG("ep_audio_in_handler");
  // 安排下一次反馈传输
  feedback();
}

bool usb_audio_control_set_interface(uint8_t alt) { return alt == 0; }

static void usb_audio_start_alt(uint8_t alt) {
  if (alt == 1) {
    // Linux exposes Type I and Type III as separate PCM devices. Raw E-AC-3
    // sent through device 0 therefore selects the 16-bit Type I alt. At the
    // dedicated 192 kHz carrier rate it is IEC 61937, not audible PCM.
    bool carrier =
#if PICODAC_EAC3_PASSTHROUGH
        audio_device_get_sampling_freq() == AUDIO_EAC3_RATE;
#else
        false;
#endif
    audio_device_stream_start(16, carrier);
    g_format = USB_SAMPLE_FORMAT_16;
  } else if (alt == 2) {
    audio_device_stream_start(24, false);
    g_format = USB_SAMPLE_FORMAT_24;
  } else if (alt == 3) {
    audio_device_stream_start(32, false);
    g_format = USB_SAMPLE_FORMAT_32;
#if PICODAC_OUTPUT_SPDIF
  } else if (alt >= AUDIO_ALT_AC3) {
    audio_device_stream_start(16, true);
    g_format = USB_SAMPLE_FORMAT_16;
#endif
  }
}

bool usb_audio_stream_set_interface(uint8_t alt) {
  // 设置新的备用接口设置 alt
  LOG_INFO("Set interface AUDIO_STREAM alt %d\r", alt);
  if (AUDIO_ALT_MAX < alt) {
    LOG_ERROR("unknown alt: %d", alt);
    return false;
  }

  audio_stream_current_alt = alt;

  audio_device_stream_stop();

#if PICODAC_EAC3_PASSTHROUGH
  if (alt == AUDIO_ALT_EAC3) audio_device_set_sampling_freq(AUDIO_EAC3_RATE);
#endif

  usb_audio_start_alt(alt);

  if (alt != 0) {
    // 触发反馈传输
    feedback();
  }

  return true;
}

#define UAC2_CS_REQ_CUR 0x01
#define UAC2_CS_REQ_RANGE 0x02

#define UAC2_FU_MUTE_CONTROL 0x01
#define UAC2_FU_VOLUME_CONTROL 0x02

#define UAC2_CS_SAM_FREQ_CONTROL 0x01

bool usb_audio_control_in_request(const struct usb_setup_packet_t* pkt) {
  uint8_t itf = pkt->wIndex & 0xFF;
  if (itf != INTERFACE_AUDIO_CONTROL) {
    LOG_ERROR("unhandled class in request for iff: %d", itf);
    return false;
  }

  if (pkt->bRequest == UAC2_CS_REQ_CUR) {
    if ((pkt->wIndex >> 8) == AUDIO_CONTROL_ID_CLOCK &&
        (pkt->wValue >> 8) == UAC2_CS_SAM_FREQ_CONTROL &&
        (pkt->wValue & 0xFF) == 0) {
      // GET CUR (Sampling Frequency)
      static uint8_t response[4];
      const uint32_t freq = audio_device_get_sampling_freq();

      response[0] = (uint8_t)freq;
      response[1] = (uint8_t)(freq >> 8);
      response[2] = (uint8_t)(freq >> 16);
      response[3] = (uint8_t)(freq >> 24);

      usb_ep0_start_transfer(response,
                             MIN(pkt->wLength, sizeof(response)));
      return true;
    } else if ((pkt->wIndex >> 8) == AUDIO_CONTROL_ID_FEATURE_UNIT &&
        (pkt->wValue >> 8) == UAC2_FU_VOLUME_CONTROL) {
      // GET Cur (Volume)
      static int16_t vol;
      uint8_t ch = pkt->wValue & 0xFF;
      vol = audio_device_get_volume(ch);
      usb_ep0_start_transfer((void*)&vol, MIN(pkt->wLength, sizeof(vol)));
      // static const uint16_t zero = 0;
      // usb_ep0_start_transfer((void *)&zero, MIN(pkt->wLength, sizeof(zero)));
      return true;
    } else if ((pkt->wIndex >> 8) == 0x02 && (pkt->wValue >> 8) == 0x01) {
      // Mute
      static uint8_t mute;
      uint8_t ch = pkt->wValue & 0xFF;
      mute = audio_device_get_mute(ch);
      usb_ep0_start_transfer((void*)&mute, MIN(pkt->wLength, sizeof(mute)));
      // static const uint8_t zero = 0;
      // usb_ep0_start_transfer((void *)&zero, MIN(pkt->wLength, sizeof(zero)));
      return true;
    }
  } else if (pkt->bRequest == UAC2_CS_REQ_RANGE) {
    if ((pkt->wIndex >> 8) == AUDIO_CONTROL_ID_FEATURE_UNIT &&
        (pkt->wValue >> 8) == UAC2_FU_VOLUME_CONTROL) {
      // GET RANGE (Volume)
      int16_t min, max, res;
      uint8_t ch = pkt->wValue & 0xFF;
      audio_device_get_volume_range(ch, &min, &max, &res);

      struct range2b {
        uint16_t wNumSubRanges;
        struct {
          int16_t dMIN;
          int16_t dMAX;
          int16_t dRES;
        } subranges[1];
      } __attribute__((packed));

      static struct range2b ret;
      ret.wNumSubRanges = 1;
      ret.subranges[0].dMIN = min;
      ret.subranges[0].dMAX = max;
      ret.subranges[0].dRES = res;
      usb_ep0_start_transfer((void*)&ret, MIN(pkt->wLength, sizeof(ret)));
      return true;
    } else if ((pkt->wIndex >> 8) == AUDIO_CONTROL_ID_CLOCK &&
               (pkt->wValue >> 8) == UAC2_CS_SAM_FREQ_CONTROL) {
      // GET Range (Freq)
      struct range4b {
        uint16_t wNumSubRages;
        struct {
          uint32_t dMIN;
          uint32_t dMAX;
          uint32_t dRES;
        } subranges[5];
      } __attribute__((packed));

      static struct range4b ret = {
          .wNumSubRages =
#if PICODAC_EAC3_PASSTHROUGH
              5,
#else
              4,
#endif
          .subranges[0] =
              {
                  .dMIN = 44100,
                  .dMAX = 44100,
                  .dRES = 0,
              },
          .subranges[1] =
              {
                  .dMIN = 48000,
                  .dMAX = 48000,
                  .dRES = 0,
              },
          .subranges[2] =
              {
                  .dMIN = 88200,
                  .dMAX = 88200,
                  .dRES = 0,
              },
          .subranges[3] =
              {
                  .dMIN = 96000,
                  .dMAX = 96000,
                  .dRES = 0,
              },
#if PICODAC_EAC3_PASSTHROUGH
          .subranges[4] =
              {
                  .dMIN = AUDIO_EAC3_RATE,
                  .dMAX = AUDIO_EAC3_RATE,
                  .dRES = 0,
              },
#endif
      };
      usb_ep0_start_transfer((void*)&ret, MIN(pkt->wLength, sizeof(ret)));
      return true;
    }
  }

  LOG_INFO("unhandled class in request: unknown requeset");
  return false;
}

bool usb_audio_control_ouot_request(const struct usb_setup_packet_t* pkt,
                                    const uint8_t* buf, uint16_t len) {
  if (pkt->bmRequestType == 0x21 && pkt->bRequest == UAC2_CS_REQ_CUR &&
      (pkt->wValue >> 8) == UAC2_CS_SAM_FREQ_CONTROL &&
      (pkt->wIndex >> 8) == AUDIO_CONTROL_ID_CLOCK &&
      (pkt->wIndex & 0xFF) == INTERFACE_AUDIO_CONTROL) {
    // 设置采样频率
    if (pkt->wLength != 4 || len != 4) return false;
    uint32_t freq = ((uint32_t)buf[0]) | ((uint32_t)buf[1] << 8) |
                    ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    bool supported = false;
    for (unsigned i = 0; i < N_SAMPLE_RATES; ++i) {
      if (freq == SAMPLE_RATES[i]) supported = true;
    }
    if (!supported) return false;
    const bool carrier_mode_changed = audio_stream_current_alt == 1 &&
        ((audio_device_get_sampling_freq() == AUDIO_EAC3_RATE) !=
         (freq == AUDIO_EAC3_RATE));
    audio_device_set_sampling_freq(freq);
    if (carrier_mode_changed) {
      audio_device_stream_stop();
      usb_audio_start_alt(audio_stream_current_alt);
    }
    return true;
  } else if (pkt->bmRequestType == 0x21 && pkt->bRequest == UAC2_CS_REQ_CUR &&
             (pkt->wValue >> 8) == UAC2_FU_MUTE_CONTROL &&
             (pkt->wIndex >> 8) == AUDIO_CONTROL_ID_FEATURE_UNIT &&
             (pkt->wIndex & 0xFF) == INTERFACE_AUDIO_CONTROL) {
    // 设置静音
    assert(pkt->wLength == 1);
    uint8_t ch = pkt->wValue & 0xFF;
    audio_device_set_mute(ch, buf[0]);
    LOG_DEBUG("set %s, ch(%d)", buf[0] ? "mute" : "unmute", ch);
    return true;
  } else if (pkt->bmRequestType == 0x21 && pkt->bRequest == UAC2_CS_REQ_CUR &&
             (pkt->wValue >> 8) == UAC2_FU_VOLUME_CONTROL &&
             (pkt->wIndex >> 8) == AUDIO_CONTROL_ID_FEATURE_UNIT &&
             (pkt->wIndex & 0xFF) == INTERFACE_AUDIO_CONTROL) {
    // 设置音量
    assert(pkt->wLength == 0x02);
    int16_t vol = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    uint8_t ch = pkt->wValue & 0xFF;
    audio_device_set_volume(ch, vol);
    LOG_DEBUG("set volume %d dB, ch(%d)", vol / 256, ch);
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

void usb_audio_init() {
  usb_device_set_ep_out_handler(EP_AUDIO_STREAM_OUT, ep_audio_out_handler);
  usb_device_set_ep_in_handler(EP_AUDIO_FEEDBACK_IN & 0x7F,
                               ep_audio_in_handler);

  usb_device_set_control_in_handler(INTERFACE_AUDIO_CONTROL,
                                    usb_audio_control_in_request);
  usb_device_set_control_out_handler(INTERFACE_AUDIO_CONTROL,
                                     usb_audio_control_ouot_request);

  usb_device_set_set_interface_handler(INTERFACE_AUDIO_CONTROL,
                                       usb_audio_control_set_interface);
  usb_device_set_set_interface_handler(INTERFACE_AUDIO_STREAM,
                                       usb_audio_stream_set_interface);
}

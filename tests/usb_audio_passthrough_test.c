#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audio_device.h"
#include "audio_diagnostics.h"
#include "spdif.h"
#include "spdif_encode.h"
#include "usb.h"
#include "usb_audio.h"
#include "usb_config.h"

// Exercise real usb_audio.c -> audio_device.c -> ringbuffer.c -> encoder.
// Only hardware output and USB transport are substituted on the host.
static usb_ep_out_handler out_callback;
static usb_device_set_interfacec_handler select_alt;
static usb_control_interface_out_handler control_out;
static usb_control_interface_in_handler control_in;
static uint8_t control_reply[64];
static uint16_t control_length;
static bool running, ready, acquired, non_pcm;
static unsigned depth;
static uint32_t rate;
static int32_t write_buf[SPDIF_BLOCK_FRAMES * 2];
static int32_t capture[40000];
static unsigned captured, non_pcm_blocks, pcm_blocks;
static uint16_t receive_size;

static uint32_t decode_subframe(const uint32_t *sf) {
  uint64_t bits = sf[0] | ((uint64_t)sf[1] << 32);
  uint32_t word = 0;
  unsigned parity = 0;
  for (unsigned i = 4; i < 32; ++i) {
    assert((bits >> (2 * i)) & 1);
    unsigned bit = (bits >> (2 * i + 1)) & 1;
    word |= bit << i;
    parity ^= bit;
  }
  assert(parity == 0);
  return word;
}

void spdif_init(unsigned pin, uint32_t sample_rate, uint8_t bit_depth,
                bool encoded) {
  assert(pin == 22);
  rate = sample_rate;
  depth = bit_depth;
  non_pcm = encoded;
  running = ready = acquired = false;
  spdif_encode_init();
}
void spdif_deinit(void) { running = ready = acquired = false; }
void spdif_start(void) { running = ready = true; }
void spdif_stop(void) { running = ready = acquired = false; }
bool spdif_buffer_ready(void) {
  if (!running || !ready) return false;
  ready = false;
  acquired = true;
  return true;
}
int32_t *spdif_write_buffer(void) { assert(acquired); return write_buf; }
void spdif_submit_buffer(void) {
  assert(acquired);
  acquired = false;
  uint32_t wire[SPDIF_BLOCK_WORDS];
  uint8_t status[2][24] = {{0}};
  spdif_encode_block(wire, write_buf, depth, rate, non_pcm);
  if (non_pcm) ++non_pcm_blocks;
  else ++pcm_blocks;
  for (unsigned i = 0; i < SPDIF_BLOCK_FRAMES * 2; ++i) {
    uint32_t sf = decode_subframe(wire + i * 2);
    assert(((sf >> 28) & 1) == non_pcm);
    assert(((sf >> 29) & 1) == 0);
    unsigned frame = i / 2;
    status[i % 2][frame / 8] |= ((sf >> 30) & 1) << (frame % 8);
    if (non_pcm) {
      // Every bit of the IEC 61937 carrier must survive the complete path.
      assert((sf & 0xff0) == 0);
      assert(((sf >> 12) & 0xffff) == (uint16_t)write_buf[i]);
    }
    assert(captured < sizeof(capture) / sizeof(capture[0]));
    capture[captured++] = write_buf[i];
  }
  assert(status[0][0] == (non_pcm ? 6 : 4));
  assert(status[1][0] == status[0][0]);
}

void blink_set_period_us(uint32_t us) { (void)us; }
void blink_led_on(void) {}
void usb_device_set_ep_out_handler(uint8_t ep, usb_ep_out_handler cb) {
  assert(ep == EP_AUDIO_STREAM_OUT); out_callback = cb;
}
void usb_device_set_ep_in_handler(uint8_t ep, usb_ep_in_handler cb) {
  (void)ep; (void)cb;
}
void usb_device_set_control_in_handler(uint8_t itf, usb_control_interface_in_handler cb) {
  (void)itf; control_in = cb;
}
void usb_device_set_control_out_handler(uint8_t itf, usb_control_interface_out_handler cb) {
  assert(itf == INTERFACE_AUDIO_CONTROL); control_out = cb;
}
void usb_device_set_set_interface_handler(uint8_t itf, usb_device_set_interfacec_handler cb) {
  if (itf == INTERFACE_AUDIO_STREAM) select_alt = cb;
}
void usb_ep_n_start_transfer(uint8_t ep, bool in, const uint8_t *buf, uint16_t len) {
  (void)buf;
  if (!in) { assert(ep == EP_AUDIO_STREAM_OUT); receive_size = len; }
}
void usb_ep0_start_transfer(const uint8_t *buf, uint16_t len) {
  assert(len <= sizeof(control_reply)); memcpy(control_reply, buf, len); control_length = len;
}

#if PICODAC_EAC3_PASSTHROUGH
static uint32_t le32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void check_eac3(void) {
  struct usb_setup_packet_t req = {0xA1, 2, 0x100, 0x400, 64};
  assert(control_in(&req));
  assert(control_length == 62 && control_reply[0] == 5);
  assert(le32(control_reply + 50) == 192000 && le32(control_reply + 54) == 192000);
  req.bRequest = 1;
  assert(control_in(&req) && le32(control_reply) == 48000);
  req.bmRequestType = 0x21; req.wLength = 4;
  uint8_t clock_bytes[] = {0, 0xee, 2, 0};
  assert(control_out(&req, clock_bytes, 4));
  assert(rate == 192000);

  assert(select_alt(0));
  assert(select_alt(AUDIO_ALT_EAC3));
  assert(non_pcm && rate == 192000 && depth == 16);
  // CM6646-style topology shares one clock across all alternate settings.
  uint8_t normal_rate[] = {0x80, 0xbb, 0, 0}; // 48000
  assert(control_out(&req, normal_rate, 4));
  assert(non_pcm && rate == 48000);
  req.bmRequestType = 0xA1;
  assert(control_in(&req) && le32(control_reply) == 48000);
  assert(select_alt(AUDIO_ALT_EAC3));
  // ALSA device 0 exposes the Type I S16 alt. At 192 kHz this is also the
  // raw IEC 61937 carrier path and must disable I2S just like Type III.
  assert(select_alt(0));
  assert(select_alt(1));
  assert(non_pcm && rate == 192000 && depth == 16);
  req.bmRequestType = 0x21;
  assert(control_out(&req, normal_rate, 4));
  assert(!non_pcm && rate == 48000 && depth == 16);
  assert(control_out(&req, clock_bytes, 4));
  assert(non_pcm && rate == 192000 && depth == 16);
  assert(select_alt(0));
  assert(select_alt(AUDIO_ALT_EAC3));
  // Two complete IEC61937 E-AC-3 bursts, followed by padding to drain them.
  static int32_t expected[24576];
  for (unsigned i = 0; i < 24576; ++i) {
    unsigned p = i % 12288;
    uint16_t value = p < 900 ? (uint16_t)(i * 977u) : 0;
    if (p == 0) value = 0xf872;
    if (p == 1) value = 0x4e1f;
    if (p == 2) value = 0x15;
    if (p == 3) value = 1792; // E-AC-3 Pd is bytes, not bits
    expected[i] = (int16_t)value;
  }
  captured = 0;
  for (unsigned sent = 0, packet = 0; sent < 28500; ++packet) {
    unsigned frames = packet % 3 == 0 ? 193 : packet % 3 == 1 ? 191 : 192;
    uint8_t bytes[772];
    for (unsigned i = 0; i < frames * 2; ++i) {
      uint16_t word = sent + i < 24576 ? (uint16_t)expected[sent + i] : 0;
      bytes[2 * i] = word; bytes[2 * i + 1] = word >> 8;
    }
    out_callback(bytes, frames * 4);
    assert(receive_size == 772);
    sent += frames * 2;
    ready = true; audio_device_task();
  }
  assert(captured >= 24576);
  assert(memcmp(expected, capture, sizeof(expected)) == 0);
  uint8_t oversized[776] = {0};
  unsigned bad = usb_audio_bad_packets;
  out_callback(oversized, sizeof(oversized));
  assert(usb_audio_bad_packets == bad + 1);
  assert(select_alt(0));
  req.bmRequestType = 0x21;
  assert(control_out(&req, normal_rate, 4));
  assert(select_alt(2));
  assert(rate == 48000 && !non_pcm && depth == 24);
  assert(select_alt(0));
  puts("PASS: EAC3 192k carrier, shared UAC2 clock, two whole 24576-byte bursts and packet bounds");
}
#endif

static void set_rate(uint32_t frequency) {
  struct usb_setup_packet_t req = {
      .bmRequestType = 0x21, .bRequest = 1, .wValue = 0x100,
      .wIndex = (AUDIO_CONTROL_ID_CLOCK << 8) | INTERFACE_AUDIO_CONTROL,
      .wLength = 4};
  uint8_t bytes[4] = {frequency, frequency >> 8, frequency >> 16, frequency >> 24};
  assert(control_out(&req, bytes, 4));
}

// Bursts span USB packets and SPDIF blocks. Include all 16-bit patterns,
// headers, negative carrier values and padding; no codec decoding is needed.
static void check_passthrough(unsigned alt, unsigned burst_frames, unsigned type,
                               uint32_t frequency) {
  assert(select_alt(0));
  set_rate(frequency);
  assert(select_alt(alt));
  assert(non_pcm && depth == 16 && rate == frequency);
  int32_t expected[12288];
  for (unsigned i = 0; i < 12288; ++i) {
    unsigned position = i % (burst_frames * 2);
    uint16_t word = position > 23 ? 0 : (uint16_t)(i * 977u + 0x8000u);
    if (position == 0) word = 0xf872;
    if (position == 1) word = 0x4e1f;
    if (position == 2) word = type;
    if (position == 3) word = 20 * 16;
    expected[i] = (int16_t)word;
  }
  captured = 0;
  unsigned sent = 0;
  for (unsigned packet = 0; sent < 12288; ++packet) {
    // Alternating sizes deliberately split Pa/Pb from Pc/Pd at boundaries.
    unsigned frames = packet % 3 == 0 ? 47 : 48;
    unsigned count = frames * 2;
    if (count > 12288 - sent) count = 12288 - sent;
    uint8_t bytes[192];
    for (unsigned i = 0; i < count; ++i) {
      uint16_t word = expected[sent + i];
      bytes[i * 2] = word;
      bytes[i * 2 + 1] = word >> 8;
    }
    out_callback(bytes, count * 2);
    assert(receive_size == AUDIO_IEC61937_MAX_PACKET_SIZE);
    sent += count;
    if (packet % 4 == 3) {
      ready = true;
      audio_device_task();
    }
  }
  assert(captured > 8000);
  assert(captured <= sent);
  assert(memcmp(capture, expected, captured * sizeof(int32_t)) == 0);
  assert(!select_alt(AUDIO_ALT_MAX + 1));
  assert(non_pcm && running);

  // Simulated producer starvation must produce zeros, without a PCM switch.
  for (unsigned i = 0; i < 12; ++i) { ready = true; audio_device_task(); }
  assert(non_pcm);
  for (unsigned i = captured - 384; i < captured; ++i) assert(capture[i] == 0);
  set_rate(frequency == 48000 ? 44100 : 48000);
  assert(non_pcm && depth == 16); // Active clock reconfiguration retains mode.
  assert(select_alt(0));
  unsigned before = captured;
  uint8_t late_packet[4] = {0x72, 0xf8, 0x1f, 0x4e};
  out_callback(late_packet, 4);
  ready = true;
  audio_device_task();
  assert(!running && captured == before);
}

static void check_pcm(void) {
  assert(select_alt(1));
  assert(!non_pcm && depth == 16);
  captured = 0;
  uint8_t bytes[192];
  memset(bytes, 0x55, sizeof(bytes));
  for (unsigned i = 0; i < 12; ++i) out_callback(bytes, sizeof(bytes));
  assert(receive_size == AUDIO_MAX_PACKET_SIZE);
  audio_device_task(); // BUFFERING -> PLAYING
  audio_device_task(); // USB mute must still silence normal PCM.
  assert(captured == 384);
  for (unsigned i = 0; i < captured; ++i) assert(capture[i] == 0);
  assert(select_alt(2));
  assert(!non_pcm && depth == 24);
  assert(select_alt(3));
  assert(!non_pcm && depth == 32);
}

int main(void) {
  audio_device_init();
  usb_audio_init();
  // Deliberately set controls that would corrupt encoded data if applied.
  audio_device_set_volume(0, -24 * 256);
  audio_device_set_volume(1, -12 * 256);
  audio_device_set_volume(2, -6 * 256);
  audio_device_set_mute(0, true);
  audio_device_set_mute(1, true);
  audio_device_set_mute(2, true);
  const unsigned periods[] = {1536, 512, 1024, 2048};
  const unsigned types[] = {1, 11, 12, 13};
  const uint32_t rates[] = {44100, 48000, 88200, 96000};
  for (unsigned r = 0; r < 4; ++r) {
    for (unsigned i = 0; i < 4; ++i) {
      check_passthrough(AUDIO_ALT_AC3 + i, periods[i], types[i], rates[r]);
    }
  }
  check_pcm();
#if PICODAC_EAC3_PASSTHROUGH
  check_eac3();
#endif
  assert(non_pcm_blocks && pcm_blocks);
  puts("PASS: USB -> ring buffer -> SPDIF bit-exact AC3/DTS, controls bypass, starvation, format/rate switches");
}

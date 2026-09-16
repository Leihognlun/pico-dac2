#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audio_device.h"
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
static uint8_t response[64];
static uint16_t response_len;
static bool running, ready, acquired, non_pcm;
static unsigned depth;
static uint32_t rate;
static int32_t write_buf[SPDIF_BLOCK_FRAMES * 2];
static int32_t capture[20000];
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
  assert(itf == INTERFACE_AUDIO_CONTROL); control_in = cb;
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
  assert(len <= sizeof(response));
  memcpy(response, buf, len); response_len = len;
}

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
    unsigned frames = frequency == 192000 ? (packet % 3 == 0 ? 193 : 192) :
                      (packet % 3 == 0 ? 47 : 48);
    unsigned count = frames * 2;
    if (count > 12288 - sent) count = 12288 - sent;
    uint8_t bytes[AUDIO_IEC61937_MAX_PACKET_SIZE];
    for (unsigned i = 0; i < count; ++i) {
      uint16_t word = expected[sent + i];
      bytes[i * 2] = word;
      bytes[i * 2 + 1] = word >> 8;
    }
    out_callback(bytes, count * 2);
    assert(receive_size == AUDIO_IEC61937_MAX_PACKET_SIZE);
    sent += count;
    if (frequency == 192000 || packet % 4 == 3) {
      ready = true;
      audio_device_task();
    }
  }
  assert(captured > (frequency == 192000 ? 6000u : 8000u));
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
  set_rate(48000);
  assert(select_alt(1));
  assert(!non_pcm && depth == 16);
  captured = 0;
  uint8_t bytes[192];
  memset(bytes, 0x55, sizeof(bytes));
  for (unsigned i = 0; i < 12; ++i) out_callback(bytes, sizeof(bytes));
  assert(receive_size == AUDIO_PCM16_MAX_PACKET_SIZE);
  audio_device_task(); // BUFFERING -> PLAYING
  audio_device_task(); // USB mute must still silence normal PCM.
  assert(captured == 384);
  for (unsigned i = 0; i < captured; ++i) assert(capture[i] == 0);
  assert(select_alt(2));
  assert(!non_pcm && depth == 24);
  assert(select_alt(3));
  assert(!non_pcm && depth == 32);
}

static uint32_t read_le32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void check_clocks(void) {
  struct usb_setup_packet_t req = {
      .bmRequestType = 0xa1, .bRequest = 2, .wValue = 0x100,
      .wIndex = (AUDIO_CONTROL_ID_CLOCK << 8) | INTERFACE_AUDIO_CONTROL,
      .wLength = 64};
  assert(control_in(&req));
  assert(response_len == 62 && response[0] == 5 && response[1] == 0);
  const uint32_t expected[] = {44100, 48000, 88200, 96000, 192000};
  for (unsigned i = 0; i < response[0]; ++i) {
    assert(read_le32(response + 2 + i * 12) == expected[i]);
    assert(read_le32(response + 6 + i * 12) == expected[i]);
    assert(read_le32(response + 10 + i * 12) == 0);
  }
  req.wLength = 2;
  assert(control_in(&req) && response_len == 2);
  req.bmRequestType = 0x21; req.bRequest = 1; req.wLength = 4;
  const uint8_t bytes[] = {0x00, 0xee, 0x02, 0x00}; // 192000
  for (unsigned alt = 1; alt <= 3; ++alt) {
    assert(select_alt(alt));
    assert(!control_out(&req, bytes, 4));
    assert(audio_device_get_sampling_freq() == 48000 && !non_pcm);
  }
  assert(select_alt(0));
  assert(control_out(&req, bytes, 4)); // Set rate before selecting a format.
  assert(!control_out(&req, bytes, 3));
  req.bmRequestType = 0xa1;
  assert(control_in(&req) && response_len == 4);
  assert(read_le32(response) == 192000);
  for (unsigned alt = 4; alt <= 7; ++alt) {
    assert(select_alt(alt) && rate == 192000 && non_pcm);
    for (unsigned pcm_alt = 1; pcm_alt <= 3; ++pcm_alt) {
      assert(!usb_audio_stream_can_set_interface(pcm_alt));
      assert(!select_alt(pcm_alt));
      assert(rate == 192000 && non_pcm);
    }
    set_rate(96000);
    set_rate(192000); // Active rate changes work for every Type III format.
  }
  set_rate(96000);
  assert(select_alt(2) && rate == 96000 && !non_pcm);
  assert(select_alt(0));
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
  const uint32_t rates[] = {44100, 48000, 88200, 96000, 192000};
  for (unsigned r = 0; r < 5; ++r) {
    for (unsigned i = 0; i < 4; ++i) {
      check_passthrough(AUDIO_ALT_AC3 + i, periods[i], types[i], rates[r]);
    }
  }
  check_pcm();
  check_clocks();
  assert(non_pcm_blocks && pcm_blocks);
  puts("PASS: USB -> ring buffer -> SPDIF bit-exact AC3/DTS, controls bypass, starvation, format/rate switches");
}

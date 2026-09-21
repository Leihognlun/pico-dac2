#include "wav_reader.h"
#include <stdbool.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static int exact(wav_reader_t *r, uint8_t *p, size_t n) {
  size_t done = 0;
  while (done < n) {
    int got = r->read(r->ctx, p + done, n - done);
    if (got < 0 || (size_t)got > n - done) return WAV_IO_ERROR;
    if (!got) return WAV_TRUNCATED;
    done += (size_t)got;
  }
  return 1;
}
static int skip(wav_reader_t *r, uint32_t n) {
  uint8_t scratch[64];
  while (n) {
    unsigned take = n > sizeof(scratch) ? sizeof(scratch) : n;
    int rc = exact(r, scratch, take); if (rc < 0) return rc; n -= take;
  }
  return 1;
}
int wav_reader_open(wav_reader_t *r, wav_read_fn read, void *ctx) {
  memset(r, 0, sizeof(*r)); r->read = read; r->ctx = ctx;
  uint8_t h[12]; if (exact(r, h, sizeof(h)) < 0) return WAV_TRUNCATED;
  if (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return WAV_BAD_FORMAT;
  bool have_fmt = false;
  for (;;) {
    uint8_t ch[8]; int rc = exact(r, ch, sizeof(ch)); if (rc < 0) return rc;
    uint32_t size = le32(ch + 4);
    if (!memcmp(ch, "fmt ", 4)) {
      if (size < 16) return WAV_BAD_FORMAT;
      uint8_t fmt[40] = {0};
      unsigned take = size < sizeof(fmt) ? size : sizeof(fmt);
      if (exact(r, fmt, take) < 0) return WAV_TRUNCATED;
      uint16_t tag = le16(fmt), align = le16(fmt + 12);
      r->channels = le16(fmt + 2); r->sample_rate = le32(fmt + 4); r->bits = le16(fmt + 14);
      bool pcm = tag == 1;
      if (tag == 0xfffe && size >= 40 && le16(fmt + 16) >= 22) {
        static const uint8_t pcm_guid[16] = {
            1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
        // WAVE_FORMAT_EXTENSIBLE: accept only integer PCM whose valid bits
        // match the packed container. Channel mask 0x3 is normal stereo.
        pcm = le16(fmt + 18) == r->bits && !memcmp(fmt + 24, pcm_guid, 16);
      }
      if (!pcm || r->channels != 2 || (r->bits != 16 && r->bits != 24) ||
          align != r->channels * r->bits / 8 ||
          (r->sample_rate != 44100 && r->sample_rate != 48000 && r->sample_rate != 96000))
        return WAV_UNSUPPORTED;
      if (skip(r, size - take + (size & 1)) < 0) return WAV_TRUNCATED;
      have_fmt = true;
    } else if (!memcmp(ch, "data", 4)) {
      if (!have_fmt) return WAV_BAD_FORMAT;
      if (size % (r->channels * r->bits / 8)) return WAV_TRUNCATED;
      r->data_remaining = size; return WAV_BLOCK_READY;
    } else if (skip(r, size + (size & 1)) < 0) return WAV_TRUNCATED;
  }
}
int wav_next_block(wav_reader_t *r, int32_t out[384], unsigned *frames) {
  unsigned frame_bytes = r->channels * r->bits / 8;
  if (!r->data_remaining) { *frames = 0; return WAV_EOF; }
  unsigned count = r->data_remaining / frame_bytes;
  if (count > 192) count = 192;
  uint8_t raw[192 * 6]; unsigned bytes = count * frame_bytes;
  if (exact(r, raw, bytes) < 0) return WAV_TRUNCATED;
  r->data_remaining -= bytes;
  for (unsigned i = 0; i < count * 2; ++i) {
    const uint8_t *p = raw + i * (r->bits / 8);
    if (r->bits == 16) out[i] = (int16_t)le16(p);
    else out[i] = (int32_t)(((uint32_t)p[0] | (uint32_t)p[1] << 8 |
                             (uint32_t)p[2] << 16) << 8) >> 8;
  }
  memset(out + count * 2, 0, (192 - count) * 2 * sizeof(*out));
  *frames = count; return WAV_BLOCK_READY;
}
const char *wav_result_string(int r) {
  switch (r) { case WAV_EOF:return "end of file"; case WAV_BLOCK_READY:return "block ready";
    case WAV_IO_ERROR:return "TF read error"; case WAV_TRUNCATED:return "truncated WAV";
    case WAV_BAD_FORMAT:return "invalid RIFF/WAVE"; case WAV_UNSUPPORTED:return "requires stereo PCM WAV, 44.1/48/96 kHz, 16/24-bit";
    default:return "unknown WAV error"; }
}

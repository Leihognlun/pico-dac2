#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eac3_burst.h"
#include "spdif_encode.h"

static uint8_t input[65536];
static size_t length, position, chunk;
static int fail_at;
static uint16_t out[EAC3_BURST_WORDS];

static int read_bytes(void *ctx, uint8_t *dst, size_t size) {
  assert(ctx == input);
  if (fail_at >= 0 && position >= (size_t)fail_at) return -1;
  if (size > length - position) size = length - position;
  if (size > chunk) size = chunk;
  memcpy(dst, input + position, size);
  position += size;
  return (int)size;
}

static void reset(eac3_reader_t *r) {
  length = position = 0; chunk = 7; fail_at = -1;
  eac3_reader_init(r, read_bytes, input);
}

static void frame(unsigned type, unsigned block_code, unsigned size) {
  assert(size >= 8 && size <= 4096 && size % 2 == 0);
  assert(length + size <= sizeof(input));
  uint8_t *p = input + length;
  for (unsigned i = 0; i < size; ++i) p[i] = (uint8_t)(i * 173 + length);
  p[0] = 0x0b; p[1] = 0x77;
  p[2] = (type << 6) | ((size / 2 - 1) >> 8);
  p[3] = (uint8_t)(size / 2 - 1);
  p[4] = (block_code << 4) | 4; // 48 kHz, stereo
  p[5] = 16 << 3; // E-AC-3 bsid
  length += size;
}

static void check_burst(size_t start, size_t size) {
  assert(out[0] == 0xf872 && out[1] == 0x4e1f && out[2] == 0x15);
  assert(out[3] == size); // bytes, not bits
  for (size_t i = 0; i < size / 2; ++i)
    assert(out[4 + i] == ((unsigned)input[start + 2 * i] << 8 | input[start + 2 * i + 1]));
  for (size_t i = 4 + size / 2; i < EAC3_BURST_WORDS; ++i) assert(out[i] == 0);

  // Exercise actual SPDIF encoding, including payload order at block borders.
  int32_t samples[SPDIF_BLOCK_FRAMES * 2];
  uint32_t encoded[SPDIF_BLOCK_WORDS];
  for (unsigned offset = 0; offset < EAC3_BURST_WORDS; offset += 384) {
    for (unsigned i = 0; i < 384; ++i) samples[i] = (int16_t)out[offset + i];
    spdif_encode_block(encoded, samples, 16, EAC3_CARRIER_RATE, true);
    for (unsigned i = 0; i < 384; ++i) {
      uint64_t bmc = encoded[2 * i] | ((uint64_t)encoded[2 * i + 1] << 32);
      uint32_t sf = 0;
      for (unsigned b = 4; b < 32; ++b) sf |= ((bmc >> (2 * b + 1)) & 1) << b;
      assert(((sf >> 12) & 0xffff) == out[offset + i]);
      assert(sf & (1u << 28));
    }
  }
}

int main(void) {
  eac3_reader_t r;
  spdif_encode_init();
  const unsigned counts[] = {6, 3, 2, 1};
  for (unsigned code = 0; code < 4; ++code) {
    reset(&r);
    for (unsigned i = 0; i < counts[code]; ++i) {
      frame(0, code, 512);
      frame(1, code, 256); // dependent payload/metadata must survive
    }
    size_t first = length;
    frame(0, 3, 1024);
    frame(1, 3, 128);
    assert(eac3_next_burst(&r, out) == EAC3_BURST_READY);
    check_burst(0, first);
    assert(eac3_next_burst(&r, out) == EAC3_BURST_READY);
    check_burst(first, length - first);
    assert(eac3_next_burst(&r, out) == EAC3_EOF);
    assert(eac3_next_burst(&r, out) == EAC3_EOF);
  }
  reset(&r); assert(eac3_next_burst(&r, out) == EAC3_EOF);
  reset(&r); frame(2, 3, 128); // E-AC-3 converted from AC-3
  chunk = 1;
  assert(eac3_next_burst(&r, out) == EAC3_BURST_READY); check_burst(0, length);
  reset(&r); frame(0, 3, 512); --length;
  assert(eac3_next_burst(&r, out) == EAC3_TRUNCATED);
  assert(eac3_next_burst(&r, out) == EAC3_TRUNCATED);
  reset(&r); frame(0, 0, 512);
  assert(eac3_next_burst(&r, out) == EAC3_TRUNCATED);
  reset(&r); frame(0, 3, 512); length = 3;
  assert(eac3_next_burst(&r, out) == EAC3_TRUNCATED);
  reset(&r); frame(0, 3, 512); input[0] = 0;
  assert(eac3_next_burst(&r, out) == EAC3_BAD_HEADER);
  reset(&r); frame(0, 3, 512); input[5] = 8 << 3;
  assert(eac3_next_burst(&r, out) == EAC3_UNSUPPORTED); // AC-3
  reset(&r); frame(0, 3, 512); input[4] |= 0x40;
  assert(eac3_next_burst(&r, out) == EAC3_UNSUPPORTED); // 44.1 kHz
  reset(&r); frame(0, 3, 512); input[2] |= 8;
  assert(eac3_next_burst(&r, out) == EAC3_UNSUPPORTED); // other program
  reset(&r); frame(1, 3, 512);
  assert(eac3_next_burst(&r, out) == EAC3_BAD_HEADER); // orphan dependent
  reset(&r); frame(0, 0, 512); frame(1, 3, 512);
  assert(eac3_next_burst(&r, out) == EAC3_BAD_HEADER);
  reset(&r); frame(0, 2, 512); frame(0, 3, 512);
  assert(eac3_next_burst(&r, out) == EAC3_BAD_HEADER); // too many blocks
  reset(&r); frame(0, 3, 4096);
  for (unsigned i = 0; i < 5; ++i) frame(1, 3, 4096);
  assert(eac3_next_burst(&r, out) == EAC3_OVERFLOW);
  reset(&r); frame(0, 3, 512); fail_at = 10;
  assert(eac3_next_burst(&r, out) == EAC3_IO_ERROR);
  puts("PASS: raw EAC3 grouping/dependents, IEC61937 byte length/order/padding, SPDIF roundtrip, malformed input");
}

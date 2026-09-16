#include "eac3_burst.h"

#include <string.h>

static int read_exact(eac3_reader_t *r, uint8_t *dst, size_t size) {
  size_t done = 0;
  while (done < size) {
    int n = r->read(r->ctx, dst + done, size - done);
    if (n < 0 || (size_t)n > size - done) return EAC3_IO_ERROR;
    if (!n) return done ? EAC3_TRUNCATED : EAC3_EOF;
    done += (size_t)n;
  }
  return EAC3_BURST_READY;
}

void eac3_reader_init(eac3_reader_t *r, eac3_read_fn read, void *ctx) {
  memset(r, 0, sizeof(*r));
  r->read = read;
  r->ctx = ctx;
  r->terminal = EAC3_BURST_READY;
}

int eac3_next_burst(eac3_reader_t *r, uint16_t out[EAC3_BURST_WORDS]) {
  if (r->terminal <= 0) return r->terminal;
  unsigned blocks = 0, last_blocks = 0;
  size_t payload = 0;
  uint8_t *bytes = (uint8_t *)(out + 4);
  int result;
  memset(out, 0, EAC3_BURST_BYTES);
  for (;;) {
    if (!r->pending) {
      result = read_exact(r, r->header, sizeof(r->header));
      if (result == EAC3_EOF) {
        r->terminal = EAC3_EOF;
        if (blocks == 6) break;
        if (!blocks) return EAC3_EOF;
        result = EAC3_TRUNCATED;
        goto fail;
      }
      if (result < 0) goto fail;
      r->pending = true;
    }
    const uint8_t *h = r->header;
    unsigned type = h[2] >> 6, id = (h[2] >> 3) & 7;
    unsigned bsid = h[5] >> 3;
    size_t size = 2u * ((((unsigned)h[2] & 7) << 8 | h[3]) + 1);
    if (h[0] != 0x0b || h[1] != 0x77 || type == 3 || size < 8 || bsid > 16) {
      result = EAC3_BAD_HEADER;
      goto fail;
    }
    // 48 kHz only: no resampling or metadata rewriting on a passthrough path.
    if (bsid <= 10 || (h[4] >> 6) != 0 || id != 0) {
      result = EAC3_UNSUPPORTED;
      goto fail;
    }
    const unsigned block_counts[] = {1, 2, 3, 6};
    unsigned count = block_counts[(h[4] >> 4) & 3];
    if (type != 1) {
      // Look ahead to retain dependent frames belonging to the final frame.
      if (blocks == 6) break;
      if (blocks + count > 6) {
        result = EAC3_BAD_HEADER;
        goto fail;
      }
      blocks += count;
      last_blocks = count;
    } else if (!blocks || count != last_blocks) {
      result = EAC3_BAD_HEADER;
      goto fail;
    }
    if (size > EAC3_BURST_BYTES - 8 - payload) {
      result = EAC3_OVERFLOW;
      goto fail;
    }
    memcpy(bytes + payload, h, sizeof(r->header));
    result = read_exact(r, bytes + payload + sizeof(r->header), size - sizeof(r->header));
    if (result != EAC3_BURST_READY) {
      if (result == EAC3_EOF) result = EAC3_TRUNCATED;
      goto fail;
    }
    payload += size;
    r->pending = false;
  }
  // E-AC-3 Pd is a BYTE count, unlike AC-3's bit count. Preserve all payload
  // bytes (including dependent-substream/Atmos metadata) in transmitted order.
  for (size_t i = 0; i < payload / 2; ++i) {
    uint16_t word = ((uint16_t)bytes[2 * i] << 8) | bytes[2 * i + 1];
    out[4 + i] = word;
  }
  out[0] = 0xf872; out[1] = 0x4e1f;
  out[2] = 0x0015; out[3] = (uint16_t)payload;
  return EAC3_BURST_READY;
fail:
  r->terminal = result;
  return result;
}

const char *eac3_result_string(int result) {
  switch (result) {
    case EAC3_EOF: return "end of file";
    case EAC3_BURST_READY: return "burst ready";
    case EAC3_IO_ERROR: return "TF read error";
    case EAC3_TRUNCATED: return "truncated frame or incomplete six-block frame set";
    case EAC3_BAD_HEADER: return "invalid E-AC-3 frame/header sequence";
    case EAC3_UNSUPPORTED: return "requires raw 48 kHz E-AC-3, substream ID 0";
    case EAC3_OVERFLOW: return "frame set exceeds IEC 61937 burst capacity";
    default: return "unknown error";
  }
}

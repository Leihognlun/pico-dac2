#include "cec_sad.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static const char *const names[15] = {
  "reserved", "LPCM", "AC-3", "MPEG-1", "MP3", "MPEG-2", "AAC-LC",
  "DTS", "ATRAC", "One-bit audio", "E-AC-3", "DTS-HD", "MAT",
  "DST", "WMA-Pro"
};
void cec_sad_describe(const uint8_t d[3], char *out, size_t size) {
  static const char *const rates[] = {"32", "44.1", "48", "88.2", "96", "176.4", "192"};
  unsigned code = (d[0] >> 3) & 15u;
  char hz[48] = "", extra[64];
  for (unsigned i = 0; i < 7; ++i) if (d[1] & (1u << i)) {
    if (*hz) strcat(hz, ",");
    strcat(hz, rates[i]);
  }
  if (code == 1)
    snprintf(extra, sizeof(extra), "bits=%s%s%s PCM44.1=%s",
             d[2] & 1 ? "16," : "", d[2] & 2 ? "20," : "",
             d[2] & 4 ? "24" : "", d[1] & 2 ? "yes" : "no");
  else if (code >= 2 && code <= 8)
    snprintf(extra, sizeof(extra), "max_kbps=%u", d[2] * 8u);
  else
    snprintf(extra, sizeof(extra), "format_specific=0x%02X", d[2]);
  snprintf(out, size, "raw=%02X %02X %02X format=%u(%s) max_channels=%u rates_kHz=%s %s",
           d[0], d[1], d[2], code, code < 15 ? names[code] : "extended",
           (d[0] & 7u) + 1, *hz ? hz : "none", extra);
}
void cec_sad_init(cec_sad_t *s, unsigned port) {
  memset(s, 0, sizeof(*s)); s->port = (uint8_t)port; s->codec = 1;
}
static void advance(cec_sad_t *s, uint32_t now) {
  s->stage = 0; s->tries = 0; ++s->codec; s->due = now + 300000u;
  if (s->codec == 15) {
    LOG_INFO("CEC ARC%u SAD complete: reported_mask=0x%04X (base formats 1..14; missing/timeout is not proof of unsupported)",
             s->port + 1u, s->reported);
  }
}
void cec_sad_task(cec_sad_t *s, bool ready, cec_send_fn send, void *ctx, uint32_t now) {
  if (!ready) { if (s->active) cec_sad_init(s, s->port); return; }
  if (!s->active) { s->active = true; s->due = now + 1000000u; }
  if (s->codec > 14 || s->stage == 1 || (int32_t)(now - s->due) < 0) return;
  if (s->stage == 2) {
    LOG_INFO("CEC ARC%u SAD format=%u timeout (unknown support)", s->port + 1u, s->codec);
    if (s->tries >= 2) { advance(s, now); return; }
    s->stage = 0;
  }
  // CEA-861 format ID=0, one code per request, so a Feature Abort is unambiguous.
  cec_frame_t f = {.data = {0x05, 0xa4, s->codec}, .len = 3,
                   .tag = (uint8_t)(CEC_SAD_TAG_BASE + s->codec)};
  if (send(ctx, &f)) { s->stage = 1; ++s->tries; }
}
void cec_sad_tx_result(cec_sad_t *s, unsigned tag, unsigned result, uint32_t now) {
  if (!s->active || s->stage != 1 || tag != CEC_SAD_TAG_BASE + s->codec) return;
  if (result == CEC_TX_OK) { s->stage = 2; s->due = now + 2000000u; }
  else {
    LOG_INFO("CEC ARC%u SAD format=%u TX failed=%u (unknown support)",
             s->port + 1u, s->codec, result);
    advance(s, now);
  }
}
bool cec_sad_receive(cec_sad_t *s, const cec_frame_t *f, uint32_t now) {
  if (f->len < 2 || f->len > 16 || f->data[0] != 0x50) return false;
  if (f->data[1] == 0xa3) {
    if (!s->active || s->stage != 2) return true;
    unsigned n = f->len - 2;
    if (!n || n > 12 || n % 3) {
      LOG_INFO("CEC ARC%u SAD malformed report len=%u", s->port + 1u, n);
      return true;
    }
    // Validate the whole message before accepting any descriptors.
    for (unsigned i = 2; i < f->len; i += 3)
      if ((f->data[i] & 0x80) || (f->data[i + 1] & 0x80) ||
          ((f->data[i] >> 3) & 15u) != s->codec) return true;
    for (unsigned i = 2; i < f->len; i += 3) {
      char text[192];
      memcpy(s->descriptors[s->codec - 1], f->data + i, 3);
      s->reported |= (uint16_t)(1u << s->codec);
      cec_sad_describe(f->data + i, text, sizeof(text));
      LOG_INFO("CEC ARC%u SAD %s", s->port + 1u, text);
    }
    advance(s, now);
    return true;
  }
  if (f->data[1] == 0 && f->len == 4 && f->data[2] == 0xa4) {
    if (s->active && s->stage == 2) {
      unsigned reason = f->data[3];
      LOG_INFO("CEC ARC%u SAD format=%u Feature Abort reason=%u (0=unrecognized,1=mode,3=invalid operand,4=refused)",
               s->port + 1u, s->codec, reason);
      if (reason == 0) {
        // Receiver does not implement this query; do not send another 13 requests.
        s->codec = 14;
      }
      advance(s, now);
    }
    return true;
  }
  return false;
}

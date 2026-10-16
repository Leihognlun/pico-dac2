#pragma once
#include "cec_tv.h"
#include <stddef.h>

// Tags are distinct from ARC/address-registration traffic.
#define CEC_SAD_TAG_BASE 0x40u
typedef struct {
  uint8_t codec, stage, tries, port;
  bool active;
  uint32_t due;
  uint16_t reported;
  uint8_t descriptors[14][3];
} cec_sad_t;
void cec_sad_init(cec_sad_t *s, unsigned port);
void cec_sad_task(cec_sad_t *s, bool ready, cec_send_fn send, void *ctx, uint32_t now);
void cec_sad_tx_result(cec_sad_t *s, unsigned tag, unsigned result, uint32_t now);
// True means this is SAD diagnostic traffic and must not be Feature-Aborted by the TV.
bool cec_sad_receive(cec_sad_t *s, const cec_frame_t *f, uint32_t now);
void cec_sad_describe(const uint8_t sad[3], char *out, size_t size);

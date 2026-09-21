#pragma once
#include "cec_wire.h"
typedef bool (*cec_send_fn)(void *ctx, const cec_frame_t *frame);
enum { CEC_ARC_OFF, CEC_ARC_REQUESTED, CEC_ARC_REPORTING, CEC_ARC_ON, CEC_ARC_STOPPING };
enum {
  CEC_TAG_NORMAL, CEC_TAG_POLL_TV, CEC_TAG_POLL_AUDIO,
  CEC_TAG_ENABLE, CEC_TAG_DISABLE
};
typedef struct {
  cec_send_fn send; void *ctx;
  bool registered, conflict, polling, soundbar_present, soundbar_polling;
  bool desired, system_audio, muted;
  uint8_t arc, volume, key, sent_key;
  uint32_t deadline, retry_at, key_at;
} cec_tv_t;
void cec_tv_init(cec_tv_t *tv, cec_send_fn send, void *ctx, uint32_t now);
void cec_tv_task(cec_tv_t *tv, uint32_t now);
void cec_tv_receive(cec_tv_t *tv, const cec_frame_t *frame, uint32_t now);
void cec_tv_tx_result(cec_tv_t *tv, uint8_t tag, unsigned result, uint32_t now);
void cec_tv_request_arc(cec_tv_t *tv, bool on, uint32_t now);

#include "cec_tv.h"
#include <string.h>

static bool send(cec_tv_t *t, uint8_t dest, uint8_t op, const uint8_t *args,
                 unsigned count, uint8_t tag) {
  cec_frame_t f = {.len = (uint8_t)(count + 2), .tag = tag};
  f.data[0] = dest; f.data[1] = op;
  if (count) memcpy(f.data + 2, args, count);
  return t->send(t->ctx, &f);
}
static void abort_msg(cec_tv_t *t, unsigned dest, unsigned opcode, unsigned reason) {
  uint8_t args[] = {opcode, reason};
  send(t, dest, 0x00, args, 2, 0);
}
void cec_tv_init(cec_tv_t *t, cec_send_fn fn, void *ctx, uint32_t now) {
  memset(t, 0, sizeof(*t)); t->send = fn; t->ctx = ctx;
  t->desired = true; t->volume = 127; t->retry_at = now + 1000000;
}
void cec_tv_request_arc(cec_tv_t *t, bool on, uint32_t now) {
  t->desired = on; t->retry_at = now;
  if (!on) { t->arc = CEC_ARC_STOPPING; t->deadline = now + 4000000; }
  else if (t->arc == CEC_ARC_STOPPING) t->arc = CEC_ARC_OFF;
}
void cec_tv_task(cec_tv_t *t, uint32_t now) {
  if (t->conflict) return;
  if (!t->registered) {
    if (!t->polling && (int32_t)(now - t->retry_at) >= 0) {
      cec_frame_t poll = {.data = {0x00}, .len = 1, .tag = CEC_TAG_POLL_TV};
      if (t->send(t->ctx, &poll)) t->polling = true;
    }
    return;
  }
  if ((t->arc == CEC_ARC_REQUESTED || t->arc == CEC_ARC_REPORTING ||
       t->arc == CEC_ARC_STOPPING) && (int32_t)(now - t->deadline) >= 0) {
    t->arc = CEC_ARC_OFF; t->retry_at = now + 5000000;
  }
  if ((int32_t)(now - t->retry_at) >= 0) {
    if (t->desired && t->arc == CEC_ARC_OFF) {
      uint8_t physical[] = {0, 0};
      send(t, 5, 0x70, physical, 2, 0); // System Audio Mode Request
      if (send(t, 5, 0xc3, NULL, 0, 0)) {
        t->arc = CEC_ARC_REQUESTED; t->deadline = now + 4000000;
      }
    } else if (!t->desired && t->arc == CEC_ARC_STOPPING) {
      send(t, 5, 0xc4, NULL, 0, 0);
      send(t, 5, 0x70, NULL, 0, 0); // no physical address = System Audio off
      t->retry_at = now + 5000000;
    }
  }
  if (t->sent_key && t->key != t->sent_key) {
    if (send(t, 5, 0x45, NULL, 0, 0)) t->sent_key = 0;
  } else if (t->key && (!t->sent_key || now - t->key_at >= 300000)) {
    if (send(t, 5, 0x44, &t->key, 1, 0)) { t->sent_key = t->key; t->key_at = now; }
  }
}
void cec_tv_tx_result(cec_tv_t *t, uint8_t tag, unsigned result, uint32_t now) {
  if (tag == CEC_TAG_POLL_TV) {
    t->polling = false;
    if (result == CEC_TX_OK) { t->conflict = true; t->arc = CEC_ARC_OFF; }
    else if (result == CEC_TX_NACK) {
      t->registered = true; t->retry_at = now;
      const uint8_t physical[] = {0, 0, 0};
      send(t, 15, 0x84, physical, 3, 0);
    } else t->retry_at = now + 2000000;
  } else if (tag == CEC_TAG_ENABLE && t->arc == CEC_ARC_REPORTING) {
    t->arc = result == CEC_TX_OK && t->desired ? CEC_ARC_ON : CEC_ARC_OFF;
    t->retry_at = now + 5000000;
  } else if (tag == CEC_TAG_DISABLE && t->arc == CEC_ARC_STOPPING) {
    t->arc = CEC_ARC_OFF; t->retry_at = now + 5000000;
  }
}
void cec_tv_receive(cec_tv_t *t, const cec_frame_t *f, uint32_t now) {
  if (!t->registered || f->len < 2 || f->len > 16) return;
  unsigned src = f->data[0] >> 4, dst = f->data[0] & 15, op = f->data[1];
  if (src == 0 || (dst != 0 && dst != 15)) return;
  const uint8_t *args = f->data + 2;
  unsigned n = f->len - 2;
  // ARC control is directed and only accepted from the Audio System.
  if (op >= 0xc0 && op <= 0xc5) {
    if (src != 5 || dst != 0) return;
    if (n) { abort_msg(t, src, op, 3); return; }
    if (op == 0xc0) {
      if (!t->desired) { abort_msg(t, src, op, 4); return; }
      if (send(t, 5, 0xc1, NULL, 0, CEC_TAG_ENABLE)) {
        // A repeated initiation must not interrupt an already active carrier.
        if (t->arc != CEC_ARC_ON) t->arc = CEC_ARC_REPORTING;
        t->deadline = now + 4000000;
      }
    } else if (op == 0xc5) {
      t->arc = CEC_ARC_STOPPING; t->desired = false; t->deadline = now + 4000000;
      send(t, 5, 0xc2, NULL, 0, CEC_TAG_DISABLE);
    } else abort_msg(t, src, op, 4); // C3/C4 belong to TV -> Audio System
    return;
  }
  switch (op) {
    case 0x83: { // Give Physical Address
      if (n || dst == 15) break;
      const uint8_t physical[] = {0, 0, 0}; send(t, 15, 0x84, physical, 3, 0); return;
    }
    case 0x46: {
      if (n || dst == 15) break;
      const uint8_t name[] = "Pico ARC TV"; send(t, src, 0x47, name, sizeof(name) - 1, 0); return;
    }
    case 0x9f: {
      if (n || dst == 15) break;
      const uint8_t version = 5; send(t, src, 0x9e, &version, 1, 0); return; // CEC 1.4
    }
    case 0x8f: {
      if (n || dst == 15) break;
      const uint8_t power = 0; send(t, src, 0x90, &power, 1, 0); return;
    }
    case 0x36:
      if (n) break;
      cec_tv_request_arc(t, false, now); return;
    case 0x72: // Set System Audio Mode, directed or broadcast from Soundbar
    case 0x7e:
      if (src != 5) return;
      if (n != 1 || args[0] > 1) break;
      t->system_audio = args[0];
      if (!args[0]) cec_tv_request_arc(t, false, now);
      return;
    case 0x7a:
      if (src != 5 || dst != 0) return;
      if (n != 1) break;
      t->muted = !!(args[0] & 0x80); t->volume = args[0] & 0x7f; return;
    case 0x00:
      if (src == 5 && n == 2 && (args[0] == 0xc3 || args[0] == 0xc1)) {
        t->arc = CEC_ARC_OFF; t->desired = false;
      }
      return; // never abort a Feature Abort
    case 0x84: case 0x87: case 0x82: case 0x90: return; // discovery/status broadcasts
    default:
      if (dst != 15) abort_msg(t, src, op, 0);
      return;
  }
  if (dst != 15) abort_msg(t, src, op, 3);
}

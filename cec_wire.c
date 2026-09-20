#include "cec_wire.h"
#include <string.h>

void cec_wire_init(cec_wire_t *w, uint32_t now) {
  memset(w, 0, sizeof(*w));
  w->address = 15; w->previous = true; w->last_edge = now;
}
bool cec_wire_send(cec_wire_t *w, const cec_frame_t *f, uint32_t now) {
  if (w->tx_pending || w->tx_result || !f->len || f->len > 16) return false;
  w->tx = *f; w->tx_pending = true; w->requested = now;
  return true;
}
static void finish(cec_wire_t *w, unsigned result, uint32_t now) {
  w->low = false; w->tx_state = 0; w->tx_pending = false;
  w->tx_result = result; w->receiving = false; w->have_fall = false;
  w->last_edge = now;
}
static void start_bit(cec_wire_t *w, uint32_t now) {
  w->tx_start = now; w->low = true;
  w->tx_one = w->tx_bit < 8 ?
      !!(w->tx.data[w->tx_byte] & (0x80u >> w->tx_bit)) :
      w->tx_bit == 8 ? w->tx_byte + 1 == w->tx.len : true;
  w->tx_due = now + (w->tx_one ? 600 : 1500);
  w->tx_state = 3;
}
void cec_wire_tick(cec_wire_t *w, bool high, uint32_t now) {
  bool fall = w->previous && !high, rise = !w->previous && high;
  w->previous = high;
  if (fall || rise) w->last_edge = now;
  if (w->tx_state) {
    if ((int32_t)(now - w->tx_due) < 0) return;
    // Missing a deadline by >200 us invalidates CEC timing; release the bus.
    if ((uint32_t)(now - w->tx_due) > 200) { finish(w, CEC_TX_TIMEOUT, now); return; }
    switch (w->tx_state) {
      case 1: w->low = false; w->tx_state = 2; w->tx_due = w->tx_start + 4500; break;
      case 2: w->tx_byte = w->tx_bit = 0; start_bit(w, now); break;
      case 3:
        w->low = false;
        w->tx_state = w->tx_one ? 4 : 5;
        w->tx_due = w->tx_start + (w->tx_one ? 1050 : 2400);
        break;
      case 4:
        if (w->tx_bit == 9) {
          bool broadcast = (w->tx.data[0] & 15) == 15;
          if (high != broadcast) { finish(w, CEC_TX_NACK, now); return; }
        } else if (!high) { finish(w, CEC_TX_COLLISION, now); return; }
        w->tx_state = 5; w->tx_due = w->tx_start + 2400; break;
      case 5:
        if (++w->tx_bit == 10) { w->tx_bit = 0; ++w->tx_byte; }
        if (w->tx_byte == w->tx.len) finish(w, CEC_TX_OK, now);
        else start_bit(w, now);
        break;
    }
    return;
  }
  if (w->ack_drive && (int32_t)(now - w->ack_until) >= 0) {
    w->low = false; w->ack_drive = false;
  }
  if (fall) {
    if (w->receiving) {
      unsigned period = now - w->fall;
      if (period < (w->first ? 4300u : 2050u) || period > (w->first ? 4700u : 2750u)) {
        w->receiving = false; ++w->rx_errors;
      } else {
        w->first = false;
        if (w->rx_bit == 9 && (w->rx.data[0] & 15) == w->address && w->address != 15 &&
            !w->rx_ready) {
          w->low = true; w->ack_drive = true; w->ack_until = now + 1500;
        }
      }
    }
    w->fall = now; w->have_fall = true;
  }
  if (rise && w->have_fall) {
    unsigned width = now - w->fall;
    if (width >= 3500 && width <= 3900) {
      memset(&w->rx, 0, sizeof(w->rx));
      w->receiving = w->first = true; w->rx_bit = w->rx_byte = 0;
    } else if (w->receiving) {
      bool one = width >= 400 && width <= 800;
      if (!one && !(width >= 1300 && width <= 1700)) {
        w->receiving = false; ++w->rx_errors;
      } else if (w->rx_bit < 8) {
        w->rx.data[w->rx_byte] = (w->rx.data[w->rx_byte] << 1) | one;
        ++w->rx_bit;
      } else if (w->rx_bit == 8) {
        w->eom = one; ++w->rx_bit;
      } else {
        if (w->eom) {
          w->rx.len = w->rx_byte + 1;
          unsigned dest = w->rx.data[0] & 15;
          if (dest == w->address || dest == 15) {
            if (!w->rx_ready) { w->received = w->rx; w->rx_ready = true; }
            else ++w->rx_drops;
          }
          w->receiving = false;
        } else if (++w->rx_byte == 16) {
          w->receiving = false; ++w->rx_errors;
        }
        w->rx_bit = 0;
      }
    }
  }
  if (w->receiving && now - w->fall > 5000) { w->receiving = false; ++w->rx_errors; }
  if (w->tx_pending) {
    if (now - w->requested > 2000000) { finish(w, CEC_TX_TIMEOUT, now); return; }
    if (high && !w->receiving && !w->ack_drive && now - w->last_edge >= 16800) {
      w->low = true; w->tx_start = now; w->tx_state = 1; w->tx_due = now + 3700;
    }
  }
}

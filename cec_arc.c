#include "cec_arc.h"
#include "cec_tv.h"
#include "spdif.h"
#include "board_status.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/irq.h"

typedef struct {
  cec_wire_t wire;
  cec_tv_t tv;
  cec_frame_t queue[8];
  unsigned head, tail, count, attempts;
  bool last_low, gate;
} arc_port_t;
static arc_port_t ports[BOARD_ARC_COUNT];
static const unsigned cec_pins[] = {BOARD_ARC1_CEC,
#if BOARD_ARC_COUNT > 1
  BOARD_ARC2_CEC
#endif
};
static const unsigned detect_pins[] = {BOARD_ARC1_DETECT,
#if BOARD_ARC_COUNT > 1
  BOARD_ARC2_DETECT
#endif
};
static repeating_timer_t timer;
volatile uint32_t cec_rx_frames, cec_tx_frames, cec_tx_errors, cec_rx_errors;
// Legacy state diagnostics describe ARC1; frame/error counters aggregate ports.
volatile uint32_t cec_arc_state, cec_address_conflict;

static bool tick(repeating_timer_t *t) {
  (void)t;
  uint32_t now = time_us_32();
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) {
    arc_port_t *p = &ports[i];
    cec_wire_tick(&p->wire, gpio_get(cec_pins[i]), now);
    if (p->wire.low != p->last_low) {
      gpio_set_dir(cec_pins[i], p->wire.low ? GPIO_OUT : GPIO_IN);
      p->last_low = p->wire.low;
    }
  }
  return true;
}
static bool enqueue(void *ctx, const cec_frame_t *f) {
  arc_port_t *p = ctx;
  if (p->count == 8) return false;
  p->queue[p->head] = *f;
  p->head = (p->head + 1) % 8;
  ++p->count;
  return true;
}
void cec_arc_init(void) {
  spdif_set_link_enabled(false);
  gpio_init(BOARD_HPD); gpio_put(BOARD_HPD, true);
  gpio_set_dir(BOARD_HPD, GPIO_OUT);
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) {
    gpio_init(cec_pins[i]); gpio_put(cec_pins[i], false);
    gpio_set_dir(cec_pins[i], GPIO_IN); gpio_disable_pulls(cec_pins[i]);
    gpio_init(detect_pins[i]); gpio_set_dir(detect_pins[i], GPIO_IN);
    gpio_disable_pulls(detect_pins[i]);
    board_status_set_arc(i, false);
    cec_wire_init(&ports[i].wire, time_us_32());
    cec_tv_init(&ports[i].tv, enqueue, &ports[i], time_us_32());
  }
  if (!add_repeating_timer_us(-50, tick, NULL, &timer)) panic("CEC timer unavailable");
}
void cec_arc_set_enabled(bool enabled) {
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i)
    cec_tv_request_arc(&ports[i].tv, enabled, time_us_32());
}
void cec_arc_request_port_playback(unsigned port) {
  if (port < BOARD_ARC_COUNT)
    cec_tv_request_playback(&ports[port].tv, time_us_32());
}
void cec_arc_request_playback(void) {
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) cec_arc_request_port_playback(i);
}
void cec_arc_port_volume_key(unsigned port, bool up, bool pressed) {
  if (port < BOARD_ARC_COUNT)
    ports[port].tv.key = pressed ? (up ? 0x41 : 0x42) : 0;
}
void cec_arc_volume_key(bool up, bool pressed) {
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) cec_arc_port_volume_key(i, up, pressed);
}
bool cec_arc_port_audio_allowed(unsigned port) {
  return port < BOARD_ARC_COUNT && ports[port].gate;
}
bool cec_arc_audio_allowed(void) {
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i)
    if (ports[i].gate) return true;
  return false;
}
static void port_task(unsigned i, uint32_t now, bool present) {
  arc_port_t *p = &ports[i];
  cec_wire_t *wire = &p->wire;
  cec_tv_t *tv = &p->tv;
  cec_frame_t incoming = {0};
  unsigned result = 0, tag = 0;
  uint32_t irq = save_and_disable_interrupts();
  if (wire->rx_ready) { incoming = wire->received; wire->rx_ready = false; }
  if (wire->tx_result) { result = wire->tx_result; tag = wire->tx.tag; wire->tx_result = 0; }
  restore_interrupts(irq);
  if (result && p->count) {
    if (result == CEC_TX_OK || tag == CEC_TAG_POLL_TV ||
        tag == CEC_TAG_POLL_AUDIO || ++p->attempts >= 3) {
      p->tail = (p->tail + 1) % 8; --p->count; p->attempts = 0;
      if (result == CEC_TX_OK) ++cec_tx_frames; else ++cec_tx_errors;
      cec_tv_tx_result(tv, tag, result, now);
    }
  }
  if (incoming.len) { ++cec_rx_frames; cec_tv_receive(tv, &incoming, now); }
  cec_tv_task(tv, now);
  bool enabled = present && tv->registered && !tv->conflict &&
                 tv->arc == CEC_ARC_ON && tv->desired;
  if (enabled != p->gate) {
    p->gate = enabled;
#if BOARD_ARC_COUNT > 1
    spdif_set_port_link_enabled(i, enabled);
#else
    spdif_set_link_enabled(enabled);
#endif
    board_status_set_arc(i, enabled);
  }
  irq = save_and_disable_interrupts();
  wire->address = tv->registered && !tv->conflict ? 0 : 15;
  if (p->count && !wire->tx_pending && !wire->tx_result)
    cec_wire_send(wire, &p->queue[p->tail], now);
  restore_interrupts(irq);
}
void cec_arc_task(void) {
  uint32_t now = time_us_32();
  bool any_present = false;
  bool any_conflict = false;
  cec_rx_errors = 0;
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) {
    bool present = gpio_get(detect_pins[i]);
    any_present |= present;
    port_task(i, now, present);
    any_conflict |= ports[i].tv.conflict;
    cec_rx_errors += ports[i].wire.rx_errors + ports[i].wire.rx_drops;
  }
  gpio_put(BOARD_HPD, !any_present);
  board_status_set_error(BOARD_ERROR_CEC_CONFLICT, any_conflict);
  cec_arc_state = ports[0].tv.arc;
  cec_address_conflict = ports[0].tv.conflict;
}

#include "cec_arc.h"
#include "cec_tv.h"
#include "spdif.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/irq.h"

static cec_wire_t wire;
static cec_tv_t tv;
static repeating_timer_t timer;
static cec_frame_t queue[8];
static unsigned head, tail, count, attempts;
static bool last_low, gate;
static uint8_t stable, raw;
static uint32_t changed[3];
static const unsigned buttons[] = {10, 26, 27};
volatile uint32_t cec_rx_frames, cec_tx_frames, cec_tx_errors, cec_rx_errors;
volatile uint32_t cec_arc_state, cec_address_conflict;

static bool tick(repeating_timer_t *t) {
  (void)t;
  cec_wire_tick(&wire, gpio_get(PICODAC_CEC_PIN), time_us_32());
  if (wire.low != last_low) {
    gpio_set_dir(PICODAC_CEC_PIN, wire.low ? GPIO_OUT : GPIO_IN);
    last_low = wire.low;
  }
  return true;
}
static bool enqueue(void *ctx, const cec_frame_t *f) {
  (void)ctx;
  if (count == 8) return false;
  queue[head] = *f; head = (head + 1) % 8; ++count;
  return true;
}
void cec_arc_init(void) {
  spdif_set_link_enabled(false);
  gpio_init(PICODAC_CEC_PIN); gpio_put(PICODAC_CEC_PIN, false);
  gpio_set_dir(PICODAC_CEC_PIN, GPIO_IN);
  gpio_disable_pulls(PICODAC_CEC_PIN); // external compliant CEC pull-up/interface
  cec_wire_init(&wire, time_us_32());
  cec_tv_init(&tv, enqueue, NULL, time_us_32());
  for (unsigned i = 0; i < 3; ++i) {
    gpio_init(buttons[i]); gpio_set_dir(buttons[i], GPIO_IN); gpio_pull_up(buttons[i]);
  }
  // Callback stays short; no I/O waits, allocations or protocol replies here.
  if (!add_repeating_timer_us(-50, tick, NULL, &timer)) panic("CEC timer unavailable");
}
void cec_arc_set_enabled(bool enabled) { cec_tv_request_arc(&tv, enabled, time_us_32()); }
void cec_arc_volume_key(bool up, bool pressed) { tv.key = pressed ? (up ? 0x41 : 0x42) : 0; }
bool cec_arc_audio_allowed(void) { return gate; }
void cec_arc_task(void) {
  uint32_t now = time_us_32();
  cec_frame_t incoming = {0};
  unsigned result = 0, tag = 0;
  uint32_t irq = save_and_disable_interrupts();
  if (wire.rx_ready) { incoming = wire.received; wire.rx_ready = false; }
  if (wire.tx_result) { result = wire.tx_result; tag = wire.tx.tag; wire.tx_result = 0; }
  cec_rx_errors = wire.rx_errors + wire.rx_drops;
  restore_interrupts(irq);
  if (result && count) {
    if (result == CEC_TX_OK || tag == CEC_TAG_POLL_TV || ++attempts >= 3) {
      tail = (tail + 1) % 8; --count; attempts = 0;
      if (result == CEC_TX_OK) ++cec_tx_frames; else ++cec_tx_errors;
      cec_tv_tx_result(&tv, tag, result, now);
    }
  }
  if (incoming.len) { ++cec_rx_frames; cec_tv_receive(&tv, &incoming, now); }
  for (unsigned i = 0; i < 3; ++i) {
    unsigned mask = 1u << i;
    bool pressed = !gpio_get(buttons[i]);
    if (pressed != !!(raw & mask)) { raw ^= mask; changed[i] = now; }
    if (now - changed[i] >= 20000 && pressed != !!(stable & mask)) {
      stable ^= mask;
      if (i == 0 && pressed) cec_arc_set_enabled(!tv.desired);
      if (i != 0) tv.key = (stable & 2) ? 0x41 : (stable & 4) ? 0x42 : 0;
    }
  }
  cec_tv_task(&tv, now);
  bool enabled = tv.registered && !tv.conflict && tv.arc == CEC_ARC_ON && tv.desired;
  if (enabled != gate) { gate = enabled; spdif_set_link_enabled(gate); }
  cec_arc_state = tv.arc; cec_address_conflict = tv.conflict;
  irq = save_and_disable_interrupts();
  wire.address = tv.registered && !tv.conflict ? 0 : 15;
  if (count && !wire.tx_pending && !wire.tx_result)
    cec_wire_send(&wire, &queue[tail], now);
  restore_interrupts(irq);
}

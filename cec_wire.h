#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t data[16], len, tag; } cec_frame_t;
enum { CEC_TX_OK = 1, CEC_TX_NACK = 2, CEC_TX_COLLISION = 3, CEC_TX_TIMEOUT = 4 };
// Single-context wire engine. Call every <=50 us, supplying the actual line
// level and timestamp. 'low' means open-drain pull-down; never drive high.
typedef struct {
  bool low, previous, have_fall, receiving, first, eom, ack_drive;
  uint8_t address, rx_bit, rx_byte;
  uint32_t fall, last_edge, ack_until;
  cec_frame_t rx, received, tx;
  bool rx_ready, tx_pending;
  uint8_t tx_state, tx_bit, tx_byte, tx_result;
  uint32_t tx_start, requested, tx_due;
  bool tx_one;
  unsigned rx_errors, rx_drops;
} cec_wire_t;
void cec_wire_init(cec_wire_t *w, uint32_t now);
bool cec_wire_send(cec_wire_t *w, const cec_frame_t *frame, uint32_t now);
void cec_wire_tick(cec_wire_t *w, bool high, uint32_t now);

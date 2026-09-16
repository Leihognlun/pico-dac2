#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One E-AC-3 burst carries 1536 source samples at 4x carrier rate.
#define EAC3_BURST_BYTES 24576u
#define EAC3_BURST_WORDS (EAC3_BURST_BYTES / 2)
#define EAC3_CARRIER_RATE 192000u

// Return bytes read, zero at EOF, or -1 on I/O error. Short reads are allowed.
typedef int (*eac3_read_fn)(void *ctx, uint8_t *dst, size_t size);
typedef enum {
  EAC3_EOF = 0, EAC3_BURST_READY = 1,
  EAC3_IO_ERROR = -1, EAC3_TRUNCATED = -2, EAC3_BAD_HEADER = -3,
  EAC3_UNSUPPORTED = -4, EAC3_OVERFLOW = -5
} eac3_result_t;

typedef struct {
  eac3_read_fn read;
  void *ctx;
  uint8_t header[6];
  bool pending;
  int terminal;
} eac3_reader_t;

void eac3_reader_init(eac3_reader_t *r, eac3_read_fn read, void *ctx);
// Raw big-endian .eac3/.ec3, 48 kHz, one independent substream (ID 0)
// and its dependent frames. Output is numeric 16-bit IEC 61937 carrier words.
int eac3_next_burst(eac3_reader_t *r, uint16_t out[EAC3_BURST_WORDS]);
const char *eac3_result_string(int result);

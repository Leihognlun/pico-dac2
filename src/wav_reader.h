#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int (*wav_read_fn)(void *ctx, uint8_t *dst, size_t size);

typedef struct {
  wav_read_fn read;
  void *ctx;
  uint32_t sample_rate, data_remaining;
  uint16_t channels, bits;
} wav_reader_t;

enum { WAV_EOF = 0, WAV_BLOCK_READY = 1, WAV_IO_ERROR = -1,
       WAV_TRUNCATED = -2, WAV_BAD_FORMAT = -3, WAV_UNSUPPORTED = -4 };

int wav_reader_open(wav_reader_t *r, wav_read_fn read, void *ctx);
int wav_next_block(wav_reader_t *r, int32_t out[384], unsigned *frames);
const char *wav_result_string(int result);

#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int (*ac3_read_fn)(void *, uint8_t *, size_t);
#define AC3_BURST_WORDS 3072u
#define AC3_CARRIER_RATE 48000u
typedef struct { ac3_read_fn read; void *ctx; int terminal; } ac3_reader_t;
enum { AC3_EOF=0, AC3_BURST_READY=1, AC3_IO_ERROR=-1, AC3_TRUNCATED=-2,
       AC3_BAD_HEADER=-3, AC3_UNSUPPORTED=-4, AC3_OVERFLOW=-5 };
void ac3_reader_init(ac3_reader_t *, ac3_read_fn, void *);
int ac3_next_burst(ac3_reader_t *, uint16_t out[AC3_BURST_WORDS]);
const char *ac3_result_string(int);

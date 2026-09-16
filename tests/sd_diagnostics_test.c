#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sd_diagnostics.h"

volatile uint32_t spdif_silence_block_count, spdif_tx_stall_count, i2s_tx_stall_count;
static uint32_t now;
static char captured[4096];
static unsigned size, budget;
uint32_t time_us_32(void) { return now; }
bool uart_is_writable(void *uart) { assert(uart == (void *)1); return budget != 0; }
void uart_putc_raw(void *uart, char c) {
  assert(uart == (void *)1 && budget && size < sizeof(captured) - 1);
  --budget; captured[size++] = c; captured[size] = 0;
}
static void drain(unsigned produced, unsigned consumed, unsigned under, unsigned played) {
  for (unsigned i = 0; i < 100; ++i) {
    budget = 100;
    unsigned before = size;
    sd_diag_task(produced, consumed, 1, under, played);
    assert(size - before <= 8);
  }
}
int main(void) {
  sd_diag_init();
  now = 100;
  sd_diag_read_begin();
  now = 1500100;
  // Full UART must not write or block, even when reporting a stuck read.
  budget = 0;
  sd_diag_task(4, 4, 1, 2, 4);
  assert(size == 0);
  drain(4, 4, 2, 4);
  assert(strstr(captured, "ph=3 q=0 qmin=0"));
  assert(strstr(captured, "rdage_us=1500000"));
  assert(strstr(captured, "under=2(+2)"));
  sd_diag_read_end(8192, 0);
  now += 10; sd_diag_submit_begin();
  now += 123; sd_diag_submit_end();
  now += 1000; sd_diag_submit_begin();
  now += 100; sd_diag_submit_end();
  spdif_silence_block_count = 3;
  size = 0; captured[0] = 0;
  now += 1000000;
  drain(8, 6, 2, 6);
  assert(strstr(captured, "reads=1(+1) bytes=8192(+8192)"));
  assert(strstr(captured, "rdmax_us=1500000 rdage_us=0 fr=0"));
  assert(strstr(captured, "under=2(+0) dma0=3(+3)"));
  assert(strstr(captured, "gapmax_us=1123 encmax_us=123"));
  puts("PASS: UART diagnostics deltas, read age/max, encode/gap timing, full FIFO nonblocking and bounded writes");
}

#include "sd_diagnostics.h"

#include <stdatomic.h>
#include <stdio.h>
#include "hardware/uart.h"
#include "pico/stdlib.h"

extern volatile uint32_t spdif_silence_block_count;
extern volatile uint32_t spdif_tx_stall_count, i2s_tx_stall_count;

// Core1 publishes only small atomic values; core0 alone formats/transmits.
static atomic_uint phase, read_started, read_max_us, read_count, read_bytes;
static atomic_int read_result;
static uint32_t epoch, report_at, last_submit, submit_start, gap_max, encode_max;
static unsigned previous_reads, previous_bytes, previous_underruns, previous_silence;
static unsigned previous_played;
static char line[480];
static unsigned pending, sent, q_min = 4;
#ifndef PICODAC_SD_UART_LOG
#define PICODAC_SD_UART_LOG 1
#endif
// Keep instrumentation and formatting in the silent comparison build.
static volatile bool uart_output = PICODAC_SD_UART_LOG;

void sd_diag_init(void) {
  epoch = time_us_32();
  report_at = epoch;
  // Existing startup printf is before playback; periodic output is never printf.
  if (uart_output) {
    puts("SDDIAG UART0 115200 8N1 TX=GP0 RX=GP1; counters cumulative, +=interval delta");
    puts("phase: 0=init 1=mount 2=open 3=read 4=pack 5=queue-full 6=done; times in us");
  }
}

void sd_diag_phase(enum sd_diag_phase p) {
  atomic_store_explicit(&phase, p, memory_order_release);
}

void sd_diag_read_begin(void) {
  atomic_store_explicit(&read_started, time_us_32(), memory_order_relaxed);
  sd_diag_phase(SD_DIAG_READ);
}

void sd_diag_read_end(unsigned bytes, int result) {
  uint32_t elapsed = time_us_32() - atomic_load_explicit(&read_started, memory_order_relaxed);
  unsigned max = atomic_load_explicit(&read_max_us, memory_order_relaxed);
  if (elapsed > max) atomic_store_explicit(&read_max_us, elapsed, memory_order_relaxed);
  atomic_store_explicit(&read_result, result, memory_order_relaxed);
  atomic_store_explicit(&read_bytes,
      atomic_load_explicit(&read_bytes, memory_order_relaxed) + bytes, memory_order_relaxed);
  atomic_store_explicit(&read_count,
      atomic_load_explicit(&read_count, memory_order_relaxed) + 1, memory_order_relaxed);
  sd_diag_phase(SD_DIAG_PACK);
}

void sd_diag_submit_begin(void) {
  submit_start = time_us_32();
  if (last_submit && submit_start - last_submit > gap_max)
    gap_max = submit_start - last_submit;
  last_submit = submit_start;
}

void sd_diag_submit_end(void) {
  uint32_t elapsed = time_us_32() - submit_start;
  if (elapsed > encode_max) encode_max = elapsed;
}

void sd_diag_task(unsigned produced, unsigned consumed, int status,
                  unsigned underruns, unsigned played) {
  unsigned depth = produced - consumed;
  if (depth < q_min) q_min = depth;
  uint32_t now = time_us_32();
  if (sent == pending && (uint32_t)(now - report_at) >= 1000000) {
    report_at = now;
    unsigned p = atomic_load_explicit(&phase, memory_order_acquire);
    // A new read can start after 'now' was sampled on this core.
    int32_t elapsed = (int32_t)(now - atomic_load_explicit(&read_started, memory_order_relaxed));
    unsigned age = p == SD_DIAG_READ && elapsed > 0 ? (unsigned)elapsed : 0;
    unsigned reads = atomic_load_explicit(&read_count, memory_order_relaxed);
    unsigned bytes = atomic_load_explicit(&read_bytes, memory_order_relaxed);
    unsigned silence = spdif_silence_block_count;
    int n = snprintf(line, sizeof(line),
        "SD t_ms=%lu st=%d ph=%u q=%u qmin=%u prod=%u play=%u(+%u) "
        "under=%u(+%u) dma0=%u(+%u) txstall=%lu/%lu "
        "reads=%u(+%u) bytes=%u(+%u) rdmax_us=%u rdage_us=%u fr=%d "
        "gapmax_us=%lu encmax_us=%lu\r\n",
        (unsigned long)((now - epoch) / 1000), status, p, depth, q_min,
        produced, played, played - previous_played,
        underruns, underruns - previous_underruns, silence, silence - previous_silence,
        (unsigned long)spdif_tx_stall_count, (unsigned long)i2s_tx_stall_count,
        reads, reads - previous_reads, bytes, bytes - previous_bytes,
        atomic_load_explicit(&read_max_us, memory_order_relaxed), age,
        atomic_load_explicit(&read_result, memory_order_relaxed),
        (unsigned long)gap_max, (unsigned long)encode_max);
    pending = n > 0 ? ((unsigned)n < sizeof(line) ? (unsigned)n : sizeof(line) - 1) : 0;
    sent = 0;
    previous_reads = reads; previous_bytes = bytes;
    previous_underruns = underruns; previous_silence = silence; previous_played = played;
    q_min = depth;
  }
  // Never wait for UART space: bounded work, at most 8 bytes per audio loop.
  if (uart_output) {
    for (unsigned i = 0; i < 8 && sent < pending && uart_is_writable(uart0); ++i)
      uart_putc_raw(uart0, line[sent++]);
  } else {
    // Discard in the same bounded chunks, without accessing UART hardware.
    unsigned remaining = pending - sent;
    sent += remaining < 8 ? remaining : 8;
  }
}

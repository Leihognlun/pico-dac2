#include "sd_eac3_player.h"
#include "sd_diagnostics.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "eac3_burst.h"
#include "ff.h"
#include "tf_card.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "spdif.h"
#include "spdif_encode.h"

// Four complete bursts give up to 128 ms of storage latency tolerance.
// Only core1 accesses FatFs. A slot is released AFTER its last word is copied.
#define BURST_SLOTS 4u
static uint16_t bursts[BURST_SLOTS][EAC3_BURST_WORDS];
static atomic_uint produced, consumed;
static atomic_int producer_result;
static FATFS fs;
static FIL file;
static uint8_t cache[8192];
static size_t cache_pos, cache_len;
volatile uint32_t sd_eac3_bursts_played;
volatile uint32_t sd_eac3_underruns;
volatile int sd_eac3_status; // 0 waiting, 1 playing, 2 EOF, negative error

static int file_read(void *ctx, uint8_t *dst, size_t size) {
  (void)ctx;
  if (cache_pos == cache_len) {
    UINT count;
    sd_diag_read_begin();
    FRESULT fr = f_read(&file, cache, sizeof(cache), &count);
    sd_diag_read_end(fr == FR_OK ? count : 0, fr);
    if (fr != FR_OK) return -1;
    cache_pos = 0;
    cache_len = count;
    if (!count) return 0;
  }
  if (size > cache_len - cache_pos) size = cache_len - cache_pos;
  memcpy(dst, cache + cache_pos, size);
  cache_pos += size;
  return (int)size;
}

static void producer(void) {
  pico_fatfs_spi_config_t config = {
      .spi_inst = spi0, .clk_slow = 100000, .clk_fast = PICODAC_SD_SPI_HZ,
      .pin_miso = PICODAC_SD_MISO_PIN, .pin_cs = PICODAC_SD_CS_PIN,
      .pin_sck = PICODAC_SD_SCK_PIN, .pin_mosi = PICODAC_SD_MOSI_PIN,
      .pullup = true};
  // Hardware SPI only: never let the library claim an audio PIO state machine.
  if (!pico_fatfs_set_config(&config)) {
    atomic_store_explicit(&producer_result, EAC3_UNSUPPORTED, memory_order_release);
    return;
  }
  sd_diag_phase(SD_DIAG_MOUNT);
  FRESULT fr = f_mount(&fs, "0:", 1);
  if (fr != FR_OK) {
    printf("TF mount failed: FatFs %d\n", fr);
    atomic_store_explicit(&producer_result, EAC3_IO_ERROR, memory_order_release);
    return;
  }
  sd_diag_phase(SD_DIAG_OPEN);
  fr = f_open(&file, PICODAC_SD_FILE, FA_READ);
  if (fr != FR_OK) {
    printf("TF open failed (%s): FatFs %d\n", PICODAC_SD_FILE, fr);
    f_mount(NULL, "0:", 0);
    atomic_store_explicit(&producer_result, EAC3_IO_ERROR, memory_order_release);
    return;
  }
  eac3_reader_t reader;
  eac3_reader_init(&reader, file_read, NULL);
  unsigned write = 0;
  int result;
  for (;;) {
    sd_diag_phase(SD_DIAG_WAIT);
    while (write - atomic_load_explicit(&consumed, memory_order_acquire) == BURST_SLOTS)
      sleep_us(100);
    sd_diag_phase(SD_DIAG_PACK);
    result = eac3_next_burst(&reader, bursts[write % BURST_SLOTS]);
    if (result != EAC3_BURST_READY) break;
    atomic_store_explicit(&produced, ++write, memory_order_release);
  }
  f_close(&file);
  f_mount(NULL, "0:", 0);
#if !PICODAC_SD_DIAGNOSTICS
  printf("TF reader: %s; %u complete bursts queued\n",
         eac3_result_string(result), write);
#endif
  // 2 distinguishes EOF from the initial zero (producer still working).
  atomic_store_explicit(&producer_result, result == EAC3_EOF ? 2 : result,
                        memory_order_release);
  sd_diag_phase(SD_DIAG_DONE);
}

static void diagnostics(void) {
  // consumed is owned by this core; produced only advances on the other core.
  unsigned c = atomic_load_explicit(&consumed, memory_order_relaxed);
  unsigned p = atomic_load_explicit(&produced, memory_order_acquire);
  sd_diag_task(p, c, sd_eac3_status, sd_eac3_underruns, sd_eac3_bursts_played);
}

void sd_eac3_player_run(void) {
  sd_diag_init();
  printf("TF E-AC-3: %s; SPI0 RX=%d CS=%d SCK=%d TX=%d\n",
         PICODAC_SD_FILE, PICODAC_SD_MISO_PIN, PICODAC_SD_CS_PIN,
         PICODAC_SD_SCK_PIN, PICODAC_SD_MOSI_PIN);
  multicore_launch_core1(producer);
  // Prebuffer before enabling the carrier. Short files can start at EOF.
  while (atomic_load_explicit(&produced, memory_order_acquire) < BURST_SLOTS &&
         !atomic_load_explicit(&producer_result, memory_order_acquire)) {
    diagnostics();
    sleep_ms(1);
  }
  if (!atomic_load_explicit(&produced, memory_order_acquire)) {
    int result = atomic_load_explicit(&producer_result, memory_order_acquire);
    sd_eac3_status = result;
    printf("TF stopped: %s\n", eac3_result_string(result == 2 ? 0 : result));
    for (;;) {
      diagnostics();
#if PICODAC_SD_DIAGNOSTICS
      sleep_ms(1);
#else
      sleep_ms(1000);
#endif
    }
  }
  spdif_init(PICODAC_SPDIF_PIN, EAC3_CARRIER_RATE, 16, true);
  spdif_start();
  sd_eac3_status = 1;
  unsigned read = 0, offset = 0;
  bool have_burst = false, reported = false;
  for (;;) {
    if (!spdif_buffer_ready()) { diagnostics(); tight_loop_contents(); continue; }
    sd_diag_submit_begin();
    if (offset == 0) {
      have_burst = read != atomic_load_explicit(&produced, memory_order_acquire);
      if (!have_burst) {
        int result = atomic_load_explicit(&producer_result, memory_order_acquire);
        if (!result) ++sd_eac3_underruns;
        else if (!reported) {
          // Recheck after observing completion to see the producer's last slot.
          have_burst = read != atomic_load_explicit(&produced, memory_order_acquire);
          if (!have_burst) {
            sd_eac3_status = result;
            reported = true;
          }
        }
      }
    }
    int32_t *out = spdif_write_buffer();
    for (unsigned i = 0; i < SPDIF_BLOCK_FRAMES * 2; ++i)
      out[i] = have_burst ? (int16_t)bursts[read % BURST_SLOTS][offset + i] : 0;
    spdif_submit_buffer();
    sd_diag_submit_end();
    offset += SPDIF_BLOCK_FRAMES * 2;
    if (offset == EAC3_BURST_WORDS) {
      offset = 0;
      if (have_burst) {
        ++sd_eac3_bursts_played;
        atomic_store_explicit(&consumed, ++read, memory_order_release);
      }
    }
    // After EOF/error keep Non-PCM zero carrier running, ensuring the final
    // queued DMA block is never truncated. No printing in the audio hot path.
  }
}

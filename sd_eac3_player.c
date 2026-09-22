#include "sd_eac3_player.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "ac3_burst.h"
#include "cec_arc.h"
#include "eac3_burst.h"
#include "ff.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "sd_controls.h"
#include "sd_diagnostics.h"
#include "spdif.h"
#include "spdif_encode.h"
#include "tf_card.h"
#include "wav_reader.h"

#define COMPRESSED_SLOTS 4u
#define PCM_SLOT_WORDS (SPDIF_BLOCK_FRAMES * 2u)
#define PCM_SLOTS ((COMPRESSED_SLOTS * EAC3_BURST_WORDS * sizeof(uint16_t)) / \
                   (PCM_SLOT_WORDS * sizeof(int32_t)))
#define MAX_TRACKS 32u
#define TRACK_PATH_SIZE 64u

typedef union {
  uint16_t compressed[COMPRESSED_SLOTS][EAC3_BURST_WORDS];
  int32_t pcm[PCM_SLOTS][PCM_SLOT_WORDS];
} audio_storage_t;

typedef enum {
  TRACK_EAC3,
  TRACK_AC3,
  TRACK_WAV,
} track_format_t;

static audio_storage_t audio_storage;
_Static_assert(sizeof(audio_storage.pcm) == sizeof(audio_storage.compressed),
               "PCM and compressed queues must reuse the same storage");
static char tracks[MAX_TRACKS][TRACK_PATH_SIZE];
static unsigned track_count;

static atomic_uint produced, consumed;
static atomic_int producer_result;
static atomic_bool stream_ready, stream_switching;
#if PICODAC_SD_BENCHMARK
static atomic_bool benchmark_running;
#endif
static atomic_uint stream_generation, switch_sequence, switch_ack_sequence;

// Published by Core1 before stream_generation/stream_ready release stores.
static uint32_t stream_rate, stream_words;
static uint8_t stream_depth;
static bool stream_non_pcm;

static FATFS fs;
static FIL file;
static uint8_t cache[8192];
static size_t cache_pos, cache_len;

volatile uint32_t sd_eac3_bursts_played;
volatile uint32_t sd_eac3_underruns;
volatile int sd_eac3_status; // 0 waiting, 1 output active, 2 EOF, negative error

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

static bool supported_name(const char *name) {
  const char *dot = strrchr(name, '.');
  if (!dot) return false;
  char ext[6] = {0};
  for (unsigned i = 0; dot[i] && i < sizeof(ext) - 1; ++i)
    ext[i] = (char)tolower((unsigned char)dot[i]);
  return !strcmp(ext, ".wav") || !strcmp(ext, ".ac3") ||
         !strcmp(ext, ".ec3") || !strcmp(ext, ".eac3");
}

#if PICODAC_SD_BENCHMARK
static bool wav_name(const char *name) {
  const char *dot = strrchr(name, '.');
  if (!dot) return false;
  char ext[5] = {0};
  for (unsigned i = 0; dot[i] && i < sizeof(ext) - 1; ++i)
    ext[i] = (char)tolower((unsigned char)dot[i]);
  return !strcmp(ext, ".wav");
}

static void benchmark_file(const char *path) {
  FIL benchmark;
  FRESULT fr = f_open(&benchmark, path, FA_READ);
  if (fr != FR_OK) {
    printf("SDBENCH file=%s open_fr=%d\n", path, fr);
    return;
  }

  enum { LIMIT_BYTES = 4 * 1024 * 1024, SLOW_WARN_US = 6000 };
  uint32_t started = time_us_32(), maximum = 0;
  unsigned bytes = 0, reads = 0, slow6 = 0, slow8 = 0;
  while (bytes < LIMIT_BYTES) {
    UINT count = 0;
    uint32_t read_started = time_us_32();
    fr = f_read(&benchmark, cache, sizeof(cache), &count);
    uint32_t elapsed = time_us_32() - read_started;
    if (elapsed > maximum) maximum = elapsed;
    if (elapsed > SLOW_WARN_US) ++slow6;
    if (elapsed > 8000) ++slow8;
    bytes += count;
    ++reads;
    if (fr != FR_OK || !count) break;
  }
  uint32_t total_us = time_us_32() - started;
  unsigned kbps = total_us ? (unsigned)(((uint64_t)bytes * 1000) / total_us) : 0;
  unsigned average_us = reads ? total_us / reads : 0;
  int margin96 = ((int)kbps - 576) * 100 / 576;
  int margin192 = ((int)kbps - 1152) * 100 / 1152;
  printf("SDBENCH file=%s spi_hz=%u bytes=%u time_ms=%lu "
         "speed_kBps=%u margin96_pct=%d margin192_pct=%d "
         "reads=%u rdavg_us=%u rdmax_us=%lu "
         "slow6ms=%u slow8ms=%u fr=%d\n",
         path, PICODAC_SD_SPI_HZ, bytes, (unsigned long)(total_us / 1000),
         kbps, margin96, margin192, reads, average_us, (unsigned long)maximum,
         slow6, slow8, fr);
  f_close(&benchmark);
}

static void benchmark_wav_files(void) {
  bool found = false;
  unsigned tested = 0;
  puts("SDBENCH begin: WAV requires 576/1152 kB/s at 96/192kHz 24-bit stereo; read-only 4MiB/file");
  for (unsigned i = 0; i < track_count; ++i) {
    if (!wav_name(tracks[i]) || tested == 4) continue;
    found = true;
    ++tested;
    benchmark_file(tracks[i]);
  }
  if (!found && track_count) benchmark_file(tracks[0]);
  puts("SDBENCH end");
}
#endif

static void scan_tracks(void) {
  DIR dir;
  FILINFO info;
  track_count = 0;
  if (f_opendir(&dir, "0:/") != FR_OK) return;
  while (track_count < MAX_TRACKS && f_readdir(&dir, &info) == FR_OK &&
         info.fname[0]) {
    if ((info.fattrib & AM_DIR) || !supported_name(info.fname)) continue;
    snprintf(tracks[track_count], TRACK_PATH_SIZE, "0:/%.60s", info.fname);
    ++track_count;
  }
  f_closedir(&dir);
}

static const char *format_name(track_format_t format) {
  if (format == TRACK_WAV) return "WAV";
  if (format == TRACK_AC3) return "AC3";
  return "EAC3";
}

static const char *result_name(track_format_t format, int result) {
  if (format == TRACK_WAV) return wav_result_string(result);
  if (format == TRACK_AC3) return ac3_result_string(result);
  return eac3_result_string(result);
}

static void publish_track_state(bool ready, int result) {
  atomic_store_explicit(&producer_result, result, memory_order_relaxed);
  atomic_store_explicit(&stream_ready, ready, memory_order_relaxed);
  atomic_fetch_add_explicit(&stream_generation, 1, memory_order_release);
  atomic_store_explicit(&stream_switching, false, memory_order_release);
}

static void wait_for_switch_ack(void) {
  unsigned sequence = atomic_fetch_add_explicit(
                          &switch_sequence, 1, memory_order_relaxed) + 1;
  atomic_store_explicit(&stream_switching, true, memory_order_release);
  while (atomic_load_explicit(&switch_ack_sequence, memory_order_acquire) !=
         sequence)
    sleep_us(100);
}

static unsigned advance_track(unsigned track, unsigned *command) {
  unsigned next = sd_controls_next_generation();
  unsigned steps = next - *command;
  if (!steps) steps = 1;
  *command = next;
  return (track + steps) % track_count;
}

static void producer(void) {
  pico_fatfs_spi_config_t config = {
      .spi_inst = spi0,
      .clk_slow = 100000,
      .clk_fast = PICODAC_SD_SPI_HZ,
      .pin_miso = PICODAC_SD_MISO_PIN,
      .pin_cs = PICODAC_SD_CS_PIN,
      .pin_sck = PICODAC_SD_SCK_PIN,
      .pin_mosi = PICODAC_SD_MOSI_PIN,
      .pullup = true,
  };
  if (!pico_fatfs_set_config(&config)) {
#if PICODAC_SD_BENCHMARK
    atomic_store_explicit(&benchmark_running, false, memory_order_release);
#endif
    publish_track_state(false, EAC3_UNSUPPORTED);
    return;
  }

  sd_diag_phase(SD_DIAG_MOUNT);
  FRESULT fr = f_mount(&fs, "0:", 1);
  if (fr != FR_OK) {
    printf("TF mount failed: FatFs %d\n", fr);
#if PICODAC_SD_BENCHMARK
    atomic_store_explicit(&benchmark_running, false, memory_order_release);
#endif
    publish_track_state(false, EAC3_IO_ERROR);
    return;
  }

  scan_tracks();
  if (!track_count) {
    strncpy(tracks[0], PICODAC_SD_FILE, TRACK_PATH_SIZE - 1);
    tracks[0][TRACK_PATH_SIZE - 1] = '\0';
    track_count = 1;
  }
  printf("TF playlist: %u track(s)\n", track_count);
  for (unsigned i = 0; i < track_count; ++i)
    printf("TF playlist[%u]: %s\n", i, tracks[i]);

#if PICODAC_SD_BENCHMARK
  benchmark_wav_files();
  atomic_store_explicit(&benchmark_running, false, memory_order_release);
#endif

  unsigned track = 0;
  unsigned command = sd_controls_next_generation();
  bool first_track = true;

  for (;;) {
    if (!first_track) {
      wait_for_switch_ack();
      atomic_store_explicit(&produced, 0, memory_order_relaxed);
      atomic_store_explicit(&consumed, 0, memory_order_relaxed);
      atomic_store_explicit(&producer_result, 0, memory_order_relaxed);
      atomic_store_explicit(&stream_ready, false, memory_order_relaxed);
    }
    first_track = false;
    cache_pos = cache_len = 0;

    sd_diag_phase(SD_DIAG_OPEN);
    fr = f_open(&file, tracks[track], FA_READ);
    if (fr != FR_OK) {
      printf("TF open failed: track=%u file=%s FatFs=%d\n", track,
             tracks[track], fr);
      publish_track_state(false, EAC3_IO_ERROR);
      goto wait_next;
    }

    uint8_t signature[12];
    int signature_size = file_read(NULL, signature, sizeof(signature));
    cache_pos = 0;
    track_format_t format = TRACK_EAC3;
    if (signature_size == 12 && !memcmp(signature, "RIFF", 4) &&
        !memcmp(signature + 8, "WAVE", 4)) {
      format = TRACK_WAV;
    } else if (signature_size >= 6 && signature[0] == 0x0b &&
               signature[1] == 0x77 && (signature[5] >> 3) <= 10) {
      format = TRACK_AC3;
    }

    eac3_reader_t eac3;
    ac3_reader_t ac3;
    wav_reader_t wav;
    int result = 1;
    if (format == TRACK_WAV) {
      result = wav_reader_open(&wav, file_read, NULL);
      if (result == WAV_BLOCK_READY) {
        stream_rate = wav.sample_rate;
        stream_depth = (uint8_t)wav.bits;
        stream_words = SPDIF_BLOCK_FRAMES * 2;
        stream_non_pcm = false;
      }
    } else if (format == TRACK_AC3) {
      ac3_reader_init(&ac3, file_read, NULL);
      stream_rate = AC3_CARRIER_RATE;
      stream_depth = 16;
      stream_words = AC3_BURST_WORDS;
      stream_non_pcm = true;
    } else {
      eac3_reader_init(&eac3, file_read, NULL);
      stream_rate = EAC3_CARRIER_RATE;
      stream_depth = 16;
      stream_words = EAC3_BURST_WORDS;
      stream_non_pcm = true;
    }

    if (result != 1) {
      printf("TF reject: track=%u file=%s format=%s result=%d (%s)\n", track,
             tracks[track], format_name(format), result,
             result_name(format, result));
      f_close(&file);
      publish_track_state(false, result);
      goto wait_next;
    }

    printf("TF play: track=%u file=%s format=%s rate=%lu depth=%u %s\n",
           track, tracks[track], format_name(format),
           (unsigned long)stream_rate, stream_depth,
           stream_non_pcm ? "non-pcm" : "pcm");

    unsigned write = 0;
    unsigned queue_slots = format == TRACK_WAV ? PCM_SLOTS : COMPRESSED_SLOTS;
    bool ready_published = false;
    for (;;) {
      if (sd_controls_next_generation() != command) break;
      sd_diag_phase(SD_DIAG_WAIT);
      while (write - atomic_load_explicit(&consumed, memory_order_acquire) ==
             queue_slots) {
        if (sd_controls_next_generation() != command) goto switch_track;
        sleep_us(100);
      }

      sd_diag_phase(SD_DIAG_PACK);
      if (format == TRACK_WAV) {
        unsigned frames;
        result = wav_next_block(&wav,
                                audio_storage.pcm[write % PCM_SLOTS], &frames);
      } else if (format == TRACK_AC3) {
        result = ac3_next_burst(&ac3,
            audio_storage.compressed[write % COMPRESSED_SLOTS]);
      } else {
        result = eac3_next_burst(&eac3,
            audio_storage.compressed[write % COMPRESSED_SLOTS]);
      }
      if (result != 1) break;
      atomic_store_explicit(&produced, ++write, memory_order_release);
      // Start only after the format-specific queue is full.  The same 96 KiB
      // storage therefore provides 4 compressed bursts or 64 PCM blocks.
      if (!ready_published && write == queue_slots) {
        publish_track_state(true, 0);
        ready_published = true;
      }
    }

switch_track:
    f_close(&file);
    if (sd_controls_next_generation() != command) {
      track = advance_track(track, &command);
      continue;
    }

    int final_result = result == 0 ? 2 : result;
    if (!ready_published) {
      publish_track_state(write != 0, final_result);
      ready_published = write != 0;
    }
    atomic_store_explicit(&producer_result, final_result,
                          memory_order_release);
    sd_diag_phase(SD_DIAG_DONE);
    printf("TF stopped: track=%u file=%s format=%s result=%d (%s)\n", track,
           tracks[track], format_name(format), final_result,
           result_name(format, result));

wait_next:
#ifdef PICODAC_TEST_EXIT_ON_EOF
    f_mount(NULL, "0:", 0);
    return;
#endif
    while (sd_controls_next_generation() == command) sleep_ms(1);
    track = advance_track(track, &command);
  }
}

static void diagnostics(void) {
  sd_controls_task();
  cec_arc_task();
#if PICODAC_SD_BENCHMARK
  if (atomic_load_explicit(&benchmark_running, memory_order_acquire)) return;
#endif
  unsigned c = atomic_load_explicit(&consumed, memory_order_relaxed);
  unsigned p = atomic_load_explicit(&produced, memory_order_acquire);
  sd_diag_task(p, c, sd_eac3_status, sd_eac3_underruns,
               sd_eac3_bursts_played);
}

void sd_eac3_player_run(void) {
  sd_diag_init();
  sd_controls_init();
  printf("TF audio: SPI0 RX=%d CS=%d SCK=%d TX=%d\n",
         PICODAC_SD_MISO_PIN, PICODAC_SD_CS_PIN, PICODAC_SD_SCK_PIN,
         PICODAC_SD_MOSI_PIN);
#if PICODAC_SD_BENCHMARK
  atomic_store_explicit(&benchmark_running, true, memory_order_relaxed);
#endif
  multicore_launch_core1(producer);

  bool output_initialized = false;
  unsigned generation = 0;
  unsigned read = 0, offset = 0;
  bool have_unit = false, reported = false;

  for (;;) {
    diagnostics();

    if (atomic_load_explicit(&stream_switching, memory_order_acquire)) {
      if (output_initialized) {
        spdif_deinit();
        output_initialized = false;
      }
      read = offset = 0;
      have_unit = reported = false;
      atomic_store_explicit(
          &switch_ack_sequence,
          atomic_load_explicit(&switch_sequence, memory_order_relaxed),
          memory_order_release);
      tight_loop_contents();
      continue;
    }

    unsigned next_generation =
        atomic_load_explicit(&stream_generation, memory_order_acquire);
    if (next_generation != generation) {
      generation = next_generation;
      read = offset = 0;
      have_unit = reported = false;
      if (output_initialized) {
        spdif_deinit();
        output_initialized = false;
      }
      if (atomic_load_explicit(&stream_ready, memory_order_acquire)) {
        spdif_init(PICODAC_SPDIF_PIN, stream_rate, stream_depth,
                   stream_non_pcm);
        spdif_start();
        output_initialized = true;
        sd_eac3_status = 1;
      } else {
        sd_eac3_status =
            atomic_load_explicit(&producer_result, memory_order_acquire);
      }
    }

    if (!output_initialized || !cec_arc_audio_allowed()) {
      offset = 0;
#ifdef PICODAC_TEST_EXIT_ON_EOF
      if (!output_initialized &&
          atomic_load_explicit(&producer_result, memory_order_acquire))
        sleep_ms(1000);
#endif
      tight_loop_contents();
      continue;
    }
    if (!spdif_buffer_ready()) {
      tight_loop_contents();
      continue;
    }

    sd_diag_submit_begin();
    if (offset == 0) {
      have_unit = read !=
          atomic_load_explicit(&produced, memory_order_acquire);
      if (!have_unit) {
        int result =
            atomic_load_explicit(&producer_result, memory_order_acquire);
        if (!result) {
          ++sd_eac3_underruns;
        } else if (!reported) {
          have_unit = read !=
              atomic_load_explicit(&produced, memory_order_acquire);
          if (!have_unit) {
            sd_eac3_status = result;
            reported = true;
          }
        }
      }
    }

    int32_t *out = spdif_write_buffer();
    bool playing = sd_controls_playing();
    for (unsigned i = 0; i < SPDIF_BLOCK_FRAMES * 2; ++i) {
      if (!have_unit || !playing) {
        out[i] = 0;
      } else if (stream_non_pcm) {
        out[i] = (int16_t)
            audio_storage.compressed[read % COMPRESSED_SLOTS][offset + i];
      } else {
        out[i] = audio_storage.pcm[read % PCM_SLOTS][i];
      }
    }
    spdif_submit_buffer();
    sd_diag_submit_end();

    if (!playing) {
      offset = 0;
      continue;
    }
    offset += SPDIF_BLOCK_FRAMES * 2;
    if (offset == stream_words) {
      offset = 0;
      if (have_unit) {
        ++sd_eac3_bursts_played;
        atomic_store_explicit(&consumed, ++read, memory_order_release);
      }
    }
  }
}

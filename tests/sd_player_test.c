// Run the real player with threaded core1 and fake FatFs/SPI/DMA. Each process
// tests one boot, including short files, errors, and delayed sector reads.
#include <assert.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif
#include "eac3_burst.h"
#include "sd_eac3_player.h"
#include "sd_controls.h"
#include "spdif.h"
#include "spdif_encode.h"
#include "ff.h"
#include "tf_card.h"

extern volatile uint32_t sd_eac3_bursts_played, sd_eac3_underruns;
extern volatile int sd_eac3_status;
static uint8_t file_bytes[12 * 1024];
static size_t file_size, file_pos;
static int scenario;
static jmp_buf finished;
static int32_t output[384];
static bool started, acquired;
static unsigned data_offset, played, trailing;
static atomic_bool underrun_seen;
static bool gpio_values[30];
static uint32_t fake_time;
uint32_t time_us_32(void){return fake_time+=1000;}
void gpio_init(unsigned pin) {(void)pin;}
void gpio_set_dir(unsigned pin,int out){(void)pin;(void)out;}
void gpio_pull_up(unsigned pin){gpio_values[pin]=true;}
void gpio_put(unsigned pin,int value){gpio_values[pin]=value;}
int gpio_get(unsigned pin){return gpio_values[pin];}
#if PICODAC_CEC
static bool arc_gate, arc_paused;
static unsigned arc_wait = 5;
bool cec_arc_audio_allowed(void) { return arc_gate; }
void cec_arc_set_enabled(bool enabled) {(void)enabled;}
void cec_arc_request_playback(void) {}
void cec_arc_volume_key(bool up, bool pressed) {(void)up;(void)pressed;}
void cec_arc_task(void) {
  if (!arc_gate && arc_wait && !--arc_wait) arc_gate = true;
}
#endif
static void (*core_entry)(void);
#ifdef _WIN32
static HANDLE core_thread;
static DWORD WINAPI core_wrapper(void *ctx) { (void)ctx; core_entry(); return 0; }
void sleep_ms(unsigned ms) {
  if (ms == 1000 && sd_eac3_status != 0) longjmp(finished, 1);
  Sleep(ms);
}
#else
static pthread_t core_thread;
static void *core_wrapper(void *ctx) { (void)ctx; core_entry(); return NULL; }
void sleep_ms(unsigned ms) {
  if (ms == 1000 && sd_eac3_status != 0) longjmp(finished, 1);
  struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000};
  nanosleep(&delay, NULL);
}
#endif
void sleep_us(unsigned us) { (void)us; sleep_ms(1); }
void multicore_launch_core1(void (*entry)(void)) {
  core_entry = entry;
#ifdef _WIN32
  core_thread = CreateThread(NULL, 0, core_wrapper, NULL, 0, NULL);
  assert(core_thread);
#else
  assert(pthread_create(&core_thread, NULL, core_wrapper, NULL) == 0);
#endif
}
void tight_loop_contents(void) { sleep_ms(1); }
bool pico_fatfs_set_config(pico_fatfs_spi_config_t *c) {
  assert(c->spi_inst == spi0 && c->pin_sck == 2 && c->pin_mosi == 3 &&
         c->pin_miso == 4 && c->pin_cs == 5);
  return true;
}
FRESULT f_mount(FATFS *fs, const char *path, int mount) {
  (void)fs; assert(strcmp(path, "0:") == 0);
  return scenario == 2 && mount ? 3 : FR_OK;
}
FRESULT f_open(FIL *file, const char *path, int mode) {
  (void)file;
  assert(strcmp(path, "0:/TRACK.EC3") == 0 && mode == FA_READ);
  return scenario == 3 ? 4 : FR_OK;
}
FRESULT f_read(FIL *file, void *buffer, UINT size, UINT *count) {
  (void)file;
  if (scenario == 4 && file_pos >= 8192) return 1;
  if (scenario == 5 && file_pos == 8192)
    while (!atomic_load(&underrun_seen)) sleep_ms(1);
  if (size > file_size - file_pos) size = (UINT)(file_size - file_pos);
  memcpy(buffer, file_bytes + file_pos, size);
  *count = size; file_pos += size;
  return FR_OK;
}
FRESULT f_close(FIL *file) { (void)file; return FR_OK; }
FRESULT f_opendir(DIR*d,const char*p){(void)d;(void)p;return 1;}
FRESULT f_readdir(DIR*d,FILINFO*f){(void)d;f->fname[0]=0;return FR_OK;}
FRESULT f_closedir(DIR*d){(void)d;return FR_OK;}
void spdif_init(unsigned pin, uint32_t rate, uint8_t depth, bool non_pcm) {
  assert(pin == 22 && rate == 192000 && depth == 16 && non_pcm);
}
void spdif_start(void) { started = true; }
void spdif_deinit(void) { started = false; }
bool spdif_buffer_ready(void) {
  assert(started && !acquired);
#if PICODAC_CEC
  assert(arc_gate);
  if (scenario == 6 && played == 1 && data_offset == 768 && !arc_paused) {
    arc_paused = true; arc_gate = false; arc_wait = 5;
    data_offset = 0; // stopped DMA: next output must restart a complete burst
    return false;
  }
#endif
  sleep_ms(1);
  acquired = true;
  return true;
}
int32_t *spdif_write_buffer(void) { assert(acquired); return output; }
void spdif_submit_buffer(void) {
  assert(acquired); acquired = false;
  if (!data_offset && (uint16_t)output[0] != 0xf872) {
    for (unsigned i = 0; i < 384; ++i) assert(output[i] == 0);
    if (sd_eac3_status == 1) atomic_store(&underrun_seen, true);
    if (sd_eac3_status != 1 && ++trailing == 64) longjmp(finished, 1);
    return;
  }
  for (unsigned i = 0; i < 384; ++i) {
    unsigned p = data_offset + i;
    uint16_t expected = 0;
    if (p == 0) expected = 0xf872;
    else if (p == 1) expected = 0x4e1f;
    else if (p == 2) expected = 0x15;
    else if (p == 3) expected = 1024;
    else if (p < 516) {
      size_t byte = played * 1024 + (p - 4) * 2;
      assert(byte + 1 < file_size);
      expected = (uint16_t)file_bytes[byte] << 8 | file_bytes[byte + 1];
    }
    assert((uint16_t)output[i] == expected);
  }
  data_offset += 384;
  if (data_offset == EAC3_BURST_WORDS) { data_offset = 0; ++played; }
}

int main(int argc, char **argv) {
  assert(argc == 2);
  scenario = argv[1][0] - '0';
  file_size = scenario == 1 ? 1024 : sizeof(file_bytes);
  for (size_t i = 0; i < file_size; ++i) file_bytes[i] = (uint8_t)(i * 177 + i / 1024);
  for (size_t i = 0; i < file_size; i += 1024) {
    const uint8_t header[] = {0x0b, 0x77, 0x01, 0xff, 0x34, 0x80};
    memcpy(file_bytes + i, header, sizeof(header));
  }
  // No-stream failures exit via the sleep_ms(1000) idle hook.
  if (!setjmp(finished)) sd_eac3_player_run();
#ifdef _WIN32
  assert(WaitForSingleObject(core_thread, 5000) == WAIT_OBJECT_0);
  CloseHandle(core_thread);
#else
  assert(pthread_join(core_thread, NULL) == 0);
#endif
  if (scenario == 2 || scenario == 3) {
    assert(sd_eac3_status == EAC3_IO_ERROR && played == 0 && !started);
  } else if (scenario == 4) {
    assert(sd_eac3_status == EAC3_IO_ERROR && played == 7);
  } else {
    assert(sd_eac3_status == 2 && played == file_size / 1024);
    assert(!sd_controls_playing());
  }
  assert(sd_eac3_bursts_played == played);
  if (scenario == 5) assert(sd_eac3_underruns > 0);
#if PICODAC_CEC
  assert(arc_paused && arc_gate);
#endif
  puts("PASS: threaded TF reader -> complete IEC61937 bursts -> SPDIF consumer, EOF/error drain");
}

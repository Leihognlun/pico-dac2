#include <assert.h>
#include <stdio.h>
#include "spdif_dual.c"
fake_pio_t fake_pio;
unsigned fake_sm_count, fake_dma_count, fake_enabled_mask;
bool fake_pending[2];
const void *fake_dma_read[2];
unsigned fake_function[30];
static void complete(unsigned p) { fake_pending[p] = true; spdif_dma_handler(); }
static void pair(unsigned first) { complete(first); complete(1 - first); }
static int submit(bool start) {
  assert(spdif_buffer_ready());
  int slot = blocks.writing;
  int32_t *samples = spdif_write_buffer();
  for (unsigned i = 0; i < SPDIF_BLOCK_FRAMES * 2; ++i) samples[i] = i;
  spdif_set_burst_start(start); spdif_submit_buffer(); return slot;
}
int main(void) {
  spdif_init(23, 192000, 16, true); spdif_start();
  assert(fake_enabled_mask == 3);
  assert(fake_function[23] == 6 && fake_function[18] == 6);
  submit(true); pair(0);
  assert(fake_dma_read[0] == silence && fake_dma_read[1] == silence);
  spdif_set_port_playing(0, true);
  int a = submit(true); pair(1);
  assert(fake_dma_read[0] == encoded[a] && fake_dma_read[1] == silence);
  assert(fake_function[23] == 6 && fake_function[18] == 6);
  spdif_set_port_playing(1, true);
  assert(!playing[0] && playing[1]);
  assert(fake_function[23] == 6 && fake_function[18] == 6);
  a = submit(false); pair(0);
  assert(fake_dma_read[0] == silence && fake_dma_read[1] == silence);
  a = submit(true); pair(1);
  assert(fake_dma_read[0] == silence && fake_dma_read[1] == encoded[a]);
  a = submit(false); complete(0);
  assert(!spdif_buffer_ready());
  // Switching between completions must route the remaining consumer to idle.
  spdif_set_port_playing(0, true); complete(1);
  assert(fake_function[23] == 6 && fake_function[18] == 6);
  assert(fake_dma_read[1] == silence);
  a = submit(false); pair(0);
  assert(fake_dma_read[0] == silence && fake_dma_read[1] == silence);
  a = submit(true); pair(1);
  assert(fake_dma_read[0] == encoded[a] && fake_dma_read[1] == silence);
  spdif_set_port_link_enabled(0, false);
  assert(fake_function[23] == GPIO_FUNC_SIO && fake_function[18] == 6);
  spdif_set_port_link_enabled(0, true);
  a = submit(false); pair(0); assert(fake_dma_read[0] == silence);
  a = submit(true); pair(1); assert(fake_dma_read[0] == encoded[a]);
  spdif_set_port_playing(0, false);
  assert(fake_function[23] == 6 && fake_function[18] == 6);
  for (unsigned i = 0; i < 100; ++i) {
    pair(i & 1);
    assert(fake_dma_read[0] == silence && fake_dma_read[1] == silence);
    assert(fake_enabled_mask == 3);
  }
  spdif_deinit();
  assert(!fake_enabled_mask && !fake_sm_count && !fake_dma_count);
  spdif_init(23, 48000, 24, false); spdif_start();
  a = submit(false); pair(0);
  assert(fake_dma_read[0] == silence && fake_dma_read[1] == silence);
  spdif_set_port_playing(1, true);
  a = submit(false); pair(1); assert(fake_dma_read[1] == encoded[a]);
  spdif_stop(); spdif_start();
  assert(fake_function[23] == 6 && fake_function[18] == 6);
  spdif_deinit();
  puts("PASS: dual ARC idle carriers, exclusive payload, link gating, burst rejoin and DMA ownership");
}

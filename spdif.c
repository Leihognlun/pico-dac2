#include "spdif.h"

#include <assert.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "spdif.pio.h"
#include "spdif_encode.h"

#define SPDIF_PIO pio0
#define SPDIF_DMA_IRQ 1

// Only completed blocks are published to the ISR. Never encode into a
// DMA-owned buffer. A whole silent block preserves channel-status framing.
static uint32_t encoded[2][SPDIF_BLOCK_WORDS];
static uint32_t silence[SPDIF_BLOCK_WORDS];
static int32_t pcm[SPDIF_BLOCK_FRAMES * 2];
static volatile int playing = -1;
static volatile int queued = -1;
static int writing = -1;
static uint sm, offset, dma_channel, output_pin;
static uint32_t rate;
static uint8_t depth;
static bool stream_non_pcm;
static bool initialized, running;

static void __isr __time_critical_func(spdif_dma_handler)(void) {
  if (!dma_irqn_get_channel_status(SPDIF_DMA_IRQ, dma_channel)) return;
  dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma_channel);
  playing = queued;
  queued = -1;
  dma_channel_set_read_addr(dma_channel,
                            playing < 0 ? silence : encoded[playing], true);
}

void spdif_init(unsigned pin, uint32_t sample_rate, uint8_t bit_depth,
                bool non_pcm) {
  assert(!initialized);
  assert(pin < NUM_BANK0_GPIOS);
  assert(bit_depth == 16 || bit_depth == 24 || bit_depth == 32);
  output_pin = pin;
  rate = sample_rate;
  depth = bit_depth;
  stream_non_pcm = non_pcm;
  spdif_encode_init();
  // During starvation keep the Non-PCM status, including on zero padding.
  // Do not make a compressed receiver switch to PCM in the middle of a burst.
  spdif_encode_block(silence, NULL, depth, rate, stream_non_pcm);
  sm = pio_claim_unused_sm(SPDIF_PIO, true);
  offset = pio_add_program(SPDIF_PIO, &picodac_spdif_program);
  pio_gpio_init(SPDIF_PIO, pin);
  pio_sm_config config = picodac_spdif_program_get_default_config(offset);
  sm_config_set_sideset_pins(&config, pin);
  sm_config_set_out_shift(&config, true, true, 32);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
  // 256 cycles/frame; divider is represented in units of 1/256.
  uint32_t divider = (clock_get_hz(clk_sys) + rate / 2) / rate;
  assert(divider >= 256 && divider < 0x1000000);
  sm_config_set_clkdiv_int_frac(&config, divider >> 8, divider & 255);
  pio_sm_init(SPDIF_PIO, sm, offset, &config);
  pio_sm_set_consecutive_pindirs(SPDIF_PIO, sm, pin, 1, true);
  pio_sm_set_pins_with_mask(SPDIF_PIO, sm, 0, 1u << pin);

  dma_channel = dma_claim_unused_channel(true);
  dma_channel_config dc = dma_channel_get_default_config(dma_channel);
  channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
  channel_config_set_read_increment(&dc, true);
  channel_config_set_write_increment(&dc, false);
  channel_config_set_dreq(&dc, pio_get_dreq(SPDIF_PIO, sm, true));
  channel_config_set_high_priority(&dc, true);
  dma_channel_configure(dma_channel, &dc, &SPDIF_PIO->txf[sm], silence,
                        dma_encode_transfer_count(SPDIF_BLOCK_WORDS), false);
  dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma_channel);
  irq_add_shared_handler(DMA_IRQ_NUM(SPDIF_DMA_IRQ), spdif_dma_handler,
                         PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
  // Refill before the FIFO drains, even when USB has an interrupt pending.
  irq_set_priority(DMA_IRQ_NUM(SPDIF_DMA_IRQ), PICO_HIGHEST_IRQ_PRIORITY);
  irq_set_enabled(DMA_IRQ_NUM(SPDIF_DMA_IRQ), true);
  playing = queued = writing = -1;
  initialized = true;
}

void spdif_start(void) {
  if (!initialized || running) return;
  running = true;
  dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, dma_channel, true);
  dma_channel_set_read_addr(dma_channel, silence, true);
  // Fill the FIFO before starting the serializer.
  while (!pio_sm_is_tx_fifo_full(SPDIF_PIO, sm)) tight_loop_contents();
  pio_sm_set_enabled(SPDIF_PIO, sm, true);
}

void spdif_stop(void) {
  if (!initialized) return;
  dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, dma_channel, false);
  pio_sm_set_enabled(SPDIF_PIO, sm, false);
  dma_channel_abort(dma_channel);
  dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma_channel);
  pio_sm_clear_fifos(SPDIF_PIO, sm);
  pio_sm_restart(SPDIF_PIO, sm);
  pio_sm_clkdiv_restart(SPDIF_PIO, sm);
  pio_sm_exec(SPDIF_PIO, sm, pio_encode_jmp(offset));
  pio_sm_set_pins_with_mask(SPDIF_PIO, sm, 0, 1u << output_pin);
  playing = queued = writing = -1;
  running = false;
}

void spdif_deinit(void) {
  if (!initialized) return;
  spdif_stop();
  irq_remove_handler(DMA_IRQ_NUM(SPDIF_DMA_IRQ), spdif_dma_handler);
  dma_channel_unclaim(dma_channel);
  pio_remove_program(SPDIF_PIO, &picodac_spdif_program, offset);
  pio_sm_unclaim(SPDIF_PIO, sm);
  gpio_init(output_pin);
  gpio_set_dir(output_pin, GPIO_OUT);
  gpio_put(output_pin, false);
  initialized = false;
}

bool spdif_buffer_ready(void) {
  if (!running || writing >= 0) return false;
  uint32_t interrupts = save_and_disable_interrupts();
  bool ready = queued < 0;
  if (ready) writing = playing == 0 ? 1 : 0;
  restore_interrupts(interrupts);
  return ready;
}

int32_t *spdif_write_buffer(void) {
  assert(writing >= 0);
  return pcm;
}

void spdif_submit_buffer(void) {
  assert(writing >= 0);
  spdif_encode_block(encoded[writing], pcm, depth, rate, stream_non_pcm);
  uint32_t interrupts = save_and_disable_interrupts();
  __mem_fence_release();
  queued = writing;
  writing = -1;
  restore_interrupts(interrupts);
}

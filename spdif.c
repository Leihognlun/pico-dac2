#include "spdif.h"

#include <assert.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "spdif.pio.h"
#include "spdif_encode.h"
#include "audio_block_queue.h"
#if PICODAC_OUTPUT_BOTH
#include "i2s_mirror.h"
#include "i2s_mirror.pio.h"
#endif

#define SPDIF_PIO pio0
#define SPDIF_DMA_IRQ 1

// Only completed blocks are published to the ISR. Never encode into a
// DMA-owned buffer. A whole silent block preserves channel-status framing.
static uint32_t encoded[2][SPDIF_BLOCK_WORDS];
static uint32_t silence[SPDIF_BLOCK_WORDS];
static int32_t pcm[SPDIF_BLOCK_FRAMES * 2];
static audio_block_queue_t blocks;
#if PICODAC_OUTPUT_BOTH
static uint32_t mirror[2][SPDIF_BLOCK_FRAMES * 2];
static uint32_t mirror_silence[SPDIF_BLOCK_FRAMES * 2];
static uint mirror_sm, mirror_offset, mirror_dma;
#define OUTPUT_CONSUMERS 3u
#else
#define OUTPUT_CONSUMERS 1u
#endif
static uint sm, offset, dma_channel, output_pin;
static uint32_t rate;
static uint8_t depth;
static bool stream_non_pcm;
static bool initialized, running;
static bool link_enabled = true, start_requested;
// Sticky PIO flags are sampled once per block. Counts indicate affected
// intervals, not the exact number of stalled clock cycles.
volatile uint32_t spdif_tx_stall_count;
volatile uint32_t i2s_tx_stall_count;
volatile uint32_t spdif_silence_block_count;

static void record_tx_stall(uint state_machine, volatile uint32_t *count) {
  uint32_t mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + state_machine);
  if (SPDIF_PIO->fdebug & mask) {
    ++*count;
    SPDIF_PIO->fdebug = mask;
  }
}

static void __isr __time_critical_func(spdif_dma_handler)(void) {
  if (dma_irqn_get_channel_status(SPDIF_DMA_IRQ, dma_channel)) {
    dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma_channel);
    record_tx_stall(sm, &spdif_tx_stall_count);
    int next = audio_block_queue_complete(&blocks, 1u);
    if (next < 0) ++spdif_silence_block_count;
    dma_channel_set_read_addr(dma_channel,
                              next < 0 ? silence : encoded[next], true);
  }
#if PICODAC_OUTPUT_BOTH
  if (dma_irqn_get_channel_status(SPDIF_DMA_IRQ, mirror_dma)) {
    dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, mirror_dma);
    record_tx_stall(mirror_sm, &i2s_tx_stall_count);
    int next = audio_block_queue_complete(&blocks, 2u);
    dma_channel_set_read_addr(mirror_dma,
                              next < 0 ? mirror_silence : mirror[next], true);
  }
#endif
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
#if PICODAC_OUTPUT_BOTH
  // Same PIO clock and exactly 256 cycles/frame as SPDIF, at every bit depth.
  mirror_sm = pio_claim_unused_sm(SPDIF_PIO, true);
  mirror_offset = pio_add_program(SPDIF_PIO, &i2s_mirror_program);
  pio_sm_config mc = i2s_mirror_program_get_default_config(mirror_offset);
  sm_config_set_out_pins(&mc, PICODAC_I2S_DATA_PIN, 1);
  sm_config_set_sideset_pins(&mc, PICODAC_I2S_BASE_CLOCK_PIN);
  sm_config_set_out_shift(&mc, false, true, 32);
  sm_config_set_fifo_join(&mc, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv_int_frac(&mc, divider >> 8, divider & 255);
  pio_gpio_init(SPDIF_PIO, PICODAC_I2S_DATA_PIN);
  pio_gpio_init(SPDIF_PIO, PICODAC_I2S_BASE_CLOCK_PIN);
  pio_gpio_init(SPDIF_PIO, PICODAC_I2S_BASE_CLOCK_PIN + 1);
  pio_sm_init(SPDIF_PIO, mirror_sm, mirror_offset, &mc);
  uint32_t pins = (1u << PICODAC_I2S_DATA_PIN) |
                  (3u << PICODAC_I2S_BASE_CLOCK_PIN);
  pio_sm_set_pindirs_with_mask(SPDIF_PIO, mirror_sm, pins, pins);
  pio_sm_set_pins_with_mask(SPDIF_PIO, mirror_sm, 0, pins);
  mirror_dma = dma_claim_unused_channel(true);
  // Obtain a fresh config: default CHAIN_TO must refer to this channel,
  // otherwise finishing I2S would spuriously trigger the SPDIF DMA.
  dc = dma_channel_get_default_config(mirror_dma);
  channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
  channel_config_set_read_increment(&dc, true);
  channel_config_set_write_increment(&dc, false);
  channel_config_set_high_priority(&dc, true);
  channel_config_set_dreq(&dc, pio_get_dreq(SPDIF_PIO, mirror_sm, true));
  dma_channel_configure(mirror_dma, &dc, &SPDIF_PIO->txf[mirror_sm],
                        mirror_silence,
                        dma_encode_transfer_count(SPDIF_BLOCK_FRAMES * 2), false);
  dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, mirror_dma);
#endif
  dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma_channel);
  irq_add_shared_handler(DMA_IRQ_NUM(SPDIF_DMA_IRQ), spdif_dma_handler,
                         PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
  // Refill before the FIFO drains, even when USB has an interrupt pending.
  irq_set_priority(DMA_IRQ_NUM(SPDIF_DMA_IRQ), PICO_HIGHEST_IRQ_PRIORITY);
  irq_set_enabled(DMA_IRQ_NUM(SPDIF_DMA_IRQ), true);
  audio_block_queue_reset(&blocks, OUTPUT_CONSUMERS);
  initialized = true;
}

void spdif_start(void) {
  start_requested = true;
  if (!initialized || running || !link_enabled) return;
  // Ignore flags left by initialization or the preceding stream.
  SPDIF_PIO->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
#if PICODAC_OUTPUT_BOTH
  SPDIF_PIO->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + mirror_sm);
#endif
  running = true;
  dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, dma_channel, true);
  dma_channel_set_read_addr(dma_channel, silence, true);
#if PICODAC_OUTPUT_BOTH
  dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, mirror_dma, true);
  dma_channel_set_read_addr(mirror_dma, mirror_silence, true);
#endif
  // Fill the FIFO before starting the serializer.
  while (!pio_sm_is_tx_fifo_full(SPDIF_PIO, sm)) tight_loop_contents();
#if PICODAC_OUTPUT_BOTH
  while (!pio_sm_is_tx_fifo_full(SPDIF_PIO, mirror_sm)) tight_loop_contents();
  pio_enable_sm_mask_in_sync(SPDIF_PIO, (1u << sm) | (1u << mirror_sm));
#else
  pio_enable_sm_mask_in_sync(SPDIF_PIO, 1u << sm);
#endif
}

void spdif_stop(void) {
  start_requested = false;
  if (!initialized) return;
  dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, dma_channel, false);
#if PICODAC_OUTPUT_BOTH
  dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, mirror_dma, false);
  pio_set_sm_mask_enabled(SPDIF_PIO, (1u << sm) | (1u << mirror_sm), false);
  dma_channel_abort(mirror_dma);
  dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, mirror_dma);
  pio_sm_clear_fifos(SPDIF_PIO, mirror_sm);
  pio_sm_restart(SPDIF_PIO, mirror_sm);
  pio_sm_exec(SPDIF_PIO, mirror_sm, pio_encode_jmp(mirror_offset));
  pio_sm_set_pins_with_mask(SPDIF_PIO, mirror_sm, 0,
      (1u << PICODAC_I2S_DATA_PIN) | (3u << PICODAC_I2S_BASE_CLOCK_PIN));
#endif
  pio_sm_set_enabled(SPDIF_PIO, sm, false);
  dma_channel_abort(dma_channel);
  dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma_channel);
  pio_sm_clear_fifos(SPDIF_PIO, sm);
  pio_sm_restart(SPDIF_PIO, sm);
  pio_sm_clkdiv_restart(SPDIF_PIO, sm);
  pio_sm_exec(SPDIF_PIO, sm, pio_encode_jmp(offset));
  pio_sm_set_pins_with_mask(SPDIF_PIO, sm, 0, 1u << output_pin);
  audio_block_queue_reset(&blocks, OUTPUT_CONSUMERS);
  running = false;
}

void spdif_deinit(void) {
  if (!initialized) return;
  spdif_stop();
  irq_remove_handler(DMA_IRQ_NUM(SPDIF_DMA_IRQ), spdif_dma_handler);
  dma_channel_unclaim(dma_channel);
#if PICODAC_OUTPUT_BOTH
  dma_channel_unclaim(mirror_dma);
  pio_remove_program(SPDIF_PIO, &i2s_mirror_program, mirror_offset);
  pio_sm_unclaim(SPDIF_PIO, mirror_sm);
#endif
  pio_remove_program(SPDIF_PIO, &picodac_spdif_program, offset);
  pio_sm_unclaim(SPDIF_PIO, sm);
  gpio_init(output_pin);
  gpio_set_dir(output_pin, GPIO_OUT);
  gpio_put(output_pin, false);
  initialized = false;
}

void spdif_set_link_enabled(bool enabled) {
  if (link_enabled == enabled) return;
  link_enabled = enabled;
  if (!enabled) {
    bool requested = start_requested;
    spdif_stop();
    start_requested = requested;
  } else if (start_requested) spdif_start();
}

bool spdif_buffer_ready(void) {
  if (!running) return false;
  uint32_t interrupts = save_and_disable_interrupts();
  bool ready = audio_block_queue_acquire(&blocks);
  restore_interrupts(interrupts);
  return ready;
}

int32_t *spdif_write_buffer(void) {
  assert(blocks.writing >= 0);
  return pcm;
}

void spdif_submit_buffer(void) {
  assert(blocks.writing >= 0);
  spdif_encode_block(encoded[blocks.writing], pcm, depth, rate, stream_non_pcm);
#if PICODAC_OUTPUT_BOTH
  for (unsigned i = 0; i < SPDIF_BLOCK_FRAMES * 2; ++i) {
    mirror[blocks.writing][i] = i2s_mirror_sample(pcm[i], depth, stream_non_pcm);
  }
#endif
  uint32_t interrupts = save_and_disable_interrupts();
  __mem_fence_release();
  audio_block_queue_publish(&blocks);
  restore_interrupts(interrupts);
}

#include "spdif.h"
#include "board.h"
#include "spdif_encode.h"
#include "audio_block_queue.h"
#include "spdif.pio.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include <assert.h>

// Both consumers always run together. Link loss only gates that port's GPIO;
// it cannot stop the other port or release a DMA-owned buffer prematurely.
#define SPDIF_PIO pio0
#define SPDIF_DMA_IRQ 1
static const unsigned pins[2] = {BOARD_ARC1_TX, BOARD_ARC2_TX};
static uint sm[2], dma[2], offset;
static uint32_t encoded[2][SPDIF_BLOCK_WORDS], silence[SPDIF_BLOCK_WORDS];
static int32_t pcm[SPDIF_BLOCK_FRAMES * 2];
static audio_block_queue_t blocks;
static bool boundary[2], explicit_boundary;
static bool initialized, running, non_pcm;
static bool link[2] = {true, true}, playing[2];
static bool active[2];
static unsigned selected_mask;
static uint32_t rate;
static uint8_t depth;
volatile uint32_t spdif_tx_stall_count, i2s_tx_stall_count;
volatile uint32_t spdif_silence_block_count;

static void gate_pin(unsigned port) {
  // SIO output is prepared before selecting it, so a gated port stays low.
  gpio_put(pins[port], false);
  gpio_set_dir(pins[port], GPIO_OUT);
  if (initialized && running && link[port])
    pio_gpio_init(SPDIF_PIO, pins[port]);
  else gpio_set_function(pins[port], GPIO_FUNC_SIO);
}
static void __isr __time_critical_func(spdif_dma_handler)(void) {
  for (unsigned port = 0; port < 2; ++port) {
    if (!dma_irqn_get_channel_status(SPDIF_DMA_IRQ, dma[port])) continue;
    dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma[port]);
    uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm[port]);
    if (SPDIF_PIO->fdebug & stall) {
      ++spdif_tx_stall_count; SPDIF_PIO->fdebug = stall;
    }
    bool first = blocks.completed == 0;
    int next = audio_block_queue_complete(&blocks, 1u << port);
    if (first) {
      selected_mask = 0;
      if (next < 0) ++spdif_silence_block_count;
      for (unsigned i = 0; i < 2; ++i) {
        if (!link[i] || !playing[i] || next < 0) active[i] = false;
        else if (!non_pcm || boundary[next]) active[i] = true;
        if (active[i]) selected_mask |= 1u << i;
      }
    }
    dma_channel_set_read_addr(dma[port],
        next >= 0 && (selected_mask & (1u << port)) ? encoded[next] : silence, true);
  }
}
void spdif_init(unsigned pin, uint32_t sample_rate, uint8_t bit_depth, bool compressed) {
  assert(!initialized && pin == BOARD_ARC1_TX);
  assert(bit_depth == 16 || bit_depth == 24 || bit_depth == 32);
  rate = sample_rate; depth = bit_depth; non_pcm = compressed;
  explicit_boundary = false;
  spdif_encode_init();
  spdif_encode_idle_block(silence, depth, rate, non_pcm);
  offset = pio_add_program(SPDIF_PIO, &picodac_spdif_program);
  uint32_t divider = (clock_get_hz(clk_sys) + rate / 2) / rate;
  assert(divider >= 256 && divider < 0x1000000);
  for (unsigned i = 0; i < 2; ++i) {
    sm[i] = pio_claim_unused_sm(SPDIF_PIO, true);
    gpio_init(pins[i]);
    pio_sm_config config = picodac_spdif_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, pins[i]);
    sm_config_set_out_shift(&config, true, true, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&config, divider >> 8, divider & 255);
    pio_sm_init(SPDIF_PIO, sm[i], offset, &config);
    pio_sm_set_consecutive_pindirs(SPDIF_PIO, sm[i], pins[i], 1, true);
    pio_sm_set_pins_with_mask(SPDIF_PIO, sm[i], 0, 1u << pins[i]);
    dma[i] = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma[i]);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(SPDIF_PIO, sm[i], true));
    channel_config_set_high_priority(&dc, true);
    dma_channel_configure(dma[i], &dc, &SPDIF_PIO->txf[sm[i]], silence,
                         dma_encode_transfer_count(SPDIF_BLOCK_WORDS), false);
    dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma[i]);
  }
  audio_block_queue_reset(&blocks, 3u);
  irq_add_shared_handler(DMA_IRQ_NUM(SPDIF_DMA_IRQ), spdif_dma_handler,
                         PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
  irq_set_priority(DMA_IRQ_NUM(SPDIF_DMA_IRQ), PICO_HIGHEST_IRQ_PRIORITY);
  irq_set_enabled(DMA_IRQ_NUM(SPDIF_DMA_IRQ), true);
  initialized = true;
  for (unsigned i = 0; i < 2; ++i) gate_pin(i);
}
void spdif_start(void) {
  if (!initialized || running) return;
  running = true;
  for (unsigned i = 0; i < 2; ++i) {
    active[i] = false;
    SPDIF_PIO->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm[i]);
    gate_pin(i);
    dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, dma[i], true);
    dma_channel_set_read_addr(dma[i], silence, true);
    while (!pio_sm_is_tx_fifo_full(SPDIF_PIO, sm[i])) tight_loop_contents();
  }
  pio_enable_sm_mask_in_sync(SPDIF_PIO, (1u << sm[0]) | (1u << sm[1]));
}
void spdif_stop(void) {
  if (!initialized) return;
  uint32_t irq = save_and_disable_interrupts();
  for (unsigned i = 0; i < 2; ++i)
    dma_irqn_set_channel_enabled(SPDIF_DMA_IRQ, dma[i], false);
  pio_set_sm_mask_enabled(SPDIF_PIO, (1u << sm[0]) | (1u << sm[1]), false);
  running = false;
  for (unsigned i = 0; i < 2; ++i) {
    dma_channel_abort(dma[i]);
    dma_irqn_acknowledge_channel(SPDIF_DMA_IRQ, dma[i]);
    pio_sm_clear_fifos(SPDIF_PIO, sm[i]);
    pio_sm_restart(SPDIF_PIO, sm[i]);
    pio_sm_exec(SPDIF_PIO, sm[i], pio_encode_jmp(offset));
    active[i] = false;
    gate_pin(i);
  }
  audio_block_queue_reset(&blocks, 3u);
  restore_interrupts(irq);
}
void spdif_deinit(void) {
  if (!initialized) return;
  spdif_stop();
  irq_remove_handler(DMA_IRQ_NUM(SPDIF_DMA_IRQ), spdif_dma_handler);
  for (unsigned i = 0; i < 2; ++i) {
    dma_channel_unclaim(dma[i]);
    pio_sm_unclaim(SPDIF_PIO, sm[i]);
  }
  pio_remove_program(SPDIF_PIO, &picodac_spdif_program, offset);
  initialized = false;
}
void spdif_set_port_link_enabled(unsigned port, bool enabled) {
  assert(port < 2);
  uint32_t irq = save_and_disable_interrupts();
  if (link[port] != enabled) active[port] = false;
  link[port] = enabled;
  if (initialized) gate_pin(port);
  restore_interrupts(irq);
}
void spdif_set_link_enabled(bool enabled) {
  for (unsigned i = 0; i < 2; ++i) spdif_set_port_link_enabled(i, enabled);
}
void spdif_set_port_playing(unsigned port, bool enabled) {
  assert(port < 2);
  uint32_t irq = save_and_disable_interrupts();
  // Only the selected port receives payload; other linked ports keep sending
  // invalid zero-data frames. Selection takes effect at DMA block boundaries.
  if (enabled) {
    unsigned other = 1u - port;
    playing[other] = active[other] = false;
    selected_mask &= ~(1u << other);
    if (initialized) gate_pin(other);
  }
  playing[port] = enabled;
  if (!enabled) {
    active[port] = false;
    selected_mask &= ~(1u << port);
  }
  if (initialized) gate_pin(port);
  restore_interrupts(irq);
}
void spdif_set_burst_start(bool start) { explicit_boundary = start; }
bool spdif_buffer_ready(void) {
  if (!running || (!link[0] && !link[1])) return false;
  uint32_t irq = save_and_disable_interrupts();
  bool ready = audio_block_queue_acquire(&blocks);
  restore_interrupts(irq);
  return ready;
}
int32_t *spdif_write_buffer(void) { assert(blocks.writing >= 0); return pcm; }
void spdif_submit_buffer(void) {
  assert(blocks.writing >= 0);
  bool start = !non_pcm || explicit_boundary;
#if !PICODAC_INPUT_SD
  // USB IEC headers need not align to a 192-frame SPDIF block.
  for (unsigned i = 0; non_pcm && i + 1 < SPDIF_BLOCK_FRAMES * 2; i += 2)
    if ((uint16_t)pcm[i] == 0xf872 && (uint16_t)pcm[i + 1] == 0x4e1f) start = true;
#endif
  boundary[blocks.writing] = start;
  spdif_encode_block(encoded[blocks.writing], pcm, depth, rate, non_pcm);
  uint32_t irq = save_and_disable_interrupts();
  __mem_fence_release();
  audio_block_queue_publish(&blocks);
  restore_interrupts(irq);
}

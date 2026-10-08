#include "i2s.h"

#include <assert.h>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "i2s.pio.h"
#include "log.h"

// --- Hardware Mute Pin ---
// To enable hardware mute, define I2S_MUTE_PIN to a valid GPIO number.
// #define I2S_MUTE_PIN 28

#define MAX_I2S_SAMPLE_RATE 96000
#define MAX_BUFFER_FRAMES (MAX_I2S_SAMPLE_RATE / 1000)
#define DMA_IRQ_INDEX 0
#define DMA_IRQ DMA_IRQ_NUM(DMA_IRQ_INDEX)

// --- Module-level static variables ---
static uint pio_sm = 0;
static uint pio_offset = 0;
static const pio_program_t *loaded_pio_program = NULL;

// Pointers to track which buffer is for writing and which is for DMA
static int32_t dma_buffer[2][MAX_BUFFER_FRAMES * 2];
static int32_t *volatile write_buffer = NULL;
static int32_t *volatile read_buffer = NULL;
static volatile uint current_dma_channel;

// Flag to notify application that a buffer is ready for writing
static volatile bool buffer_ready = false;
static bool initialized = false;

// #define TRACE_LOG LOG_DEBUG
#define TRACE_LOG

// DMA interrupt handler
static void dma_irq_handler() {
  // Acknowledge the interrupt for our channel
  dma_irqn_acknowledge_channel(DMA_IRQ_INDEX, current_dma_channel);

  // Swap buffers
  int32_t *volatile temp = write_buffer;
  write_buffer = read_buffer;
  read_buffer = temp;

  // Start DMA on the new read_buffer
  dma_channel_set_read_addr(current_dma_channel, read_buffer, true);

  // Set flag for the application
  buffer_ready = true;
}

static void dma_init(const i2s_config_t *config) {
  TRACE_LOG("dma_init begin\n");
  PIO pio = config->pio_instance;

  write_buffer = dma_buffer[0];
  read_buffer = dma_buffer[1];

  current_dma_channel = dma_claim_unused_channel(true);
  dma_channel_config dma_config =
      dma_channel_get_default_config(current_dma_channel);
  channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
  channel_config_set_read_increment(&dma_config, true);
  channel_config_set_write_increment(&dma_config, false);
  channel_config_set_dreq(&dma_config, pio_get_dreq(pio, pio_sm, true));
  dma_channel_configure(current_dma_channel, &dma_config, &pio->txf[pio_sm],
                        NULL,  // Read address (set later)
                        dma_encode_transfer_count(config->buffer_frames * 2),
                        false  // Don't start yet
  );

  // --- IRQ setup ---
  dma_irqn_acknowledge_channel(DMA_IRQ_INDEX, current_dma_channel);
  irq_set_exclusive_handler(DMA_IRQ, dma_irq_handler);
  irq_set_enabled(DMA_IRQ, true);
  TRACE_LOG("dma_init end\n");
}

static void dma_start() {
  TRACE_LOG("dma_start begin\n");
  int32_t *volatile temp = write_buffer;
  write_buffer = read_buffer;
  read_buffer = temp;

  dma_irqn_set_channel_enabled(DMA_IRQ_INDEX, current_dma_channel, true);
  dma_channel_set_read_addr(current_dma_channel, read_buffer, true);
  buffer_ready = true;
  TRACE_LOG("dma_start end\n");
}

static void dma_stop() {
  TRACE_LOG("dma_stop begin\n");
  dma_irqn_set_channel_enabled(DMA_IRQ_INDEX, current_dma_channel, false);
  dma_channel_abort(current_dma_channel);
  while (dma_channel_is_busy(current_dma_channel));
  dma_irqn_acknowledge_channel(DMA_IRQ_INDEX, current_dma_channel);
  buffer_ready = true;
  TRACE_LOG("dma_stop end\n");
}

static void dma_deinit() {
  TRACE_LOG("dma_deinit begin\n");
  dma_stop();

  irq_remove_handler(DMA_IRQ, dma_irq_handler);
  irq_set_enabled(DMA_IRQ, false);
  dma_channel_unclaim(current_dma_channel);
  TRACE_LOG("dma_deinit end\n");
}

static void pio_init(const i2s_config_t *config) {
  TRACE_LOG("pio_init begin\n");
  // --- PIO setup ---
  PIO pio = config->pio_instance;
  pio_sm = pio_claim_unused_sm(pio, true);

  if (config->bit_depth == 16) {
    loaded_pio_program = &i2s_stereo_16bit_program;
    pio_offset = pio_add_program(pio, loaded_pio_program);
    i2s_16bit_program_init(pio, pio_sm, pio_offset, config);
  } else if (config->bit_depth == 24) {
    loaded_pio_program = &i2s_stereo_24bit_program;
    pio_offset = pio_add_program(pio, loaded_pio_program);
    i2s_24bit_program_init(pio, pio_sm, pio_offset, config);
  } else if (config->bit_depth == 32) {
    loaded_pio_program = &i2s_stereo_32bit_program;
    pio_offset = pio_add_program(pio, loaded_pio_program);
    i2s_32bit_program_init(pio, pio_sm, pio_offset, config);
  } else {
    // Should not happen
    assert(false);
  }
  TRACE_LOG("pio_init end\n");
}

static void pio_start(const i2s_config_t *config) {
  TRACE_LOG("pio_start begin\n");
  pio_sm_set_enabled(config->pio_instance, pio_sm, true);
  TRACE_LOG("pio_start end\n");
}

static void pio_stop(const i2s_config_t *config) {
  TRACE_LOG("pio_stop begin\n");
  pio_sm_set_enabled(config->pio_instance, pio_sm, false);
  pio_sm_clear_fifos(config->pio_instance, pio_sm);
  pio_sm_exec(config->pio_instance, pio_sm, pio_encode_jmp(pio_offset));
  TRACE_LOG("pio_stop begin\n");
}

static void pio_deinit(const i2s_config_t *config) {
  TRACE_LOG("pio_deinit begin\n");
  if (loaded_pio_program) {
    pio_remove_program(config->pio_instance, loaded_pio_program, pio_offset);
    loaded_pio_program = NULL;
  }
  pio_sm_unclaim(config->pio_instance, pio_sm);
  TRACE_LOG("pio_deinit end\n");
}

void i2s_init(const i2s_config_t *config) {
  TRACE_LOG("i2s_init begin\n");
  assert(config != NULL);
  assert(config->buffer_frames > 0);

#ifdef I2S_MUTE_PIN
  gpio_init(I2S_MUTE_PIN);
  gpio_set_dir(I2S_MUTE_PIN, GPIO_OUT);
  i2s_mute();  // Start in muted state
#endif

  // PIO
  pio_init(config);

  // DMA
  dma_init(config);

  // Initially, one buffer is ready to be filled
  buffer_ready = true;
  initialized = true;
  TRACE_LOG("i2s_init end\n");
}

void i2s_deinit(const i2s_config_t *config) {
  if (!initialized) {
    return;
  }
  TRACE_LOG("i2s_deinit begin\n");

  // DMA
  dma_deinit();

  // PIO
  pio_deinit(config);

  initialized = false;
  TRACE_LOG("i2s_deinit end\n");
}

void i2s_start(const i2s_config_t *config) {
  TRACE_LOG("i2s_start begin\n");
  i2s_unmute();

  // DMA
  dma_start();

  // PIO
  pio_start(config);
  TRACE_LOG("i2s_start end\n");
}

void i2s_stop(const i2s_config_t *config) {
  TRACE_LOG("i2s_stop begin\n");
  i2s_mute();

  // DMA
  dma_stop();

  // PIO
  pio_stop(config);
  TRACE_LOG("i2s_stop end\n");
}

void i2s_mute() {
#ifdef I2S_MUTE_PIN
  gpio_put(I2S_MUTE_PIN, 0);  // Active Low Mute
#endif
}

void i2s_unmute() {
#ifdef I2S_MUTE_PIN
  gpio_put(I2S_MUTE_PIN, 1);
#endif
}

bool i2s_is_buffer_ready() {
  if (buffer_ready) {
    buffer_ready = false;
    return true;
  }
  return false;
}

int32_t *i2s_get_write_buffer() { return (int32_t *)write_buffer; }

uint32_t i2s_get_buffer_size_frames(const i2s_config_t *config) {
  return config->buffer_frames;
}

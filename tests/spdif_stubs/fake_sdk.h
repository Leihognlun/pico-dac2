#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef unsigned uint;
typedef struct { uint32_t fdebug, txf[4]; } fake_pio_t;
typedef fake_pio_t *PIO;
extern fake_pio_t fake_pio;
#define pio0 (&fake_pio)
typedef struct { unsigned dummy; } pio_sm_config;
typedef struct { unsigned channel; } dma_channel_config;
extern unsigned fake_sm_count, fake_dma_count, fake_enabled_mask;
extern bool fake_pending[2];
extern const void *fake_dma_read[2];
extern unsigned fake_function[30];
#define __isr
#define __time_critical_func(name) name
#define PIO_FDEBUG_TXSTALL_LSB 0
#define PIO_FIFO_JOIN_TX 0
#define DMA_SIZE_32 2
#define PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY 0
#define PICO_HIGHEST_IRQ_PRIORITY 0
#define GPIO_OUT 1
#define GPIO_FUNC_SIO 5
#define clk_sys 0
#define DMA_IRQ_NUM(n) (n)
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t n) { (void)n; }
static inline void __mem_fence_release(void) {}
static inline uint32_t clock_get_hz(unsigned c) { (void)c; return 120000000; }
static inline void gpio_init(unsigned p) { fake_function[p] = GPIO_FUNC_SIO; }
static inline void gpio_put(unsigned p, bool v) { (void)p; (void)v; }
static inline void gpio_set_dir(unsigned p, bool v) { (void)p; (void)v; }
static inline void gpio_set_function(unsigned p, unsigned f) { fake_function[p] = f; }
static inline void pio_gpio_init(PIO p, unsigned pin) {
  (void)p;
  fake_function[pin] = 6;
}
static inline unsigned pio_claim_unused_sm(PIO p, bool b) { (void)p; (void)b; return fake_sm_count++; }
static inline unsigned pio_add_program(PIO p, const void *v) { (void)p; (void)v; return 0; }
static inline void pio_remove_program(PIO p, const void *v, unsigned o) { (void)p; (void)v; (void)o; }
static inline void pio_sm_unclaim(PIO p, unsigned s) { (void)p; (void)s; --fake_sm_count; }
static inline void sm_config_set_sideset_pins(pio_sm_config *c, unsigned p) { (void)c; (void)p; }
static inline void sm_config_set_out_shift(pio_sm_config *c, bool a, bool b, unsigned n) { (void)c; (void)a; (void)b; (void)n; }
static inline void sm_config_set_fifo_join(pio_sm_config *c, unsigned n) { (void)c; (void)n; }
static inline void sm_config_set_clkdiv_int_frac(pio_sm_config *c, unsigned a, unsigned b) { (void)c; (void)a; (void)b; }
static inline void pio_sm_init(PIO p, unsigned s, unsigned o, const pio_sm_config *c) { (void)p; (void)s; (void)o; (void)c; }
static inline void pio_sm_set_consecutive_pindirs(PIO p, unsigned s, unsigned pin, unsigned n, bool d) { (void)p; (void)s; (void)pin; (void)n; (void)d; }
static inline void pio_sm_set_pins_with_mask(PIO p, unsigned s, unsigned v, unsigned m) { (void)p; (void)s; (void)v; (void)m; }
static inline unsigned pio_get_dreq(PIO p, unsigned s, bool b) { (void)p; (void)b; return s; }
static inline bool pio_sm_is_tx_fifo_full(PIO p, unsigned s) { (void)p; (void)s; return true; }
static inline void pio_enable_sm_mask_in_sync(PIO p, unsigned m) { (void)p; fake_enabled_mask = m; }
static inline void pio_set_sm_mask_enabled(PIO p, unsigned m, bool on) { (void)p; if (!on) fake_enabled_mask &= ~m; }
static inline void pio_sm_clear_fifos(PIO p, unsigned s) { (void)p; (void)s; }
static inline void pio_sm_restart(PIO p, unsigned s) { (void)p; (void)s; }
static inline void pio_sm_exec(PIO p, unsigned s, unsigned v) { (void)p; (void)s; (void)v; }
static inline unsigned pio_encode_jmp(unsigned o) { return o; }
static inline unsigned dma_claim_unused_channel(bool b) { (void)b; return fake_dma_count++; }
static inline dma_channel_config dma_channel_get_default_config(unsigned n) { return (dma_channel_config){n}; }
static inline void channel_config_set_transfer_data_size(dma_channel_config *c, unsigned n) { (void)c; (void)n; }
static inline void channel_config_set_read_increment(dma_channel_config *c, bool n) { (void)c; (void)n; }
static inline void channel_config_set_write_increment(dma_channel_config *c, bool n) { (void)c; (void)n; }
static inline void channel_config_set_dreq(dma_channel_config *c, unsigned n) { (void)c; (void)n; }
static inline void channel_config_set_high_priority(dma_channel_config *c, bool n) { (void)c; (void)n; }
static inline unsigned dma_encode_transfer_count(unsigned n) { return n; }
static inline void dma_channel_configure(unsigned n, const dma_channel_config *c, void *w, const void *r, unsigned count, bool go) {
  (void)c; (void)w; (void)count; (void)go; fake_dma_read[n] = r;
}
static inline bool dma_irqn_get_channel_status(unsigned irq, unsigned n) { (void)irq; return fake_pending[n]; }
static inline void dma_irqn_acknowledge_channel(unsigned irq, unsigned n) { (void)irq; fake_pending[n] = false; }
static inline void dma_irqn_set_channel_enabled(unsigned irq, unsigned n, bool on) { (void)irq; (void)n; (void)on; }
static inline void dma_channel_set_read_addr(unsigned n, const void *r, bool go) { (void)go; fake_dma_read[n] = r; }
static inline void dma_channel_abort(unsigned n) { (void)n; }
static inline void dma_channel_unclaim(unsigned n) { (void)n; --fake_dma_count; }
static inline void irq_add_shared_handler(unsigned n, void (*f)(void), unsigned priority) { (void)n; (void)f; (void)priority; }
static inline void irq_remove_handler(unsigned n, void (*f)(void)) { (void)n; (void)f; }
static inline void irq_set_priority(unsigned n, unsigned priority) { (void)n; (void)priority; }
static inline void irq_set_enabled(unsigned n, bool b) { (void)n; (void)b; }
static inline void tight_loop_contents(void) {}

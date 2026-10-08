#pragma once
#include <stdint.h>

enum sd_diag_phase { SD_DIAG_INIT, SD_DIAG_MOUNT, SD_DIAG_OPEN,
                     SD_DIAG_READ, SD_DIAG_PACK, SD_DIAG_WAIT, SD_DIAG_DONE };
#if PICODAC_SD_DIAGNOSTICS
void sd_diag_init(void);
void sd_diag_phase(enum sd_diag_phase phase);
void sd_diag_read_begin(void);
void sd_diag_read_end(unsigned bytes, int result);
void sd_diag_submit_begin(void);
void sd_diag_submit_end(void);
void sd_diag_task(unsigned produced, unsigned consumed, int status,
                  unsigned underruns, unsigned played);
#else
static inline void sd_diag_init(void) {}
static inline void sd_diag_phase(enum sd_diag_phase p) { (void)p; }
static inline void sd_diag_read_begin(void) {}
static inline void sd_diag_read_end(unsigned b, int r) { (void)b; (void)r; }
static inline void sd_diag_submit_begin(void) {}
static inline void sd_diag_submit_end(void) {}
static inline void sd_diag_task(unsigned p, unsigned c, int s, unsigned u, unsigned b) {
  (void)p; (void)c; (void)s; (void)u; (void)b;
}
#endif

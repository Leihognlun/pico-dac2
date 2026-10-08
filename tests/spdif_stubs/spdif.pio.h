#include "fake_sdk.h"
static const unsigned picodac_spdif_program = 0;
static inline pio_sm_config picodac_spdif_program_get_default_config(unsigned o) {
  (void)o; return (pio_sm_config){0};
}

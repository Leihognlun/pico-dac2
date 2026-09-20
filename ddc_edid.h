#pragma once
#include <stdint.h>
#if PICODAC_DDC
void ddc_edid_init(void);
#else
static inline void ddc_edid_init(void) {}
#endif
extern const uint8_t ddc_edid_data[256];

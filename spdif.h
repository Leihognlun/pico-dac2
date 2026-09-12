#pragma once

#include <stdbool.h>
#include <stdint.h>

void spdif_init(unsigned pin, uint32_t sample_rate, uint8_t bit_depth);
void spdif_deinit(void);
void spdif_start(void);
void spdif_stop(void);
bool spdif_buffer_ready(void);
int32_t *spdif_write_buffer(void);
void spdif_submit_buffer(void);

#pragma once

#include <stdbool.h>
#include <stdint.h>

void spdif_init(unsigned pin, uint32_t sample_rate, uint8_t bit_depth,
                bool non_pcm);
void spdif_deinit(void);
void spdif_start(void);
void spdif_stop(void);
// Gate the physical carrier without losing the application's start request.
void spdif_set_link_enabled(bool enabled);
bool spdif_buffer_ready(void);
int32_t *spdif_write_buffer(void);
void spdif_submit_buffer(void);

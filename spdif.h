#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "board.h"

void spdif_init(unsigned pin, uint32_t sample_rate, uint8_t bit_depth,
                bool non_pcm);
void spdif_deinit(void);
void spdif_start(void);
void spdif_stop(void);
// Gate the physical carrier without losing the application's start request.
void spdif_set_link_enabled(bool enabled);
// C2 ports are zero based. Both serializers share one clock and audio timeline.
#if BOARD_ARC_COUNT > 1
void spdif_set_port_link_enabled(unsigned port, bool enabled);
// Enabling one C2 port atomically disables the other port, including its GPIO.
void spdif_set_port_playing(unsigned port, bool playing);
// Mark complete IEC burst boundaries before submitting SD blocks.
void spdif_set_burst_start(bool start);
#endif
bool spdif_buffer_ready(void);
int32_t *spdif_write_buffer(void);
void spdif_submit_buffer(void);

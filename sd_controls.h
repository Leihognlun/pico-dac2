#pragma once
#include <stdbool.h>
void sd_controls_init(void);
void sd_controls_task(void);
bool sd_controls_playing(void);
void sd_controls_stop(void);
unsigned sd_controls_next_generation(void);
unsigned sd_controls_play_generation(void);

#pragma once
#include <stdbool.h>
void sd_controls_init(void);
void sd_controls_task(void);
bool sd_controls_playing(void);
unsigned sd_controls_next_generation(void);

#pragma once
#include <stdbool.h>
#include "board.h"
void sd_controls_init(void);
void sd_controls_task(void);
bool sd_controls_playing(void);
bool sd_controls_port_playing(unsigned port);
#if BOARD_ARC_COUNT > 1
bool sd_controls_audio_allowed(void);
bool sd_controls_next_pressed(void);
#endif
void sd_controls_stop(void);
unsigned sd_controls_next_generation(void);
unsigned sd_controls_play_generation(void);
// Changes only when standby becomes playback.  The accompanying flag tells the
// player whether that start happened within 10 seconds of standby entry.
unsigned sd_controls_start_generation(void);
bool sd_controls_start_from_next(void);

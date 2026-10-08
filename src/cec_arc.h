#pragma once
#include <stdbool.h>
#include "board.h"
#if PICODAC_CEC
void cec_arc_init(void);
void cec_arc_task(void);
bool cec_arc_audio_allowed(void);
void cec_arc_set_enabled(bool enabled);
void cec_arc_request_playback(void);
void cec_arc_volume_key(bool up, bool pressed);
void cec_arc_set_port_playing(unsigned port, bool playing);
#if BOARD_ARC_COUNT > 1
void cec_arc_request_port_playback(unsigned port);
void cec_arc_port_volume_key(unsigned port, bool up, bool pressed);
bool cec_arc_port_audio_allowed(unsigned port);
#endif
#else
static inline void cec_arc_init(void) {}
static inline void cec_arc_task(void) {}
static inline bool cec_arc_audio_allowed(void) { return true; }
static inline void cec_arc_set_enabled(bool enabled) {(void)enabled;}
static inline void cec_arc_request_playback(void) {}
static inline void cec_arc_volume_key(bool up, bool pressed) {(void)up;(void)pressed;}
static inline void cec_arc_set_port_playing(unsigned port, bool playing) {(void)port;(void)playing;}
static inline void cec_arc_request_port_playback(unsigned port) {(void)port;}
static inline void cec_arc_port_volume_key(unsigned port, bool up, bool pressed) {(void)port;(void)up;(void)pressed;}
static inline bool cec_arc_port_audio_allowed(unsigned port) { return port < BOARD_ARC_COUNT; }
#endif

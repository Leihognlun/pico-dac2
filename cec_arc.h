#pragma once
#include <stdbool.h>
#if PICODAC_CEC
void cec_arc_init(void);
void cec_arc_task(void);
bool cec_arc_audio_allowed(void);
void cec_arc_set_enabled(bool enabled);
void cec_arc_volume_key(bool up, bool pressed);
#else
static inline void cec_arc_init(void) {}
static inline void cec_arc_task(void) {}
static inline bool cec_arc_audio_allowed(void) { return true; }
static inline void cec_arc_set_enabled(bool enabled) {(void)enabled;}
static inline void cec_arc_volume_key(bool up, bool pressed) {(void)up;(void)pressed;}
#endif

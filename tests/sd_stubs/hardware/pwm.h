#pragma once
#include <stdbool.h>
static inline unsigned pwm_gpio_to_slice_num(unsigned pin) {return (pin >> 1) & 7;}
static inline void pwm_set_wrap(unsigned slice, unsigned wrap) {(void)slice; (void)wrap;}
static inline void pwm_set_clkdiv(unsigned slice, float div) {(void)slice; (void)div;}
static inline void pwm_set_gpio_level(unsigned pin, unsigned level) {(void)pin; (void)level;}
static inline void pwm_set_enabled(unsigned slice, bool on) {(void)slice; (void)on;}

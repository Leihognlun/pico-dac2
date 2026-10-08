#pragma once
#include "pico/stdlib.h"
#define GPIO_FUNC_SIO 5
#define GPIO_FUNC_PWM 4
static inline void gpio_set_function(unsigned pin, unsigned fn) {(void)pin; (void)fn;}

#pragma once
#include <stdbool.h>
#define GPIO_IN 0
#define GPIO_OUT 1
#define GPIO_FUNC_SIO 5
#define GPIO_FUNC_PWM 4
static inline void gpio_set_function(unsigned pin, unsigned fn) {(void)pin; (void)fn;}
void gpio_init(unsigned pin);
void gpio_set_dir(unsigned pin, bool out);
void gpio_pull_up(unsigned pin);
bool gpio_get(unsigned pin);
void gpio_put(unsigned pin, bool value);

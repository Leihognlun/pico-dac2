#pragma once
#include <stdint.h>
uint32_t time_us_32(void);
void sleep_ms(unsigned ms);
void sleep_us(unsigned us);
void tight_loop_contents(void);
#define GPIO_IN 0
#define GPIO_OUT 1
void gpio_init(unsigned pin);
void gpio_set_dir(unsigned pin, int out);
void gpio_pull_up(unsigned pin);
void gpio_put(unsigned pin, int value);
int gpio_get(unsigned pin);

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { int unused; } repeating_timer_t;
#define GPIO_IN 0
#define GPIO_OUT 1
uint32_t time_us_32(void);
void gpio_init(unsigned);
void gpio_put(unsigned, bool);
void gpio_set_dir(unsigned, bool);
void gpio_disable_pulls(unsigned);
bool gpio_get(unsigned);
bool add_repeating_timer_us(int64_t, bool (*)(repeating_timer_t *), void *, repeating_timer_t *);
void panic(const char *);

#pragma once
#define GPIO_FUNC_I2C 3
void gpio_init(unsigned pin);
void gpio_set_function(unsigned pin, unsigned fn);
void gpio_disable_pulls(unsigned pin);

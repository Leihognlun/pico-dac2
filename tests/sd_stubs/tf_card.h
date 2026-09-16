#pragma once
#include <stdbool.h>
#define spi0 ((void *)1)
typedef struct {
  void *spi_inst;
  unsigned clk_slow, clk_fast, pin_miso, pin_cs, pin_sck, pin_mosi;
  bool pullup;
} pico_fatfs_spi_config_t;
bool pico_fatfs_set_config(pico_fatfs_spi_config_t *config);

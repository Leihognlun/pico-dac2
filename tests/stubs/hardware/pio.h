#pragma once

// Host test declarations only. The firmware build uses the real Pico SDK.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef unsigned uint;
typedef void *PIO;
#define pio0 ((PIO)(uintptr_t)1)

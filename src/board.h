#pragma once

// Application wiring, independent of the Pico SDK's PICO_BOARD selection.
#ifndef PICODAC_BOARD_C2
#define PICODAC_BOARD_C2 0
#endif
#if PICODAC_BOARD_C2
#include "boards/c2.h"
#else
#include "boards/c1.h"
#endif

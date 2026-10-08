#pragma once

#include <stdbool.h>
#include <stdint.h>

bool usb_audio_stream_can_set_interface(uint8_t alt);

void usb_audio_init();

#pragma once

#include <stdint.h>

#include "hardware/pio.h"

// --- Configuration Struct ---
typedef struct {
  uint8_t data_pin;        // I2S DATA pin
  uint8_t clock_pin_base;  // BCLK pin. LRCLK will be clock_pin_base + 1
  uint8_t bit_depth;       // 16 or 24
  PIO pio_instance;        // PIO instance to use (pio0 or pio1)
  uint32_t buffer_frames;  // Number of frames (stereo samples) per DMA buffer
  uint32_t sample_rate;    // sample_rate (44.1kHz ~ 96kHz)
} i2s_config_t;

/**
 * @brief Initializes the I2S output PIO and DMA systems.
 *
 * @param config Configuration parameters for the I2S interface.
 */
void i2s_init(const i2s_config_t* config);

/**
 * @brief Deinitializes the I2S PIO and DMA, releasing hardware resources.
 */
void i2s_deinit(const i2s_config_t* config);

/**
 * @brief Starts the I2S audio output.
 */
void i2s_start(const i2s_config_t* config);

/**
 * @brief Stops the I2S audio output.
 */
void i2s_stop(const i2s_config_t* config);

/**
 * @brief Mutes the audio output using the hardware mute pin, if configured.
 */
void i2s_mute();

/**
 * @brief Unmutes the audio output using the hardware mute pin, if configured.
 */
void i2s_unmute();

/**
 * @brief Checks if a new buffer is ready to be filled by the application.
 *
 * The main application loop should poll this function. If it returns true,
 * a buffer has been fully transferred via DMA and is now free. The application
 * can then call i2s_get_write_buffer() to get its address and fill it.
 *
 * @return True if a new buffer is available for writing, false otherwise.
 */
bool i2s_is_buffer_ready();

/**
 * @brief Returns a pointer to the next available buffer for writing audio data.
 *
 * The application should fill this buffer with new audio samples.
 * The data must be formatted as 32-bit words.
 *
 * Example: buffer[i] = left_sample; buffer[i+1] = right_sample;
 *
 * @return A pointer to the writable buffer (as int32_t*).
 */
int32_t* i2s_get_write_buffer();

/**
 * @brief Returns the size of the audio buffers in stereo samples.
 *
 * This is the value that was configured in i2s_config_t during i2s_init.
 *
 * @return The number of stereo samples that can be written to the buffer.
 */
uint32_t i2s_get_buffer_size_frames(const i2s_config_t* config);

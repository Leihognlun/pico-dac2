#include "ddc_edid.h"
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/i2c_slave.h"

static uint8_t offset;
static bool offset_written;
volatile uint32_t ddc_read_bytes, ddc_offset_writes, ddc_ignored_writes;

static void handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
  switch (event) {
    case I2C_SLAVE_RECEIVE:
      while (i2c_get_read_available(i2c)) {
        uint8_t byte = i2c_read_byte_raw(i2c);
        if (!offset_written) {
          offset = byte; offset_written = true; ++ddc_offset_writes;
        } else ++ddc_ignored_writes; // Read-only EEPROM: discard attempted writes.
      }
      break;
    case I2C_SLAVE_REQUEST:
      // Supply exactly one byte per request; never prefetch beyond a short read.
      i2c_write_byte_raw(i2c, ddc_edid_data[offset++]);
      ++ddc_read_bytes;
      break;
    case I2C_SLAVE_FINISH:
      offset_written = false; // STOP/repeated START retain current read address.
      break;
  }
}
void ddc_edid_init(void) {
  offset = 0; offset_written = false;
  i2c_init(i2c1, 100000);
  for (unsigned pin = 6; pin <= 7; ++pin) {
    gpio_init(pin);
    gpio_set_function(pin, GPIO_FUNC_I2C);
    gpio_disable_pulls(pin); // External DDC pull-ups and bidirectional level shifter.
  }
  i2c_slave_init(i2c1, 0x50, handler);
}

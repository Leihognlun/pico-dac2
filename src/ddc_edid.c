#include "ddc_edid.h"
#include "board.h"
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/i2c_slave.h"

typedef struct { uint8_t offset; bool offset_written; } ddc_state_t;
static ddc_state_t states[BOARD_ARC_COUNT];
volatile uint32_t ddc_read_bytes, ddc_offset_writes, ddc_ignored_writes;

static void handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
  unsigned port = 0;
#if BOARD_ARC_COUNT > 1
  if (i2c == i2c0) port = 1;
#endif
  ddc_state_t *state = &states[port];
  switch (event) {
    case I2C_SLAVE_RECEIVE:
      while (i2c_get_read_available(i2c)) {
        uint8_t byte = i2c_read_byte_raw(i2c);
        if (!state->offset_written) {
          state->offset = byte; state->offset_written = true; ++ddc_offset_writes;
        } else ++ddc_ignored_writes; // Read-only EEPROM: discard attempted writes.
      }
      break;
    case I2C_SLAVE_REQUEST:
      // Supply exactly one byte per request; never prefetch beyond a short read.
      i2c_write_byte_raw(i2c, ddc_edid_data[state->offset++]);
      ++ddc_read_bytes;
      break;
    case I2C_SLAVE_FINISH:
      state->offset_written = false; // STOP/repeated START retain current read address.
      break;
  }
}
void ddc_edid_init(void) {
  i2c_inst_t *buses[] = {i2c1,
#if BOARD_ARC_COUNT > 1
    i2c0
#endif
  };
  const unsigned pins[][2] = {{BOARD_ARC1_SDA, BOARD_ARC1_SCL},
#if BOARD_ARC_COUNT > 1
    {BOARD_ARC2_SDA, BOARD_ARC2_SCL}
#endif
  };
  for (unsigned i = 0; i < BOARD_ARC_COUNT; ++i) {
    states[i] = (ddc_state_t){0};
    i2c_init(buses[i], 100000);
    for (unsigned j = 0; j < 2; ++j) {
      gpio_init(pins[i][j]);
      gpio_set_function(pins[i][j], GPIO_FUNC_I2C);
      gpio_disable_pulls(pins[i][j]);
    }
    i2c_slave_init(buses[i], 0x50, handler);
  }
}

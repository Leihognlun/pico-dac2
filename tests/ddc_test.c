#include "ddc_edid.h"
#include "pico/i2c_slave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static i2c_inst_t instance;
i2c_inst_t *i2c1 = &instance;
static i2c_slave_handler_t callback;
static uint8_t rx[4], tx;
static unsigned pending, pos, pins;
void gpio_init(unsigned pin) { assert(pin == 6 || pin == 7); pins |= 1u << pin; }
void gpio_set_function(unsigned pin, unsigned fn) { assert((pin == 6 || pin == 7) && fn == 3); }
void gpio_disable_pulls(unsigned pin) { assert(pin == 6 || pin == 7); }
void i2c_init(i2c_inst_t *i, unsigned baud) { assert(i == i2c1 && baud == 100000); }
void i2c_slave_init(i2c_inst_t *i, uint8_t addr, i2c_slave_handler_t fn) {
  assert(i == i2c1 && addr == 0x50); callback = fn;
}
unsigned i2c_get_read_available(i2c_inst_t *i) { assert(i == i2c1); return pending; }
uint8_t i2c_read_byte_raw(i2c_inst_t *i) {
  assert(i == i2c1 && pending); --pending; return rx[pos++];
}
void i2c_write_byte_raw(i2c_inst_t *i, uint8_t v) { assert(i == i2c1); tx = v; }
static void address(uint8_t off) {
  rx[0] = off; pending = 1; pos = 0; callback(i2c1, I2C_SLAVE_RECEIVE);
  callback(i2c1, I2C_SLAVE_FINISH); // repeated START or STOP + START
}
static uint8_t read_byte(void) { callback(i2c1, I2C_SLAVE_REQUEST); return tx; }
int main(void) {
  ddc_edid_init(); assert(pins == ((1u << 6) | (1u << 7)));
  const uint8_t header[] = {0,255,255,255,255,255,255,0};
  assert(memcmp(ddc_edid_data, header, 8) == 0 && ddc_edid_data[126] == 1);
  for (unsigned block = 0; block < 2; ++block) {
    unsigned sum = 0;
    address(block * 128);
    for (unsigned i = 0; i < 128; ++i) {
      uint8_t b = read_byte(); assert(b == ddc_edid_data[block * 128 + i]); sum += b;
    }
    assert((sum & 255) == 0); callback(i2c1, I2C_SLAVE_FINISH);
  }
  address(255); assert(read_byte() == ddc_edid_data[255]);
  assert(read_byte() == ddc_edid_data[0]);
  callback(i2c1, I2C_SLAVE_FINISH);
  assert(read_byte() == ddc_edid_data[1]); // current address persists across STOP
  callback(i2c1, I2C_SLAVE_FINISH);
  rx[0] = 128; rx[1] = 0xaa; rx[2] = 0xbb; pending = 3; pos = 0;
  callback(i2c1, I2C_SLAVE_RECEIVE); callback(i2c1, I2C_SLAVE_FINISH);
  assert(read_byte() == 2); // attempted write did not change extension block
  puts("PASS: DDC init, offsets, repeated START, sequential/current reads, wrap, read-only and EDID checksums");
}

#include "ddc_edid.h"
#include "pico/i2c_slave.h"
#include <assert.h>
#include <stdio.h>
static i2c_inst_t instances[2];
i2c_inst_t *i2c0 = &instances[0], *i2c1 = &instances[1];
static i2c_slave_handler_t callbacks[2];
static uint8_t rx[2], tx[2];
static unsigned pending[2], pin_mask;
static unsigned index_of(i2c_inst_t *i) { return i == i2c1 ? 1 : 0; }
void gpio_init(unsigned pin) {
  assert(pin == 20 || pin == 21 || pin == 26 || pin == 27); pin_mask |= 1u << pin;
}
void gpio_set_function(unsigned pin, unsigned fn) { (void)pin; assert(fn == 3); }
void gpio_disable_pulls(unsigned pin) { (void)pin; }
void i2c_init(i2c_inst_t *i, unsigned baud) { (void)i; assert(baud == 100000); }
void i2c_slave_init(i2c_inst_t *i, uint8_t addr, i2c_slave_handler_t fn) {
  assert(addr == 0x50); callbacks[index_of(i)] = fn;
}
unsigned i2c_get_read_available(i2c_inst_t *i) { return pending[index_of(i)]; }
uint8_t i2c_read_byte_raw(i2c_inst_t *i) {
  unsigned n = index_of(i); assert(pending[n]); --pending[n]; return rx[n];
}
void i2c_write_byte_raw(i2c_inst_t *i, uint8_t v) { tx[index_of(i)] = v; }
static void event(unsigned i, i2c_slave_event_t e) { callbacks[i](&instances[i], e); }
static void address(unsigned i, uint8_t off) {
  rx[i] = off; pending[i] = 1; event(i, I2C_SLAVE_RECEIVE);
}
static uint8_t read_byte(unsigned i) { event(i, I2C_SLAVE_REQUEST); return tx[i]; }
int main(void) {
  ddc_edid_init();
  assert(pin_mask == ((1u << 20) | (1u << 21) | (1u << 26) | (1u << 27)));
  address(0, 128); address(1, 255);
  event(0, I2C_SLAVE_FINISH); event(1, I2C_SLAVE_FINISH);
  assert(read_byte(1) == ddc_edid_data[255]);
  assert(read_byte(0) == ddc_edid_data[128]);
  assert(read_byte(1) == ddc_edid_data[0]);
  assert(read_byte(0) == ddc_edid_data[129]);
  address(0, 42); address(0, 99); // A second data byte is ignored.
  event(0, I2C_SLAVE_FINISH);
  assert(read_byte(0) == ddc_edid_data[42]);
  assert(read_byte(1) == ddc_edid_data[1]);
  puts("PASS: two DDC buses, independent interleaved offsets, wrap and read-only writes");
}

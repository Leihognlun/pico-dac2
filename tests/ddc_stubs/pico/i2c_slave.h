#pragma once
#include <stdint.h>
typedef struct { int unused; } i2c_inst_t;
extern i2c_inst_t *i2c1;
typedef enum { I2C_SLAVE_RECEIVE, I2C_SLAVE_REQUEST, I2C_SLAVE_FINISH } i2c_slave_event_t;
typedef void (*i2c_slave_handler_t)(i2c_inst_t *, i2c_slave_event_t);
void i2c_init(i2c_inst_t *, unsigned);
void i2c_slave_init(i2c_inst_t *, uint8_t, i2c_slave_handler_t);
unsigned i2c_get_read_available(i2c_inst_t *);
uint8_t i2c_read_byte_raw(i2c_inst_t *);
void i2c_write_byte_raw(i2c_inst_t *, uint8_t);

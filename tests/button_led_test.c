#include <assert.h>
#include <stdio.h>
#include "button_led.h"
int main(void) {
  for (unsigned p = 0; p <= 100; ++p)
    assert(button_led_level(0, 2000, p, 1234567) == p * 100);
  for (unsigned ms = 500; ms <= 5000; ++ms) {
    uint32_t period = ms * 1000;
    assert(button_led_level(1, ms, 0, 0) == 0);
    assert(button_led_level(1, ms, 0, period / 4) == 2176);
    assert(button_led_level(1, ms, 0, period / 2) == 10000);
    assert(button_led_level(1, ms, 0, period * 3 / 4) == 2176);
    assert(button_led_level(1, ms, 0, period) == 0);
    unsigned previous = 0;
    for (unsigned i = 0; i <= 256; ++i) {
      uint32_t t = (uint64_t)(period / 2) * i / 256;
      unsigned level = button_led_level(1, ms, 0, t);
      assert(level >= previous && level <= 10000);
      assert(level == button_led_level(1, ms, 100, period - t));
      assert(level == button_led_level(1, ms, 0, period + t));
      previous = level;
    }
    assert(button_led_level(1, ms, 0, period / 100) < 10);
    assert(button_led_level(1, ms, 0, period / 2 - period / 100) > 9970);
  }
  puts("PASS: fixed brightness 0..100 and breathing period 500..5000 ms");
}

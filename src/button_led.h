#pragma once
#include "board.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <stdint.h>

_Static_assert(BOARD_B1_IDLE_LED_MODE >= 0 && BOARD_B1_IDLE_LED_MODE <= 1,
               "Idle LED mode must be 0 (fixed) or 1 (breathing)");
_Static_assert(BOARD_B1_IDLE_LED_PERIOD_MS >= 500 && BOARD_B1_IDLE_LED_PERIOD_MS <= 5000,
               "Idle LED period must be 500..5000 ms");
_Static_assert(BOARD_B1_IDLE_LED_BRIGHTNESS >= 0 && BOARD_B1_IDLE_LED_BRIGHTNESS <= 100,
               "Idle LED brightness must be 0..100 percent");
#if BOARD_ARC_COUNT > 1
_Static_assert(BOARD_B2_IDLE_LED_MODE >= 0 && BOARD_B2_IDLE_LED_MODE <= 1,
               "B2 idle LED mode must be 0 or 1");
_Static_assert(BOARD_B2_IDLE_LED_PERIOD_MS >= 500 && BOARD_B2_IDLE_LED_PERIOD_MS <= 5000,
               "B2 idle LED period must be 500..5000 ms");
_Static_assert(BOARD_B2_IDLE_LED_BRIGHTNESS >= 0 && BOARD_B2_IDLE_LED_BRIGHTNESS <= 100,
               "B2 idle LED brightness must be 0..100 percent");
#endif

// 10000 counts gives exact percentage endpoints; hardware PWM avoids
// brightness depending on USB/SD main-loop timing.
static inline unsigned button_led_level(unsigned mode, unsigned period_ms,
                                         unsigned percent, uint32_t elapsed_us) {
  if (!mode) return percent * 100u;
  // Half-cycle LUT: round(10000 * ((1 - cos(pi*x))/2)^2.2), x=i/128.
  // Cosine easing softens both ends; gamma 2.2 compensates perception.
  // Integer interpolation keeps animation smooth without runtime libm.
  static const uint16_t curve[129] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
    2, 3, 4, 6, 8, 10, 13, 16, 20, 24, 30, 36,
    43, 52, 61, 71, 83, 97, 111, 128, 146, 166, 188, 212,
    238, 266, 297, 330, 366, 404, 445, 489, 535, 585, 638, 694,
    753, 816, 882, 951, 1024, 1100, 1180, 1263, 1350, 1441, 1535, 1633,
    1734, 1840, 1948, 2061, 2176, 2296, 2418, 2544, 2673, 2806, 2941, 3080,
    3221, 3365, 3512, 3661, 3813, 3967, 4122, 4280, 4439, 4600, 4763, 4926,
    5090, 5255, 5421, 5587, 5753, 5919, 6085, 6250, 6414, 6577, 6739, 6900,
    7058, 7215, 7370, 7522, 7672, 7819, 7962, 8103, 8239, 8372, 8501, 8626,
    8747, 8863, 8974, 9081, 9182, 9278, 9368, 9453, 9532, 9606, 9673, 9735,
    9790, 9839, 9881, 9917, 9947, 9970, 9987, 9997, 10000,
  };
  uint32_t period = period_ms * 1000u;
  uint32_t phase = elapsed_us % period;
  uint32_t ramp = phase <= period / 2 ? phase : period - phase;
  uint32_t half = period / 2;
  uint32_t scaled = ramp * 128u;
  unsigned index = scaled / half;
  if (index >= 128) return 10000;
  uint32_t fraction = scaled % half;
  return curve[index] + (unsigned)(((uint64_t)(curve[index + 1] - curve[index]) *
                                   fraction + half / 2) / half);
}
static inline unsigned button_led_idle_level(unsigned button, uint32_t elapsed) {
#if BOARD_ARC_COUNT > 1
  if (button == 1)
    return button_led_level(BOARD_B2_IDLE_LED_MODE, BOARD_B2_IDLE_LED_PERIOD_MS,
                            BOARD_B2_IDLE_LED_BRIGHTNESS, elapsed);
#else
  (void)button;
#endif
  return button_led_level(BOARD_B1_IDLE_LED_MODE, BOARD_B1_IDLE_LED_PERIOD_MS,
                          BOARD_B1_IDLE_LED_BRIGHTNESS, elapsed);
}
static inline void button_led_write(unsigned pin, unsigned level) {
  if (level == 0 || level == 10000) {
    gpio_put(pin, level != 0);
    gpio_set_function(pin, GPIO_FUNC_SIO);
  } else {
    unsigned slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, 9999);
    pwm_set_clkdiv(slice, 1.0f);
    pwm_set_gpio_level(pin, level);
    pwm_set_enabled(slice, true);
    gpio_set_function(pin, GPIO_FUNC_PWM);
  }
}

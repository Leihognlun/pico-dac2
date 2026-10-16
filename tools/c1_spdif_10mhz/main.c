#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "boards/c1.h"

enum {
    TEST_SYS_CLOCK_KHZ = 120000,
    TEST_PWM_DIVIDER = 1,
    TEST_PWM_COUNTS = 12,
    TEST_FREQUENCY_HZ = 10000000,
};

_Static_assert(BOARD_ARC1_TX == 16, "This test targets the C1 SPDIF GPIO16");
_Static_assert(TEST_SYS_CLOCK_KHZ * 1000u ==
                   TEST_PWM_DIVIDER * TEST_PWM_COUNTS * TEST_FREQUENCY_HZ,
               "PWM must produce exactly 10 MHz at the nominal system clock");
_Static_assert(TEST_PWM_COUNTS % 2 == 0, "50% duty needs an even period");

int main(void) {
    // Drive the C1 SPDIF pad directly: logic 0 / 1, no audio encoding.
    gpio_init(BOARD_ARC1_TX);
    gpio_put(BOARD_ARC1_TX, 0);
    gpio_set_dir(BOARD_ARC1_TX, GPIO_OUT);
    gpio_disable_pulls(BOARD_ARC1_TX);
    gpio_set_drive_strength(BOARD_ARC1_TX, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(BOARD_ARC1_TX, GPIO_SLEW_RATE_FAST);

    set_sys_clock_khz(TEST_SYS_CLOCK_KHZ, true);

    // 120 MHz / 1 / 12 = 10 MHz, with 6 counts (50 ns) per level.
    const uint slice = pwm_gpio_to_slice_num(BOARD_ARC1_TX);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac4(&config, TEST_PWM_DIVIDER, 0);
    pwm_config_set_wrap(&config, TEST_PWM_COUNTS - 1);
    pwm_init(slice, &config, false);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(BOARD_ARC1_TX),
                       TEST_PWM_COUNTS / 2);
    gpio_set_function(BOARD_ARC1_TX, GPIO_FUNC_PWM);
    pwm_set_enabled(slice, true);

    while (true) {
        __wfi();
    }
}

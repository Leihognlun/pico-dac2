cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Pico SDK board")
include(${CMAKE_CURRENT_LIST_DIR}/../../pico_sdk_import.cmake)

project(c1_spdif_10mhz_12ma C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(c1_spdif_10mhz_12ma main.c)
target_include_directories(c1_spdif_10mhz_12ma PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_options(c1_spdif_10mhz_12ma PRIVATE -Wall -Wextra -Werror)
target_link_libraries(c1_spdif_10mhz_12ma pico_stdlib hardware_pwm hardware_clocks)
pico_enable_stdio_uart(c1_spdif_10mhz_12ma 0)
pico_enable_stdio_usb(c1_spdif_10mhz_12ma 0)
pico_set_program_name(c1_spdif_10mhz_12ma "C1 GPIO16 10MHz 12mA square wave")
pico_set_program_version(c1_spdif_10mhz_12ma "1.0")
pico_add_extra_outputs(c1_spdif_10mhz_12ma)

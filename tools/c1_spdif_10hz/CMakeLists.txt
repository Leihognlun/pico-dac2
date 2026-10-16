cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Pico SDK board")
include(${CMAKE_CURRENT_LIST_DIR}/../../pico_sdk_import.cmake)

project(c1_spdif_10hz C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(c1_spdif_10hz main.c)
target_include_directories(c1_spdif_10hz PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_options(c1_spdif_10hz PRIVATE -Wall -Wextra -Werror)
target_link_libraries(c1_spdif_10hz pico_stdlib hardware_pwm hardware_clocks)
pico_enable_stdio_uart(c1_spdif_10hz 0)
pico_enable_stdio_usb(c1_spdif_10hz 0)
pico_set_program_name(c1_spdif_10hz "C1 GPIO16 10Hz square wave")
pico_set_program_version(c1_spdif_10hz "1.0")
pico_add_extra_outputs(c1_spdif_10hz)

# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(APP_NAME "mdac_adc2")
set(BOARD_NAME "rp2040" CACHE STRING "Target board name")

# user configurations
set(PICODAC_OUTPUT "BOTH" CACHE STRING "Audio output: BOTH, SPDIF or I2S")
set_property(CACHE PICODAC_OUTPUT PROPERTY STRINGS BOTH SPDIF I2S)
option(PICODAC_EAC3_PASSTHROUGH "192 kHz S16 IEC61937 carrier for E-AC-3" ON)
if(PICODAC_EAC3_PASSTHROUGH AND PICODAC_OUTPUT STREQUAL "I2S")
    message(FATAL_ERROR "E-AC-3 passthrough requires SPDIF or BOTH output")
endif()
set(PICODAC_SPDIF_PIN 22 CACHE STRING "SPDIF output GPIO")
if(NOT PICODAC_OUTPUT MATCHES "^(BOTH|SPDIF|I2S)$")
    message(FATAL_ERROR "PICODAC_OUTPUT must be BOTH, SPDIF or I2S")
endif()
if(NOT PICODAC_SPDIF_PIN MATCHES "^[0-9]+$" OR PICODAC_SPDIF_PIN GREATER 29)
    message(FATAL_ERROR "PICODAC_SPDIF_PIN must be an RP2040 GPIO number (0..29)")
endif()
set (PICODAC_I2S_DATA_PIN 18 CACHE STRING "I2S Data Pin")
set (PICODAC_I2S_BASE_CLOCK_PIN 16 CACHE STRING "I2S Base Clock Pin. LRCLK is BASE + 1")
if(PICODAC_OUTPUT STREQUAL "BOTH")
    math(EXPR PICODAC_I2S_LRCLK_PIN "${PICODAC_I2S_BASE_CLOCK_PIN} + 1")
    if(PICODAC_I2S_DATA_PIN LESS 0 OR PICODAC_I2S_DATA_PIN GREATER 29 OR
       PICODAC_I2S_BASE_CLOCK_PIN LESS 0 OR PICODAC_I2S_BASE_CLOCK_PIN GREATER 28 OR
       PICODAC_I2S_DATA_PIN EQUAL PICODAC_I2S_BASE_CLOCK_PIN OR
       PICODAC_I2S_DATA_PIN EQUAL PICODAC_I2S_LRCLK_PIN OR
       PICODAC_SPDIF_PIN EQUAL PICODAC_I2S_DATA_PIN OR
       PICODAC_SPDIF_PIN EQUAL PICODAC_I2S_BASE_CLOCK_PIN OR
       PICODAC_SPDIF_PIN EQUAL PICODAC_I2S_LRCLK_PIN)
        message(FATAL_ERROR "BOTH output requires four distinct valid GPIOs")
    endif()
endif()
set (PICODAC_VENDOR_ID 0xcafe CACHE STRING "USB Vendor ID")
set (PICODAC_PRODUCT_ID 0xbabe CACHE STRING "USB Product ID")

# Media buttons and host-controlled LEDs reserve these GPIOs.
math(EXPR PICODAC_I2S_LRCLK_PIN "${PICODAC_I2S_BASE_CLOCK_PIN} + 1")
foreach(control_pin 9 10 12 13 26 27)
    if((NOT PICODAC_OUTPUT STREQUAL "I2S" AND PICODAC_SPDIF_PIN EQUAL control_pin) OR
       (NOT PICODAC_OUTPUT STREQUAL "SPDIF" AND
        (PICODAC_I2S_DATA_PIN EQUAL control_pin OR
         PICODAC_I2S_BASE_CLOCK_PIN EQUAL control_pin OR
         PICODAC_I2S_LRCLK_PIN EQUAL control_pin)))
        message(FATAL_ERROR "Audio GPIO ${control_pin} conflicts with media buttons/LEDs")
    endif()
endforeach()





if(BOARD_NAME STREQUAL "rp2040")

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

    # pico-sdk requirement: include the SDK import before project()
    include(pico_sdk_import.cmake)

    # pico-sdk requirement: project() must come AFTER the include above,
    # and BEFORE pico_sdk_init() / add_subdirectory().
    project(mdac_adc2 C CXX ASM)

    # pico-sdk requirement: initialize the SDK AFTER project(),
    # and BEFORE adding any subdirectories/targets.
    pico_sdk_init()

    add_executable(mdac_adc2
        main.c
        audio_device.c
        blink.c
        ringbuffer.c
        usb.c
        usb_audio.c
        usb_hid.c
    )
    if(NOT PICODAC_OUTPUT STREQUAL "I2S")
        target_sources(mdac_adc2 PRIVATE spdif.c spdif_encode.c)
        target_compile_definitions(mdac_adc2 PRIVATE PICODAC_OUTPUT_SPDIF=1)
        pico_generate_pio_header(mdac_adc2 ${CMAKE_CURRENT_LIST_DIR}/spdif.pio)
    else()
        target_sources(mdac_adc2 PRIVATE i2s.c)
        target_compile_definitions(mdac_adc2 PRIVATE PICODAC_OUTPUT_SPDIF=0)
    endif()
    if(PICODAC_OUTPUT STREQUAL "BOTH")
        target_compile_definitions(mdac_adc2 PRIVATE PICODAC_OUTPUT_BOTH=1)
        pico_generate_pio_header(mdac_adc2 ${CMAKE_CURRENT_LIST_DIR}/i2s_mirror.pio)
    else()
        target_compile_definitions(mdac_adc2 PRIVATE PICODAC_OUTPUT_BOTH=0)
    endif()
    # Add the standard library to the build
    target_link_libraries(mdac_adc2
        hardware_dma
        hardware_pio
        pico_stdlib
    )

    target_compile_definitions(mdac_adc2 PRIVATE
        PICODAC_EAC3_PASSTHROUGH=$<BOOL:${PICODAC_EAC3_PASSTHROUGH}>
        PICODAC_SPDIF_PIN=${PICODAC_SPDIF_PIN}
        PICODAC_I2S_DATA_PIN=${PICODAC_I2S_DATA_PIN}
        PICODAC_I2S_BASE_CLOCK_PIN=${PICODAC_I2S_BASE_CLOCK_PIN}
        HID_ENABLE=1
        VENDOR_ID=${PICODAC_VENDOR_ID}
        PRODUCT_ID=${PICODAC_PRODUCT_ID}
    )

    # Add the standard include files to the build
    target_include_directories(mdac_adc2
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    pico_generate_pio_header(mdac_adc2
        ${CMAKE_CURRENT_LIST_DIR}/blink.pio
        ${CMAKE_CURRENT_LIST_DIR}/i2s.pio
    )

    pico_set_program_name(mdac_adc2 "mdac_adc2")
    pico_set_program_version(mdac_adc2 "0.2.0")

    # pico_set_binary_type(mdac_adc2 no_flash)

    # Modify the below lines to enable/disable output over UART/USB
    pico_enable_stdio_uart(mdac_adc2 1)
    pico_enable_stdio_usb(mdac_adc2 0)

    pico_add_extra_outputs(mdac_adc2)

endif()

# add_subdirectory(musb)
# target_link_libraries(mdac_adc2 musb)

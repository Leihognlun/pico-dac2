# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(APP_NAME "mdac_adc2")
set(BOARD_NAME "rp2040" CACHE STRING "Target board name")

set(PICODAC_INPUT "USB" CACHE STRING "Audio source: USB or SD_EAC3")
set_property(CACHE PICODAC_INPUT PROPERTY STRINGS USB SD_EAC3)
if(NOT PICODAC_INPUT MATCHES "^(USB|SD_EAC3)$")
    message(FATAL_ERROR "PICODAC_INPUT must be USB or SD_EAC3")
endif()
set(PICODAC_SD_SCK_PIN 2 CACHE STRING "TF SPI0 SCLK GPIO")
set(PICODAC_SD_MOSI_PIN 3 CACHE STRING "TF SPI0 TX/MOSI GPIO")
set(PICODAC_SD_MISO_PIN 4 CACHE STRING "TF SPI0 RX/MISO GPIO")
set(PICODAC_SD_CS_PIN 5 CACHE STRING "TF SPI0 CS GPIO")
set(PICODAC_SD_SPI_HZ 12000000 CACHE STRING "TF SPI transfer clock")
set(PICODAC_SD_FILE "0:/TRACK.EC3" CACHE STRING "Raw 48 kHz E-AC-3 file on TF")
option(PICODAC_SD_DIAGNOSTICS "Nonblocking UART0 SD playback diagnostics" OFF)
option(PICODAC_SD_UART_LOG "Enable UART log output in SD mode" ON)

# user configurations
set(PICODAC_OUTPUT "BOTH" CACHE STRING "Audio output: BOTH, SPDIF or I2S")
set_property(CACHE PICODAC_OUTPUT PROPERTY STRINGS BOTH SPDIF I2S)
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

if(PICODAC_INPUT STREQUAL "SD_EAC3")
    if(PICODAC_OUTPUT STREQUAL "I2S")
        message(FATAL_ERROR "SD_EAC3 requires SPDIF or BOTH output")
    endif()
    if(NOT PICODAC_SD_SCK_PIN MATCHES "^(2|6|18|22)$" OR
       NOT PICODAC_SD_MOSI_PIN MATCHES "^(3|7|19)$" OR
       NOT PICODAC_SD_MISO_PIN MATCHES "^(4|16|20)$" OR
       NOT PICODAC_SD_CS_PIN MATCHES "^([0-9]|1[0-9]|2[0-2]|2[6-8])$")
        message(FATAL_ERROR "TF requires exposed Pico hardware SPI0 pins (UART GPIO0/1 reserved)")
    endif()
    set(sd_pins ${PICODAC_SD_SCK_PIN} ${PICODAC_SD_MOSI_PIN}
                ${PICODAC_SD_MISO_PIN} ${PICODAC_SD_CS_PIN})
    list(REMOVE_DUPLICATES sd_pins)
    list(LENGTH sd_pins sd_pin_count)
    if(NOT sd_pin_count EQUAL 4)
        message(FATAL_ERROR "TF requires four distinct pins")
    endif()
    foreach(sd_pin IN LISTS sd_pins)
        if(sd_pin EQUAL 0 OR sd_pin EQUAL 1 OR sd_pin EQUAL PICODAC_SPDIF_PIN OR
           sd_pin MATCHES "^(9|10|11|12|13|14|26|27|28)$" OR
           (PICODAC_OUTPUT STREQUAL "BOTH" AND
            (sd_pin EQUAL PICODAC_I2S_DATA_PIN OR sd_pin EQUAL PICODAC_I2S_BASE_CLOCK_PIN OR
             sd_pin EQUAL PICODAC_I2S_LRCLK_PIN)))
            message(FATAL_ERROR "TF GPIO ${sd_pin} conflicts with audio/UART/control pins")
        endif()
    endforeach()
    if(NOT PICODAC_SD_SPI_HZ MATCHES "^[0-9]+$" OR
       PICODAC_SD_SPI_HZ LESS 100000 OR PICODAC_SD_SPI_HZ GREATER 25000000)
        message(FATAL_ERROR "TF SPI clock must be 100000..25000000 Hz")
    endif()
    if(NOT PICODAC_SD_FILE MATCHES "^0:/[A-Za-z0-9_./-]+$")
        message(FATAL_ERROR "TF file must be an ASCII 0:/ path without spaces or quotes")
    endif()
endif()





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
        blink.c
    )
    if(PICODAC_INPUT STREQUAL "SD_EAC3")
        include(FetchContent)
        FetchContent_Declare(pico_fatfs
            GIT_REPOSITORY https://github.com/elehobica/pico_fatfs.git
            GIT_TAG 417fca7fa519a915a7a697e3edf7b9a183243eaa
        )
        FetchContent_MakeAvailable(pico_fatfs)
        target_sources(mdac_adc2 PRIVATE eac3_burst.c sd_eac3_player.c)
        if(PICODAC_SD_DIAGNOSTICS)
            target_sources(mdac_adc2 PRIVATE sd_diagnostics.c)
            target_link_libraries(mdac_adc2 hardware_uart)
            target_compile_definitions(mdac_adc2 PRIVATE PICODAC_SD_DIAGNOSTICS=1
                PICODAC_SD_UART_LOG=$<BOOL:${PICODAC_SD_UART_LOG}>
                PICO_DEFAULT_UART=0 PICO_DEFAULT_UART_TX_PIN=0
                PICO_DEFAULT_UART_RX_PIN=1 PICO_DEFAULT_UART_BAUD_RATE=115200)
        endif()
        target_link_libraries(mdac_adc2 pico_fatfs pico_multicore)
        target_compile_definitions(mdac_adc2 PRIVATE
            PICODAC_INPUT_SD=1
            PICO_CORE1_STACK_SIZE=0x1000
            PICODAC_SD_SCK_PIN=${PICODAC_SD_SCK_PIN}
            PICODAC_SD_MOSI_PIN=${PICODAC_SD_MOSI_PIN}
            PICODAC_SD_MISO_PIN=${PICODAC_SD_MISO_PIN}
            PICODAC_SD_CS_PIN=${PICODAC_SD_CS_PIN}
            PICODAC_SD_SPI_HZ=${PICODAC_SD_SPI_HZ}
            PICODAC_SD_FILE="${PICODAC_SD_FILE}"
        )
    else()
        target_sources(mdac_adc2 PRIVATE audio_device.c ringbuffer.c usb.c usb_audio.c usb_hid.c)
        target_compile_definitions(mdac_adc2 PRIVATE PICODAC_INPUT_SD=0)
    endif()
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
    pico_set_program_version(mdac_adc2 "0.1")

    # pico_set_binary_type(mdac_adc2 no_flash)

    # Modify the below lines to enable/disable output over UART/USB
    pico_enable_stdio_uart(mdac_adc2 1)
    if(PICODAC_INPUT STREQUAL "SD_EAC3" AND NOT PICODAC_SD_UART_LOG)
        pico_enable_stdio_uart(mdac_adc2 0)
    endif()
    pico_enable_stdio_usb(mdac_adc2 0)

    pico_add_extra_outputs(mdac_adc2)

endif()

# add_subdirectory(musb)
# target_link_libraries(mdac_adc2 musb)

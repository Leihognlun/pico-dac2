# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(APP_NAME "mdac_adc2")
set(BOARD_NAME "rp2040" CACHE STRING "Target board name")
set(PICODAC_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/src")

set(PICODAC_INPUT "USB" CACHE STRING "Audio source: USB or SD_AUDIO")
set_property(CACHE PICODAC_INPUT PROPERTY STRINGS USB SD_AUDIO)
if(PICODAC_INPUT STREQUAL "SD_EAC3")
    set(PICODAC_INPUT "SD_AUDIO") # compatibility with older build scripts
endif()
if(NOT PICODAC_INPUT MATCHES "^(USB|SD_AUDIO)$")
    message(FATAL_ERROR "PICODAC_INPUT must be USB or SD_AUDIO")
endif()
include(boards/select.cmake)
set(PICODAC_SD_SPI_HZ 24000000 CACHE STRING "TF SPI transfer clock")
set(PICODAC_SD_FILE "0:/TRACK.WAV" CACHE STRING "WAV, AC3, or E-AC-3 file on TF")
option(PICODAC_SD_DIAGNOSTICS "Nonblocking UART0 SD playback diagnostics" ON)
option(PICODAC_SD_UART_LOG "Enable UART log output in SD mode" ON)
option(PICODAC_SD_BENCHMARK "Benchmark read-only TF/SPI throughput before playback" OFF)
set(PICODAC_ERROR_LED_GAP_MS 2000 CACHE STRING "Dark interval between error LED rounds (ms)")
set(PICODAC_ERROR_LED_STEP_MS 250 CACHE STRING "Duration of each error LED ON/OFF phase (ms)")
foreach(timing PICODAC_ERROR_LED_GAP_MS PICODAC_ERROR_LED_STEP_MS)
    if(NOT "${${timing}}" MATCHES "^[0-9]+$" OR
       ${timing} LESS 1 OR ${timing} GREATER 60000)
        message(FATAL_ERROR "${timing} must be 1..60000 ms")
    endif()
endforeach()

# user configurations
option(PICODAC_CEC "TV CEC / ARC control" ON)
option(PICODAC_DDC "Read-only EDID on each board DDC bus" ${PICODAC_CEC})
set(PICODAC_OUTPUT "SPDIF")
if(NOT PICODAC_SPDIF_PIN MATCHES "^[0-9]+$" OR PICODAC_SPDIF_PIN GREATER 29)
    message(FATAL_ERROR "PICODAC_SPDIF_PIN must be an RP2040 GPIO number (0..29)")
endif()
set (PICODAC_VENDOR_ID 0xcafe CACHE STRING "USB Vendor ID")
set (PICODAC_PRODUCT_ID 0xbabe CACHE STRING "USB Product ID")

# Check all board-owned pins, counting shared HPD only once.
set(board_pins ${BOARD_UART_TX} ${BOARD_UART_RX} ${BOARD_STATUS_LED}
    ${BOARD_HPD} ${BOARD_CONTROL_PINS} ${BOARD_SD_SCK} ${BOARD_SD_MOSI}
    ${BOARD_SD_MISO} ${BOARD_SD_CS})
foreach(port RANGE 1 ${BOARD_ARC_COUNT})
    foreach(signal TX CEC DETECT SDA SCL LED)
        list(APPEND board_pins ${BOARD_ARC${port}_${signal}})
    endforeach()
endforeach()
set(unique_pins ${board_pins})
list(REMOVE_DUPLICATES unique_pins)
list(LENGTH board_pins pin_count)
list(LENGTH unique_pins unique_count)
if(NOT pin_count EQUAL unique_count)
    message(FATAL_ERROR "Conflicting GPIO assignments in board ${PICODAC_BOARD}")
endif()
foreach(pin IN LISTS board_pins)
    if(NOT pin MATCHES "^[0-9]+$" OR pin GREATER 29)
        message(FATAL_ERROR "Invalid RP2040 GPIO ${pin}")
    endif()
endforeach()
if(NOT PICODAC_SD_SPI_HZ MATCHES "^[0-9]+$" OR
   PICODAC_SD_SPI_HZ LESS 100000 OR PICODAC_SD_SPI_HZ GREATER 25000000)
    message(FATAL_ERROR "TF SPI clock must be 100000..25000000 Hz")
endif()
if(NOT PICODAC_SD_FILE MATCHES "^0:/[A-Za-z0-9_./-]+$")
    message(FATAL_ERROR "TF file must be an ASCII 0:/ path without spaces or quotes")
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
        src/main.c
        src/board_status.c
        src/board_panic.c
    )
    target_compile_definitions(mdac_adc2 PRIVATE
        PICODAC_ERROR_LED_GAP_MS=${PICODAC_ERROR_LED_GAP_MS}
        PICODAC_ERROR_LED_STEP_MS=${PICODAC_ERROR_LED_STEP_MS}
        PICO_PANIC_FUNCTION=board_panic PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1
        PICODAC_BOARD_C2=$<STREQUAL:${PICODAC_BOARD},c2>
        PICO_DEFAULT_UART=0 PICO_DEFAULT_UART_TX_PIN=${BOARD_UART_TX}
        PICO_DEFAULT_UART_RX_PIN=${BOARD_UART_RX})
    if(PICODAC_BOARD STREQUAL "c2")
        target_sources(mdac_adc2 PRIVATE src/sd_controls.c)
    endif()
    target_compile_definitions(mdac_adc2 PRIVATE PICODAC_CEC=$<BOOL:${PICODAC_CEC}>)
    target_compile_definitions(mdac_adc2 PRIVATE PICODAC_DDC=$<BOOL:${PICODAC_DDC}>)
    if(PICODAC_DDC)
        target_sources(mdac_adc2 PRIVATE src/ddc_edid.c src/ddc_edid_data.c)
        target_link_libraries(mdac_adc2 pico_i2c_slave hardware_i2c)
    endif()
    if(PICODAC_CEC)
        target_sources(mdac_adc2 PRIVATE src/cec_wire.c src/cec_tv.c src/cec_arc.c)
        target_compile_definitions(mdac_adc2 PRIVATE PICODAC_CEC_PIN=${PICODAC_CEC_PIN})
    endif()
    if(PICODAC_INPUT STREQUAL "SD_AUDIO")
        include(FetchContent)
        FetchContent_Declare(pico_fatfs
            GIT_REPOSITORY https://github.com/elehobica/pico_fatfs.git
            GIT_TAG 417fca7fa519a915a7a697e3edf7b9a183243eaa
        )
        FetchContent_MakeAvailable(pico_fatfs)
        target_sources(mdac_adc2 PRIVATE src/eac3_burst.c src/ac3_burst.c
            src/wav_reader.c src/sd_eac3_player.c)
        if(NOT PICODAC_BOARD STREQUAL "c2")
            target_sources(mdac_adc2 PRIVATE src/sd_controls.c)
        endif()
        if(PICODAC_SD_DIAGNOSTICS)
            target_sources(mdac_adc2 PRIVATE src/sd_diagnostics.c)
            target_link_libraries(mdac_adc2 hardware_uart)
            target_compile_definitions(mdac_adc2 PRIVATE PICODAC_SD_DIAGNOSTICS=1
                PICODAC_SD_UART_LOG=$<BOOL:${PICODAC_SD_UART_LOG}>
                PICO_DEFAULT_UART_BAUD_RATE=115200)
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
            PICODAC_SD_BENCHMARK=$<BOOL:${PICODAC_SD_BENCHMARK}>
        )
    else()
        target_sources(mdac_adc2 PRIVATE src/audio_device.c src/ringbuffer.c
            src/usb.c src/usb_audio.c src/usb_hid.c)
        target_compile_definitions(mdac_adc2 PRIVATE PICODAC_INPUT_SD=0)
    endif()
    if(PICODAC_BOARD STREQUAL "c2")
        target_sources(mdac_adc2 PRIVATE src/spdif_dual.c)
    else()
        target_sources(mdac_adc2 PRIVATE src/spdif.c)
    endif()
    target_sources(mdac_adc2 PRIVATE src/spdif_encode.c)
    target_compile_definitions(mdac_adc2 PRIVATE PICODAC_OUTPUT_SPDIF=1 PICODAC_OUTPUT_BOTH=0)
    pico_generate_pio_header(mdac_adc2 ${PICODAC_SRC_DIR}/spdif.pio)
    # Add the standard library to the build
    target_link_libraries(mdac_adc2
        hardware_dma
        hardware_pwm
        hardware_pio
        pico_stdlib
    )

    target_compile_definitions(mdac_adc2 PRIVATE
        PICODAC_SPDIF_PIN=${PICODAC_SPDIF_PIN}
        HID_ENABLE=1
        VENDOR_ID=${PICODAC_VENDOR_ID}
        PRODUCT_ID=${PICODAC_PRODUCT_ID}
    )

    # Add the standard include files to the build
    target_include_directories(mdac_adc2
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICODAC_SRC_DIR}
    )


    pico_set_program_name(mdac_adc2 "mdac_adc2")
    pico_set_program_version(mdac_adc2 "0.1")

    # pico_set_binary_type(mdac_adc2 no_flash)

    # Modify the below lines to enable/disable output over UART/USB
    pico_enable_stdio_uart(mdac_adc2 1)
    if(PICODAC_INPUT STREQUAL "SD_AUDIO" AND NOT PICODAC_SD_UART_LOG)
        pico_enable_stdio_uart(mdac_adc2 0)
    endif()
    pico_enable_stdio_usb(mdac_adc2 0)

    pico_add_extra_outputs(mdac_adc2)

endif()

# add_subdirectory(musb)
# target_link_libraries(mdac_adc2 musb)

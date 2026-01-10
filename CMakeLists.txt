cmake_minimum_required(VERSION 3.17)
set(PICO_SDK_PATH "C:/RP2040/pico-sdk")
#include(${CMAKE_CURRENT_SOURCE_DIR}/../../../hw/bsp/family_support.cmake)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Initialize the SDK
pico_sdk_init()

project(spi_listner C CXX ASM)

## Checks this example is valid for the family and initializes the project
#family_initialize_project(spi_listner ${CMAKE_CURRENT_LIST_DIR})
## Espressif has its own cmake build system
#if(FAMILY STREQUAL "espressif")
#  return()
#endif()

add_executable(spi_listner)

#target_compile_definitions( spi_listner PRIVATE CFG_TUSB_MCU=OPT_MCU_RP2040 )

target_link_libraries( spi_listner PUBLIC
        pico_stdlib
)



get_target_property(OUT spi_listner LINK_LIBRARIES)
message(STATUS ${OUT})


target_link_libraries(spi_listner PUBLIC
        #tinyusb_port
        #tinyusb_common
        # tinyusb_device
        # tinyusb_board
        hardware_adc
        hardware_pwm
        hardware_pio
        hardware_i2c
        hardware_spi
        pico_multicore
)

get_target_property(OUT spi_listner LINK_LIBRARIES)
message(STATUS ${OUT})

# Example source
target_sources(spi_listner PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/spi_listner.c
        ${CMAKE_CURRENT_SOURCE_DIR}/spi_capture/spi_capture.c
        ${CMAKE_CURRENT_SOURCE_DIR}/decoder/decoder.c
        )

set_source_files_properties(spi_capture.c PROPERTIES COMPILE_FLAGS "-O3")

# Example include
target_include_directories(spi_listner PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/spi_capture
        ${CMAKE_CURRENT_SOURCE_DIR}/decoder
        )


pico_enable_stdio_uart(spi_listner 0)
pico_enable_stdio_usb(spi_listner 1)



pico_add_extra_outputs(spi_listner)

# Configure compilation flags and libraries for the example without RTOS.
# See the corresponding function in hw/bsp/FAMILY/family.cmake for details.
#family_configure_device_example(spi_listner noos)

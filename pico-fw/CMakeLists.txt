cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(playground C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

pico_sdk_init()

add_compile_options(-Wall -Wno-format -Wno-unused-function -Werror)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-maybe-uninitialized)
endif()

add_executable(
  firmware
  firmware.c videoout.c graphics.c

  vendor/libvterm/src/encoding.c
  vendor/libvterm/src/keyboard.c
  vendor/libvterm/src/mouse.c
  vendor/libvterm/src/parser.c
  vendor/libvterm/src/pen.c
  vendor/libvterm/src/screen.c
  vendor/libvterm/src/state.c
  vendor/libvterm/src/unicode.c
  vendor/libvterm/src/vterm.c
)

target_include_directories(
  firmware
  PUBLIC vendor/libtsm vendor/libvterm/include
)

pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/videoout.pio)
pico_enable_stdio_uart(firmware 0)
pico_enable_stdio_usb(firmware 1)

target_link_libraries(
  firmware
  pico_stdlib pico_sync pico_multicore
  hardware_pio hardware_clocks hardware_dma hardware_irq
)

pico_add_extra_outputs(firmware)

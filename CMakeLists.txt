cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD adafruit_fruit_jam CACHE STRING "Public target" FORCE)
set(PICO_PSRAM_CS_PIN 47 CACHE STRING "Fruit Jam PSRAM CS" FORCE)
set(PICO_PSRAM_SIZE_BYTES 0x800000 CACHE STRING "PSRAM size" FORCE)
if(NOT DEFINED PICO_SDK_PATH OR NOT DEFINED PICO_PIO_USB_PATH)
  message(FATAL_ERROR "Set PICO_SDK_PATH and PICO_PIO_USB_PATH to pinned public checkouts")
endif()
set(ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(DEP "${ROOT}/deps")
set(SHARED "${DEP}/pico_shared")
set(HDMI "${SHARED}/drivers/pico_hdmi")
set(FATFS "${SHARED}/drivers/pico_fatfs")
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(MSX1 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
set(SAVED_TINYUSB_TOP "${TOP}")
set(TOP "${PICO_SDK_PATH}/lib/tinyusb")
check_and_add_pico_pio_usb_support()
set(TOP "${SAVED_TINYUSB_TOP}")
add_subdirectory("${FATFS}" "${CMAKE_BINARY_DIR}/pico_fatfs")

add_library(msx_z80 OBJECT "${DEP}/Z80/sources/Z80.c")
target_include_directories(msx_z80 PRIVATE "${DEP}/Z80/API" "${DEP}/Zeta/API")
target_compile_definitions(msx_z80 PRIVATE Z80_STATIC)
target_compile_options(msx_z80 PRIVATE -O3 -fno-function-sections -fno-data-sections -fstack-usage)

add_library(msx1_core STATIC
  runtime/msx1_runtime.c runtime/msx1_vdp.c "${DEP}/devices/mapper.c"
  $<TARGET_OBJECTS:msx_z80> runtime/msx1_psg.c "${DEP}/emu2212/emu2212.c")
target_include_directories(msx1_core PUBLIC runtime PRIVATE
  "${DEP}/devices" "${DEP}/Z80/API" "${DEP}/Zeta/API" "${DEP}/emu2149" "${DEP}/emu2212")
target_compile_definitions(msx1_core PRIVATE Z80_STATIC PICO_MALLOC_PANIC=0)
target_compile_options(msx1_core PRIVATE -O3 -funsigned-char -ffunction-sections -fdata-sections -fstack-usage)
target_link_libraries(msx1_core PRIVATE pico_stdlib)
add_custom_command(TARGET msx1_core PRE_LINK
  COMMAND ${CMAKE_OBJCOPY} --rename-section .text=.time_critical.msx_z80 --rename-section .rodata=.time_critical.msx_z80_ro
  $<TARGET_OBJECTS:msx_z80>
  COMMAND_EXPAND_LISTS VERBATIM COMMENT "Place Z80 interpreter in SRAM")

add_library(msx1_hdmi STATIC "${HDMI}/hstx.c" "${HDMI}/video_output.c"
  "${HDMI}/hstx_data_island_queue.c" "${HDMI}/hstx_packet.c")
target_include_directories(msx1_hdmi PUBLIC "${HDMI}")
target_compile_definitions(msx1_hdmi PRIVATE GPIOHSTXD0=15 GPIOHSTXD1=17 GPIOHSTXD2=19
  GPIOHSTXCK=13 GPIOHSTXINVERTED=1 JACK_HSTX_126MHZ=1 JACK_HSTX_DOUBLE_BUFFER=1 DI_RING_BUFFER_SIZE=128)
target_compile_options(msx1_hdmi PRIVATE -O3)
target_link_libraries(msx1_hdmi PUBLIC pico_stdlib pico_multicore hardware_dma hardware_sync
  hardware_clocks hardware_vreg hardware_pio hardware_spi hardware_i2c hardware_pwm hardware_adc)

add_executable(picoMSX1 firmware/msx1.c platform/input/jack_input.c
  platform/input/jack_input_devices.c platform/input/jack_tinyusb_input.c platform/input/jack_nespad_input.c)
target_include_directories(picoMSX1 PRIVATE runtime platform/input platform/text "${SHARED}" "${FATFS}/fatfs")
target_compile_definitions(picoMSX1 PRIVATE HW_CONFIG=8 PICO_STACK_SIZE=4096 N3_SAVE=0
  R8_TITLE="MSX1" R8_DIRECTORY="/roms/MSX1" R8_EXTENSION=".rom" R8_BIOS="cbios_main_msx1.rom"
  R8_PROFILE=1 R8_ASYNC_PAD=1 R8_DEFERRED_PRESENT=1 R8_QUEUE_DIAG=0
  R8_VARIANT="MSX1_PUBLIC_INDEX4" R8_BUILD_VERSION="0.2.0-public-candidate"
  JACK_TINYUSB_KEYBOARD_RECOVERY=1 PICO_PIO_USE_GPIO_BASE=1 CFG_TUH_RPI_PIO_USB=1
  PIO_USB_USE_PIO=2 PICO_DEFAULT_PIO_USB_DP_PIN=1
  SDCARD_PIN_CS=39 SDCARD_PIN_SCK=34 SDCARD_PIN_MOSI=35 SDCARD_PIN_MISO=36 SDCARD_SPI=spi0 SDCARD_PIO=pio0)
target_compile_options(picoMSX1 PRIVATE -O3 -Wall -Wextra -Werror
  -Wno-unused-parameter -Wno-cast-function-type -Wno-missing-field-initializers
  -Wno-attributes -Wno-sign-compare -fstack-usage)
target_link_libraries(picoMSX1 PRIVATE msx1_core msx1_hdmi pico_fatfs pico_stdlib pico_multicore
  hardware_dma hardware_pio hardware_spi hardware_clocks hardware_vreg hardware_psram hardware_xip_cache
  tinyusb_host tinyusb_board tinyusb_pico_pio_usb m)
target_link_options(picoMSX1 PRIVATE -Wl,--gc-sections -Wl,--print-memory-usage)
pico_set_program_name(picoMSX1 "picoMSX1")
pico_set_program_version(picoMSX1 "0.2.0-public-candidate")
pico_enable_stdio_uart(picoMSX1 1)
pico_enable_stdio_usb(picoMSX1 0)
include("${ROOT}/cmake/LoaderPartition.cmake")
msx1_link_for_loader(picoMSX1)
pico_add_extra_outputs(picoMSX1)

# The fm1buddy USB MIDI host box: usbhost/build.sh makes build/fm1buddy-usbhost.uf2
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD adafruit_feather_rp2040_usb_host CACHE STRING "")
set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH}) # (TinyUSB builds it in, for its USB-A socket)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(fm1buddy_usbhost C CXX ASM)
pico_sdk_init()

add_executable(fm1buddy-usbhost main.c)
target_include_directories(fm1buddy-usbhost PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(fm1buddy-usbhost PRIVATE pico_stdlib hardware_uart hardware_irq tinyusb_host tinyusb_pico_pio_usb)
pico_add_extra_outputs(fm1buddy-usbhost)

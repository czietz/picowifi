cmake_minimum_required(VERSION 3.12)

# Build for Pico W unless speicifed otherwise
if (NOT DEFINED ENV{PICO_BOARD})
  if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "pico_w")
  endif()
endif()

# Pull in PICO SDK (must be before project)
include(pico_sdk_import.cmake)

# Note: this version defines the API version indicated in the USB descriptor
project(picowifi VERSION 1.0 LANGUAGES C CXX ASM)

# Initialize the SDK
pico_sdk_init()
include_directories("..")
include_directories(".")

add_executable(picowifi picowifi.c usb_descriptors.c)

# pull in common dependencies
target_link_libraries(picowifi pico_stdlib pico_util pico_multicore pico_cyw43_arch_none tinyusb_device tinyusb_board)

# create file with SVN revision infomation
add_custom_target(git_rev.h
 COMMAND git log -1 --date=short "--format=format:#define GIT_REVISION \"%h %cd\"%n" HEAD > git_rev.h
 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)

add_dependencies(picowifi git_rev.h)

# Debug UART
pico_enable_stdio_usb(picowifi 0)
pico_enable_stdio_uart(picowifi 1)

# Stack
target_compile_definitions(picowifi PRIVATE
PICO_STACK_SIZE=8192
PICO_CORE1_STACK_SIZE=4096
# see https://github.com/raspberrypi/pico-sdk/issues/1260
TUD_OPT_RP2040_USB_DEVICE_UFRAME_FIX=0
CYW43_SLEEP_CHECK_MS=3
FW_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
FW_VERSION_MINOR=${PROJECT_VERSION_MINOR}
)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(picowifi)


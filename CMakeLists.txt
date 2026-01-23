cmake_minimum_required(VERSION 3.13)

# Set board type
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in pico-extras (provides usb_device, etc.)
include(pico_extras_import.cmake)

project(rp2350_usb_audio C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Audio output backend: I2S to PCM5101 DAC (default) or PDM single-wire
option(AUDIO_PDM "Use PDM audio output on GP18 instead of I2S" OFF)

# Board / LCD selection: 2.8" ST7789T3 320x240 (default) or 0.96" ST7735S 160x80
option(BOARD_096 "Target Waveshare RP2350-LCD-0.96 (160x80 ST7735S)" OFF)

# Initialize the SDK
pico_sdk_init()

# Add our executable
add_executable(usb_audio
    src/main.c
    src/usb_audio.c
    src/font.c
    src/splash.c
    src/asrc.c
    src/stats.c
)

# Audio backend selection
if(AUDIO_PDM)
    target_sources(usb_audio PRIVATE src/pdm_audio.c)
    pico_generate_pio_header(usb_audio ${CMAKE_CURRENT_LIST_DIR}/src/pdm_audio.pio)
    target_compile_definitions(usb_audio PRIVATE AUDIO_PDM)
    message(STATUS "Audio backend: PDM (GP18)")
else()
    target_sources(usb_audio PRIVATE src/i2s_audio.c)
    pico_generate_pio_header(usb_audio ${CMAKE_CURRENT_LIST_DIR}/src/i2s_audio.pio)
    message(STATUS "Audio backend: I2S (GP2/GP3/GP4)")
endif()

# LCD / board selection
if(BOARD_096)
    target_sources(usb_audio PRIVATE src/lcd_096.c src/audio_ui_096.c)
    target_compile_definitions(usb_audio PRIVATE BOARD_096)
    message(STATUS "Board: RP2350-LCD-0.96 (160x80 ST7735S)")
else()
    target_sources(usb_audio PRIVATE src/lcd_028.c src/audio_ui_028.c)
    message(STATUS "Board: RP2350-Touch-LCD-2.8 (320x240 ST7789T3)")
endif()

pico_enable_stdio_usb(usb_audio 0)
pico_enable_stdio_uart(usb_audio 1)

target_include_directories(usb_audio PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# pico-extras USB device stack compile definitions
target_compile_definitions(usb_audio PRIVATE
    # Our interfaces are zero-based
    PICO_USBDEV_USE_ZERO_BASED_INTERFACES=1
    # Descriptor size for audio device
    PICO_USBDEV_MAX_DESCRIPTOR_SIZE=256
    # Required for isochronous endpoints
    PICO_USBDEV_ISOCHRONOUS_BUFFER_STRIDE_TYPE=1
)

target_link_libraries(usb_audio
    pico_stdlib
    pico_unique_id
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_spi
    usb_device
)

pico_add_extra_outputs(usb_audio)

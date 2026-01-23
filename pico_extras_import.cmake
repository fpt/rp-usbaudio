# This is a copy of <PICO_EXTRAS_PATH>/external/pico_extras_import.cmake

if (DEFINED ENV{PICO_EXTRAS_PATH} AND (NOT PICO_EXTRAS_PATH))
    set(PICO_EXTRAS_PATH $ENV{PICO_EXTRAS_PATH})
    message("Using PICO_EXTRAS_PATH from environment ('${PICO_EXTRAS_PATH}')")
endif ()

if (NOT PICO_EXTRAS_PATH)
    if (PICO_SDK_PATH AND EXISTS "${PICO_SDK_PATH}/../pico-extras")
        set(PICO_EXTRAS_PATH ${PICO_SDK_PATH}/../pico-extras)
        message("Defaulting PICO_EXTRAS_PATH as sibling of PICO_SDK_PATH: ${PICO_EXTRAS_PATH}")
    else()
        message(FATAL_ERROR
            "PICO_EXTRAS_PATH not set. Pass -DPICO_EXTRAS_PATH=/path/to/pico-extras to cmake.")
    endif()
endif ()

set(PICO_EXTRAS_PATH "${PICO_EXTRAS_PATH}" CACHE PATH "Path to the Pico Extras")

get_filename_component(PICO_EXTRAS_PATH "${PICO_EXTRAS_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_EXTRAS_PATH})
    message(FATAL_ERROR "Directory '${PICO_EXTRAS_PATH}' not found")
endif ()

set(PICO_EXTRAS_PATH ${PICO_EXTRAS_PATH} CACHE PATH "Path to the Pico Extras" FORCE)

add_subdirectory(${PICO_EXTRAS_PATH} pico_extras)

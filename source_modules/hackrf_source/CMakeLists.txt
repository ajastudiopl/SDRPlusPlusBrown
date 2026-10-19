cmake_minimum_required(VERSION 3.13)
project(hackrf_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(hackrf_source PRIVATE "C:/Program Files/PothosSDR/bin/")

    target_link_libraries(hackrf_source PRIVATE hackrf)
elseif (ANDROID)
    target_include_directories(hackrf_source PUBLIC
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/include/libhackrf
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/include
    )

    target_link_libraries(hackrf_source PUBLIC
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/lib/libusb1.0.so
            ${SDR_KIT_ROOT}/${ANDROID_ABI}/lib/libhackrf.so
    )
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBHACKRF REQUIRED libhackrf)

    target_include_directories(hackrf_source PRIVATE ${LIBHACKRF_INCLUDE_DIRS})
    target_link_directories(hackrf_source PRIVATE ${LIBHACKRF_LIBRARY_DIRS})
    target_link_libraries(hackrf_source PRIVATE ${LIBHACKRF_LIBRARIES})

    # libhackrf older than 2021 starts sweeps with hackrf_start_rx()
    include(CheckSymbolExists)
    set(CMAKE_REQUIRED_INCLUDES ${LIBHACKRF_INCLUDE_DIRS})
    foreach (_dir ${LIBHACKRF_INCLUDE_DIRS})
        list(APPEND CMAKE_REQUIRED_INCLUDES "${_dir}/..")
    endforeach ()
    set(CMAKE_REQUIRED_LIBRARIES ${LIBHACKRF_LINK_LIBRARIES})
    check_symbol_exists(hackrf_start_rx_sweep "libhackrf/hackrf.h" HACKRF_HAS_START_RX_SWEEP)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if (NOT HACKRF_HAS_START_RX_SWEEP)
        target_compile_definitions(hackrf_source PRIVATE SDRPP_HACKRF_NO_START_RX_SWEEP)
    endif ()
endif ()
cmake_minimum_required(VERSION 3.13)
# Standalone unit tests of the Spectrum Analyzer engine. They do not need the
# SDR++ core library, a GPU or a radio:
#   cmake -S misc_modules/spectrum_analyzer/tests -B build-sa-tests && cmake --build build-sa-tests && ./build-sa-tests/spectrum_analyzer_tests
project(spectrum_analyzer_tests CXX)

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif ()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(SA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(SDRPP_ROOT "${SA_ROOT}/../.." ABSOLUTE)

file(GLOB SA_ENGINE_SRC "${SA_ROOT}/src/engine/*.cpp")
add_executable(spectrum_analyzer_tests spectrum_analyzer_tests.cpp ${SA_ENGINE_SRC})
target_include_directories(spectrum_analyzer_tests PRIVATE "${SDRPP_ROOT}/core/src")

find_package(Threads REQUIRED)
target_link_libraries(spectrum_analyzer_tests PRIVATE Threads::Threads)

# Use FFTW (the library the SDR++ core uses) when available, otherwise a reference FFT.
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(SA_FFTW3F QUIET fftw3f)
endif ()
if (SA_FFTW3F_FOUND)
    target_compile_definitions(spectrum_analyzer_tests PRIVATE SA_TEST_HAVE_FFTW)
    target_include_directories(spectrum_analyzer_tests PRIVATE ${SA_FFTW3F_INCLUDE_DIRS})
    target_link_directories(spectrum_analyzer_tests PRIVATE ${SA_FFTW3F_LIBRARY_DIRS})
    target_link_libraries(spectrum_analyzer_tests PRIVATE ${SA_FFTW3F_LIBRARIES})
endif ()

if (MSVC)
    target_compile_definitions(spectrum_analyzer_tests PRIVATE _USE_MATH_DEFINES)
endif ()

enable_testing()
add_test(NAME spectrum_analyzer_tests COMMAND spectrum_analyzer_tests)

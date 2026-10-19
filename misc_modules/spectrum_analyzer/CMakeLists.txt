cmake_minimum_required(VERSION 3.13)
project(spectrum_analyzer)

file(GLOB SRC "src/*.cpp" "src/engine/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(spectrum_analyzer PRIVATE "src/")

if (MSVC)
    target_compile_definitions(spectrum_analyzer PRIVATE _USE_MATH_DEFINES)
endif ()

# Engine unit tests (standalone, no radio needed): cmake -DBUILD_TESTS=ON
if (BUILD_TESTS)
    add_subdirectory(tests)
endif ()

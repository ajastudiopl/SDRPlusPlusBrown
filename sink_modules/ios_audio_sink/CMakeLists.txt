cmake_minimum_required(VERSION 3.13)
project(ios_audio_sink)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_link_libraries(ios_audio_sink PRIVATE
    "-framework AudioToolbox"
    "-framework AVFoundation"
    "-framework CoreFoundation"
)

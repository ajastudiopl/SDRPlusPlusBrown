# Install script for directory: /work

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/root/Android/ndk/25.2.9519653/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/work/android/app/build/intermediates/cmake/debug/obj/armeabi-v7a/sdrpp")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/root/Android/ndk/25.2.9519653/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/work/root/res/bandplans")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/work/root/res/colormaps")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/work/root/res/fonts")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/work/root/res/icons")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/work/root/res/themes")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/work/root/res/cty")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/core/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/airspy_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/airspyhf_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/file_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/hackrf_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/hermes_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/hydrasdr_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/network_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/plutosdr_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/rfspace_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/rtl_sdr_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/rtl_tcp_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/sdrpp_server_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/spectran_http_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/spyserver_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/hl2_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/source_modules/kiwisdr_source/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/sink_modules/android_audio_sink/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/sink_modules/network_sink/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/sink_modules/null_audio_sink/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/atv_decoder/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/m17_decoder/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/ch_extravhf_decoder/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/ch_tetra_demodulator/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/ft8_decoder/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/meteor_demodulator/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/pager_decoder/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/decoder_modules/radio/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/frequency_manager/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/iq_exporter/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/recorder/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/rigctl_client/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/rigctl_server/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/websdr_view/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/reports_monitor/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/scanner/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/spectrum_analyzer/cmake_install.cmake")
  include("/work/android/app/.cxx/cmake/debug/armeabi-v7a/misc_modules/noise_reduction_logmmse/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/work/android/app/.cxx/cmake/debug/armeabi-v7a/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")

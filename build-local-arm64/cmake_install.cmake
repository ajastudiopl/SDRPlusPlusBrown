# Install script for directory: /Users/macbook/Documents/GitHub/SDRPlusPlusBrown

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/root_dev/inst")
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

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/sdrpp")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    execute_process(COMMAND /usr/bin/install_name_tool
      -delete_rpath "/opt/homebrew/Cellar/glfw/3.5.1/lib"
      -delete_rpath "/opt/homebrew/Cellar/fftw/3.3.11/lib"
      -delete_rpath "/opt/homebrew/Cellar/volk/3.3.0_2/lib"
      -delete_rpath "/opt/homebrew/opt/zstd/lib"
      -delete_rpath "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/core"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/CMakeFiles/sdrpp.dir/install-cxx-module-bmi-Release.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/root/res/bandplans")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/root/res/colormaps")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/root/res/fonts")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/root/res/icons")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/root/res/themes")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/root/res/cty")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/core/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/file_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/hermes_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/network_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/rfspace_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/rtl_tcp_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/sdrpp_server_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/spectran_http_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/spyserver_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/hl2_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/source_modules/kiwisdr_source/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/sink_modules/macos_coreaudio_sink/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/sink_modules/network_sink/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/sink_modules/null_audio_sink/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/decoder_modules/radio/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/misc_modules/websdr_view/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/misc_modules/reports_monitor/cmake_install.cmake")
  include("/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/misc_modules/spectrum_analyzer/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/macbook/Documents/GitHub/SDRPlusPlusBrown/build-local-arm64/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()

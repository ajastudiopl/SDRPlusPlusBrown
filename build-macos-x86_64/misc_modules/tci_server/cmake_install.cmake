# Install script for directory: /Users/ajastudioimacpro/Documents/GItHub/SDRPlusPlusBrown/misc_modules/tci_server

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/sdrpp/plugins" TYPE SHARED_LIBRARY FILES "/Users/ajastudioimacpro/Documents/GItHub/SDRPlusPlusBrown/build-macos-x86_64/misc_modules/tci_server/tci_server.dylib")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/sdrpp/plugins/tci_server.dylib" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/sdrpp/plugins/tci_server.dylib")
    execute_process(COMMAND /usr/bin/install_name_tool
      -delete_rpath "/usr/local/Cellar/glfw/3.5.1/lib"
      -delete_rpath "/usr/local/Cellar/fftw/3.3.11/lib"
      -delete_rpath "/private/tmp/claude-501/-Users-ajastudioimacpro-Documents-GItHub-SDRPlusPlusBrown/5b91e13d-52fa-4130-bd25-c0766a4dd57b/scratchpad/volk-mac-install/lib"
      -delete_rpath "/usr/local/opt/zstd/lib"
      -delete_rpath "/Users/ajastudioimacpro/Documents/GItHub/SDRPlusPlusBrown/build-macos-x86_64/core"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/sdrpp/plugins/tci_server.dylib")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -x "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/sdrpp/plugins/tci_server.dylib")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/Users/ajastudioimacpro/Documents/GItHub/SDRPlusPlusBrown/build-macos-x86_64/misc_modules/tci_server/CMakeFiles/tci_server.dir/install-cxx-module-bmi-Release.cmake" OPTIONAL)
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/ajastudioimacpro/Documents/GItHub/SDRPlusPlusBrown/build-macos-x86_64/misc_modules/tci_server/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()

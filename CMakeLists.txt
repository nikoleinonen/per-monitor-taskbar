cmake_minimum_required(VERSION 3.16)

# Short version used in the binary name: PMT-<version>.exe
set(PMT_VERSION "1.2")

project(per-monitor-taskbar VERSION ${PMT_VERSION}.0 LANGUAGES CXX RC)

configure_file(
  resources/pmt_version.h.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/pmt_version.h"
  @ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(per-monitor-taskbar WIN32
  src/main.cpp
  src/application.cpp
  src/settings_window.cpp
  src/monitor.cpp
  src/taskbar.cpp
  resources/app.rc
)

set_target_properties(per-monitor-taskbar PROPERTIES
  OUTPUT_NAME "PMT-${PMT_VERSION}"
)

target_include_directories(per-monitor-taskbar PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(per-monitor-taskbar PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(per-monitor-taskbar PRIVATE
  Shell32
  User32
  Gdi32
  Advapi32
  Comctl32
  Dwmapi
  Uxtheme
)

if(MSVC)
  target_compile_options(per-monitor-taskbar PRIVATE /W4 /permissive- /utf-8)
  # Manifest is embedded via app.rc; suppress linker-generated manifest.
  target_link_options(per-monitor-taskbar PRIVATE /MANIFEST:NO)
else()
  target_compile_options(per-monitor-taskbar PRIVATE -Wall -Wextra)
endif()

if(MINGW)
  target_link_options(per-monitor-taskbar PRIVATE -municode)
endif()

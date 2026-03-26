cmake_minimum_required(VERSION 3.16)
project(per-monitor-taskbar LANGUAGES CXX RC)

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

target_include_directories(per-monitor-taskbar PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources"
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

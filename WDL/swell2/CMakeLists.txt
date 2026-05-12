cmake_minimum_required(VERSION 3.14)
project(swell2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- find Skia ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(SKIA REQUIRED IMPORTED_TARGET skia)

# ---- swell2 sources ----
set(SWELL2_SOURCES
  swell-ini.cpp
  swell-gdi.cpp
  swell-wnd.cpp
  swell-backend-headless.cpp
  swell-stubs.cpp
)

# ---- swell2 library ----
add_library(Swell SHARED ${SWELL2_SOURCES})

target_include_directories(Swell PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Swell PRIVATE
  PkgConfig::SKIA
  pthread
  dl
)

# needed to suppress min/max macros from swell-types.h
target_compile_definitions(Swell PRIVATE NOMINMAX)

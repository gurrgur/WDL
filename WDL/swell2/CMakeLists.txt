cmake_minimum_required(VERSION 3.14)
project(swell2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- find Skia ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(SKIA REQUIRED IMPORTED_TARGET skia)

# ---- find SDL3 (optional) ----
pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)

# ---- swell2 sources ----
set(SWELL2_SOURCES
  swell-ini.cpp
  swell-gdi.cpp
  swell-wnd.cpp
  swell-dlg.cpp
  swell-controls.cpp
  swell-menu.cpp
  swell-misc.cpp
  swell-kb.cpp
  swell-stubs.cpp
  swell-appstub.cpp
  swell-lua.cpp
  swell-inspector.cpp
)

# ---- backend selection ----
if(SDL3_FOUND)
  message(STATUS "SDL3 found — building SDL3 OS backend")
  list(APPEND SWELL2_SOURCES swell-backend-sdl3.cpp)
  set(SWELL2_BACKEND_DEFINE SWELL_TARGET_SDL3)
  set(SWELL2_BACKEND_LIBS PkgConfig::SDL3)
else()
  message(STATUS "SDL3 NOT found — building headless OS backend")
  list(APPEND SWELL2_SOURCES swell-backend-headless.cpp)
endif()

# ---- swell2 library ----
add_library(Swell SHARED ${SWELL2_SOURCES})

option(SWELL2_FRAME_POINTERS "Build swell2 with frame pointers for profiling" OFF)
if(SWELL2_FRAME_POINTERS AND
    (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang"))
  target_compile_options(Swell PRIVATE
    -fno-omit-frame-pointer
    -mno-omit-leaf-frame-pointer
  )
endif()

target_include_directories(Swell PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# ---- Lua scripting (optional) ----
pkg_check_modules(LUA IMPORTED_TARGET lua)
if(LUA_FOUND)
  message(STATUS "Lua found — enabling swell Lua scripting engine")
  target_compile_definitions(Swell PRIVATE SWELL2_HAS_LUA)
  target_link_libraries(Swell PUBLIC PkgConfig::LUA)
else()
  message(STATUS "Lua NOT found — swell Lua scripting disabled")
endif()

target_link_libraries(Swell PRIVATE
  PkgConfig::SKIA
  pthread
  dl
  fontconfig
  ${SWELL2_BACKEND_LIBS}
)

# needed to suppress min/max macros from swell-types.h
target_compile_definitions(Swell PRIVATE NOMINMAX ${SWELL2_BACKEND_DEFINE})

# ---- benchmark/profiling tools ----
option(SWELL2_BUILD_BENCHMARKS "Build swell2 benchmark/profiling applications" ON)

if(SWELL2_BUILD_BENCHMARKS)
  add_executable(bench_swell_app
    bench_swell_app/bench_swell_app.cpp
  )

  target_include_directories(bench_swell_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )

  target_link_libraries(bench_swell_app PRIVATE
    Swell
  )

  target_compile_definitions(bench_swell_app PRIVATE NOMINMAX ${SWELL2_BACKEND_DEFINE})

  add_executable(bench_swell_window_app
    bench_swell_window_app/bench_swell_window_app.cpp
  )

  target_include_directories(bench_swell_window_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
  )

  target_link_libraries(bench_swell_window_app PRIVATE
    Swell
  )

  target_compile_definitions(bench_swell_window_app PRIVATE NOMINMAX ${SWELL2_BACKEND_DEFINE})
endif()

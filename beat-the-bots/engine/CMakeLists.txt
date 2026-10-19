cmake_minimum_required(VERSION 3.24)
project(beat_the_bots_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BTB_BUILD_TESTS "Build unit tests (native only)" ON)
option(BTB_BUILD_BENCH "Build benchmarks (native only)" ON)
option(BTB_WERROR "Treat warnings as errors" OFF)

# ---------------------------------------------------------------- engine lib
add_library(btb_engine STATIC
  src/types.cpp
  src/order_book.cpp
  src/exchange.cpp
)
target_include_directories(btb_engine PUBLIC include)
target_compile_options(btb_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(BTB_WERROR)
  target_compile_options(btb_engine PRIVATE -Werror)
endif()

if(EMSCRIPTEN)
  return()  # the wasm target is added in a later milestone
endif()

include(FetchContent)

# --------------------------------------------------------------------- tests
if(BTB_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(btb_tests
    tests/test_order_book.cpp
    tests/test_flat_map.cpp
    tests/test_exchange.cpp
  )
  target_link_libraries(btb_tests PRIVATE btb_engine Catch2::Catch2WithMain)
  enable_testing()
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(btb_tests)
endif()

# ---------------------------------------------------------------- benchmarks
if(BTB_BUILD_BENCH)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(benchmark)

  add_executable(btb_bench bench/bench_order_book.cpp)
  target_link_libraries(btb_bench PRIVATE btb_engine benchmark::benchmark)

  add_executable(btb_latency bench/latency.cpp)
  target_link_libraries(btb_latency PRIVATE btb_engine)
endif()

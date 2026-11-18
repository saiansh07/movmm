cmake_minimum_required(VERSION 3.10)
project(MoVMMEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(cpp)

# Engine library
add_library(movmm_engine
    cpp/heap_allocator.cpp
    cpp/mmu.cpp
    cpp/paging.cpp
    cpp/swap.cpp
    cpp/engine.cpp
    cpp/events.cpp
)

# Test runner
add_executable(engine_tests cpp/tests/test_engine.cpp)
target_link_libraries(engine_tests movmm_engine)

# CLI executable
add_executable(engine_cli cpp/engine_cli.cpp)
target_link_libraries(engine_cli movmm_engine)
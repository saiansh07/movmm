cmake_minimum_required(VERSION 3.10)
project(MoVMMEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Engine library
add_library(movmm_engine
    heap_allocator.cpp
    mmu.cpp
    paging.cpp
    swap.cpp
    engine.cpp
    events.cpp
)
target_include_directories(movmm_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Test runner
add_executable(engine_tests tests/test_engine.cpp)
target_link_libraries(engine_tests movmm_engine)
enable_testing()
add_test(NAME engine_tests COMMAND engine_tests)

# CLI executable
add_executable(engine_cli engine_cli.cpp)
target_link_libraries(engine_cli movmm_engine)

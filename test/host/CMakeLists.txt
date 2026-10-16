# Class U host unit tests (no ESP-IDF). Build:
#   cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host -V
cmake_minimum_required(VERSION 3.16)
project(nn2_host_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(nn2_host_tests
    test_main.c test_ring.c test_sequencer.c
    ${ROOT}/components/nn2_core/src/nn2_core_ring.c
    ${ROOT}/components/nn2_core/src/nn2_core_status.c
    ${ROOT}/components/nn2_app/src/nn2_app_sequencer.c)
target_include_directories(nn2_host_tests PRIVATE
    ${ROOT}/components/nn2_core/include
    ${ROOT}/components/nn2_ports/include
    ${ROOT}/components/nn2_app/include)
target_compile_options(nn2_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)
enable_testing()
add_test(NAME nn2_host_tests COMMAND nn2_host_tests)

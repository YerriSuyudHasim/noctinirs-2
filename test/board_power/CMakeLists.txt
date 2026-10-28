cmake_minimum_required(VERSION 3.16)
project(nn2_board_power_host C)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_executable(nn2_power_tests test_power.c
    "${ROOT}/components/nn2_board/nn2_board_power.c"
    "${ROOT}/components/nn2_app/src/nn2_app_battery.c")
target_include_directories(nn2_power_tests PRIVATE stubs
    "${ROOT}/components/nn2_board/include"
    "${ROOT}/components/nn2_core/include"
    "${ROOT}/components/nn2_ports/include"
    "${ROOT}/components/nn2_app/include")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nn2_power_tests PRIVATE -Wall -Wextra -Werror -pedantic)
elseif(MSVC)
    target_compile_options(nn2_power_tests PRIVATE /W4 /WX)
endif()
enable_testing()
foreach(case IN ITEMS normal schedule missing-gauge wrong-part readback safety-timer
        read-failure occupied cleanup-failure wrong-owner disabled-boot
        current-readback voltage-readback preserve-bits)
    add_test(NAME power_${case} COMMAND nn2_power_tests ${case})
endforeach()
foreach(step RANGE 1 23)
    add_test(NAME power_fault_${step} COMMAND nn2_power_tests fault ${step})
endforeach()

cmake_minimum_required(VERSION 3.16)
project(nn2_communication_host C)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_library(nn2_communication STATIC
    "${ROOT}/components/nn2_core/src/nn2_core_protocol.c"
    "${ROOT}/components/nn2_app/src/nn2_app_comm.c"
    "${ROOT}/components/nn2_app/src/nn2_app_provision.c")
target_include_directories(nn2_communication PUBLIC
    "${ROOT}/components/nn2_core/include"
    "${ROOT}/components/nn2_ports/include"
    "${ROOT}/components/nn2_app/include")
add_executable(nn2_comm_tests test_communication.c)
target_link_libraries(nn2_comm_tests PRIVATE nn2_communication)
add_executable(nn2_loopback loopback.c)
target_link_libraries(nn2_loopback PRIVATE nn2_communication)
if(WIN32)
    target_link_libraries(nn2_loopback PRIVATE ws2_32)
else()
    target_compile_definitions(nn2_loopback PRIVATE _POSIX_C_SOURCE=200809L)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(t nn2_communication nn2_comm_tests nn2_loopback)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Werror -pedantic)
    endforeach()
elseif(MSVC)
    foreach(t nn2_communication nn2_comm_tests nn2_loopback)
        target_compile_options(${t} PRIVATE /W4 /WX)
    endforeach()
endif()
enable_testing()
add_test(NAME communication_policy COMMAND nn2_comm_tests)

cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "4.0.0-idf61-migration")
set(IDF_TARGET esp32s3 CACHE STRING "Migration target")
# Reuse the actual NN2 communication implementation, not a second wire parser.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/nn2_core"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/nn2_ports"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/nn2_app"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/nn2_wifi"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/nn2_net"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/nn2_config")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(NoctiNIRSv4_IDF61)

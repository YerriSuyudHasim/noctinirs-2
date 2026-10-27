cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(noctinirs2)

if(NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 1)
    message(FATAL_ERROR "NoctiNIRS 2 requires ESP-IDF v6.1; do not substitute another SDK")
endif()

# Apply the inner-layer contract after ESP-IDF creates component targets.
include(cmake/nn2_c11.cmake)
nn2_enforce_idf_layers("${CMAKE_CURRENT_SOURCE_DIR}")

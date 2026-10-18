cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(noctinirs2)

# Apply the inner-layer contract after ESP-IDF creates component targets.
include(cmake/nn2_c11.cmake)
nn2_enforce_idf_layers("${CMAKE_CURRENT_SOURCE_DIR}")

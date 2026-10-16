cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "4.0.0-v4-7-2-idf-5-5-5-patch")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# ESP-IDF 5.5.5 MINIMAL_BUILD includes main and its declared transitive
# dependencies only. V4-7 intentionally includes Wi-Fi/lwIP through the network
# component, while BLE, LCD, and UI remain excluded from the component graph.
idf_build_set_property(MINIMAL_BUILD ON)
project(NoctiNIRSv4)

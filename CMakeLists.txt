cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(noctinirs2)

# VS Code / CLI board selection asserts the GENERATED policy, not just defaults.
set(NN2_EXPECT_LED_POLICY "" CACHE STRING "Explicit owner-selected LED policy (STRICT or BENCH_WARN)")
if(NN2_EXPECT_LED_POLICY)
    include(cmake/nn2_led_profile.cmake)
    nn2_check_led_profile("${NN2_EXPECT_LED_POLICY}" "${CONFIG_NN2_PROFILE_COMMUNICATION}"
        "${CONFIG_NN2_LED_BRINGUP}" "${CONFIG_NN2_LED_SHORT_BENCH}")
endif()

set(NN2_EXPECT_LED_I2C_HZ "" CACHE STRING "Explicit owner-selected LED runtime SCL")
if(NN2_EXPECT_LED_I2C_HZ)
    include(cmake/nn2_led_profile.cmake)
    nn2_check_led_transport("${NN2_EXPECT_LED_I2C_HZ}" "${CONFIG_NN2_LED_RUNTIME_I2C_HZ}"
        "${CONFIG_NN2_LED_BRINGUP}" "${CONFIG_I2C_ISR_IRAM_SAFE}")
endif()

if(NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 1)
    message(FATAL_ERROR "NoctiNIRS 2 requires ESP-IDF v6.1; do not substitute another SDK")
endif()

# Apply the inner-layer contract after ESP-IDF creates component targets.
include(cmake/nn2_c11.cmake)
nn2_enforce_idf_layers("${CMAKE_CURRENT_SOURCE_DIR}")

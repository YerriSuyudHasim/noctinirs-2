# components/nocti_ble/CMakeLists.txt

# The project may contain nocti_ble even when a specific firmware image does not
# enable Bluetooth. Compile the NimBLE implementation only when both Bluetooth
# and the NimBLE host are enabled in sdkconfig.
if(CONFIG_BT_ENABLED AND CONFIG_BT_NIMBLE_ENABLED)
    idf_component_register(
        SRCS
            "nocti_ble.c"
        INCLUDE_DIRS
            "include"
        REQUIRES
            bt
            nvs_flash
            freertos
            nocti_common
            nocti_stream
    )
else()
    # Header-only registration prevents an unused BLE component from breaking
    # Wi-Fi-only or fNIRS-only builds. A firmware that calls nocti_ble APIs must
    # enable Bluetooth + NimBLE first.
    idf_component_register(
        INCLUDE_DIRS
            "include"
        REQUIRES
            freertos
    )
endif()
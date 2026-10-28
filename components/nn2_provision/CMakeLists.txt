idf_component_register(SRCS "nn2_provision.c" INCLUDE_DIRS "include"
                       REQUIRES nn2_ports esp_common
                       PRIV_REQUIRES nn2_app esp_driver_usb_serial_jtag vfs)

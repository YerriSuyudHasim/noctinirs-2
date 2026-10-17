idf_component_register(
    SRCS "app_main.c" "nn2_wifi_bringup.c"
    INCLUDE_DIRS "."
    REQUIRES nn2_app nn2_ad_wifi nn2_ad_cred nn2_ad_prov nn2_rtos freertos
)

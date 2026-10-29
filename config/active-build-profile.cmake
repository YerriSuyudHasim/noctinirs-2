# Versioned with the branch. Do not infer a profile from a Git branch name.
set(NN2_BRANCH_PROFILE "ads131-acquisition")
set(NN2_BRANCH_DEFAULTS
    sdkconfig.defaults
    sdkconfig.communication
    sdkconfig.led-bringup
    sdkconfig.led-short-bench
    sdkconfig.acquisition)
set(NN2_BRANCH_REQUIRED_ON NN2_PROFILE_COMMUNICATION NN2_LED_BRINGUP
    NN2_OPTICAL_ACQUISITION NN2_LED_SHORT_BENCH I2C_ISR_IRAM_SAFE
    ESPTOOLPY_FLASHMODE_DIO ESPTOOLPY_FLASHSIZE_16MB PARTITION_TABLE_CUSTOM)
set(NN2_BRANCH_REQUIRED_OFF NN2_PROFILE_FAKE NN2_PROFILE_ADS131_DIAGNOSTIC SPIRAM)
set(NN2_BRANCH_LED_I2C_HZ 800000)

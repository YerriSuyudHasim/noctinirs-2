# LED checkpoint profile. This file is versioned with feat33.
set(NN2_BRANCH_PROFILE "led-bringup")
set(NN2_BRANCH_DEFAULTS
    sdkconfig.defaults
    sdkconfig.communication
    sdkconfig.led-bringup
    sdkconfig.led-short-bench)
set(NN2_BRANCH_REQUIRED_ON NN2_PROFILE_COMMUNICATION NN2_LED_BRINGUP
    NN2_LED_SHORT_BENCH I2C_ISR_IRAM_SAFE)
set(NN2_BRANCH_REQUIRED_OFF NN2_PROFILE_FAKE NN2_PROFILE_ADS131_DIAGNOSTIC
    NN2_OPTICAL_ACQUISITION SPIRAM)
set(NN2_BRANCH_LED_I2C_HZ 800000)

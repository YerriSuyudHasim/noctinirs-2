/* PCA9956B and IO18 parameters that issue #49, the accepted ADRs, and active
 * main config do not state. No defaults. The real target fails closed until
 * Agent 04 defines every symbol below. Host policy tests do not include this
 * header and may inject fakes only outside the product build.
 *
 * Stated elsewhere, and therefore not symbols here:
 *   RESET_LED = IO18, push-pull low in the bootloader (nn2_reset_led_pin.h)
 *   IREFALL = 0x40, PWMALL = 0x3F, off value = 0x00 (nn2_port_emitter_safe.h)
 *   release wait = 2 ms (nn2_app_safe_boot.h)
 */
#ifndef NN2_EMITTER_SAFE_HW_H
#define NN2_EMITTER_SAFE_HW_H

#if !defined(NN2_PCA9956B_I2C_PORT)
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_PORT
#endif
#if !defined(NN2_PCA9956B_SDA_GPIO)
#error HW-TBD (Agent 04): NN2_PCA9956B_SDA_GPIO
#endif
#if !defined(NN2_PCA9956B_SCL_GPIO)
#error HW-TBD (Agent 04): NN2_PCA9956B_SCL_GPIO
#endif
#if !defined(NN2_PCA9956B_ADDR_7BIT)
#error HW-TBD (Agent 04): NN2_PCA9956B_ADDR_7BIT
#endif
#if !defined(NN2_PCA9956B_I2C_TIMEOUT_MS)
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_TIMEOUT_MS
#endif
#if !defined(NN2_PCA9956B_I2C_SCL_HZ)
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_SCL_HZ
#endif
#if !defined(NN2_PCA9956B_I2C_SCL_WAIT_US)
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_SCL_WAIT_US
#endif
#if !defined(NN2_PCA9956B_I2C_GLITCH_IGNORE_CNT)
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_GLITCH_IGNORE_CNT
#endif
#if !defined(NN2_PCA9956B_I2C_INTERNAL_PULLUP)
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_INTERNAL_PULLUP
#endif
#if !defined(NN2_PCA9956B_REG_LEDOUT0)
#error HW-TBD (Agent 04): NN2_PCA9956B_REG_LEDOUT0
#endif
#if !defined(NN2_PCA9956B_REG_LEDOUT_STRIDE)
#error HW-TBD (Agent 04): NN2_PCA9956B_REG_LEDOUT_STRIDE
#endif
#if !defined(NN2_PCA9956B_REG_IREF0)
#error HW-TBD (Agent 04): NN2_PCA9956B_REG_IREF0
#endif
#if !defined(NN2_PCA9956B_REG_IREF_STRIDE)
#error HW-TBD (Agent 04): NN2_PCA9956B_REG_IREF_STRIDE
#endif
#if !defined(NN2_IO18_RELEASE_DRIVES_HIGH)
#error HW-TBD (Agent 04): NN2_IO18_RELEASE_DRIVES_HIGH
#endif

#if defined(NN2_PCA9956B_I2C_PORT) && defined(NN2_PCA9956B_SDA_GPIO) && \
    defined(NN2_PCA9956B_SCL_GPIO) && defined(NN2_PCA9956B_ADDR_7BIT) && \
    defined(NN2_PCA9956B_I2C_TIMEOUT_MS) && defined(NN2_PCA9956B_I2C_SCL_HZ) && \
    defined(NN2_PCA9956B_I2C_SCL_WAIT_US) && defined(NN2_PCA9956B_I2C_GLITCH_IGNORE_CNT) && \
    defined(NN2_PCA9956B_I2C_INTERNAL_PULLUP) && defined(NN2_PCA9956B_REG_LEDOUT0) && \
    defined(NN2_PCA9956B_REG_LEDOUT_STRIDE) && defined(NN2_PCA9956B_REG_IREF0) && \
    defined(NN2_PCA9956B_REG_IREF_STRIDE) && defined(NN2_IO18_RELEASE_DRIVES_HIGH)

#if NN2_PCA9956B_ADDR_7BIT > 0x7F
#error HW-TBD (Agent 04): NN2_PCA9956B_ADDR_7BIT must be a 7-bit address
#endif
#if NN2_PCA9956B_I2C_SCL_HZ < 1
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_SCL_HZ must be positive
#endif
#if NN2_PCA9956B_I2C_GLITCH_IGNORE_CNT > 255
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_GLITCH_IGNORE_CNT does not fit in uint8_t
#endif
#if NN2_PCA9956B_I2C_INTERNAL_PULLUP != 0 && NN2_PCA9956B_I2C_INTERNAL_PULLUP != 1
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_INTERNAL_PULLUP must be 0 or 1
#endif
#if NN2_IO18_RELEASE_DRIVES_HIGH != 0 && NN2_IO18_RELEASE_DRIVES_HIGH != 1
#error HW-TBD (Agent 04): NN2_IO18_RELEASE_DRIVES_HIGH must be 0 or 1
#endif
#if (NN2_PCA9956B_REG_LEDOUT0 + (NN2_PCA9956B_REG_LEDOUT_STRIDE * 5)) > 255
#error HW-TBD (Agent 04): LEDOUT0-5 address span does not fit in one byte
#endif
#if (NN2_PCA9956B_REG_IREF0 + (NN2_PCA9956B_REG_IREF_STRIDE * 23)) > 255
#error HW-TBD (Agent 04): IREF0-23 address span does not fit in one byte
#endif

#include "sdkconfig.h"
#if NN2_PCA9956B_I2C_TIMEOUT_MS < 1
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_TIMEOUT_MS must be a positive millisecond bound
#endif
#if NN2_PCA9956B_I2C_TIMEOUT_MS < ((1000 + CONFIG_FREERTOS_HZ - 1) / CONFIG_FREERTOS_HZ)
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_TIMEOUT_MS is shorter than one FreeRTOS tick
#endif
#if NN2_PCA9956B_I2C_TIMEOUT_MS == -1
#error HW-TBD (Agent 04): NN2_PCA9956B_I2C_TIMEOUT_MS must not request an unbounded wait
#endif

#endif /* all HW-TBD symbols defined */

#endif /* NN2_EMITTER_SAFE_HW_H */

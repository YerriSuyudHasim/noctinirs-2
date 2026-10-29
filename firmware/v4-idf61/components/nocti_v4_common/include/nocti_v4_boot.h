#pragma once

/* Staged boot policy. The stage list follows feat35 app_main at
 * 31fb2a9101fbb12a59e52506464d476e184bbdd9 (same list at df796ba and edb7041).
 * Flags: 1 success, 0 failure, -1 not attempted. */

typedef enum {
    NOCTI_V4_BOOT_SAFE_GPIO = 0,
    NOCTI_V4_BOOT_CORE,
    NOCTI_V4_BOOT_I2C,
    NOCTI_V4_BOOT_PCA,
    NOCTI_V4_BOOT_ADS,
    NOCTI_V4_BOOT_RUNTIME,
    NOCTI_V4_BOOT_NETWORK,
    NOCTI_V4_BOOT_READY
} nocti_v4_boot_stage_t;

typedef struct {
    int gpio;
    int core;
    int i2c;
    int pca;
    int ads;
    int runtime;
    int network;
} nocti_v4_boot_flags_t;

static inline int nocti_v4_boot_attempt_runtime(nocti_v4_boot_flags_t flags)
{
    return flags.gpio == 1 && flags.core == 1 && flags.i2c == 1 &&
           flags.pca == 1 && flags.ads == 1;
}

/* UDP still starts after a later hardware failure so nn2ctl can report it.
 * Safe-GPIO failure does not start the network: pin ownership is unknown. */
static inline int nocti_v4_boot_attempt_network(nocti_v4_boot_flags_t flags)
{
    return flags.gpio == 1;
}

/* A zero flag is a completed failure. -1 means not attempted yet. */
static inline int nocti_v4_boot_hardware_failed(nocti_v4_boot_flags_t flags)
{
    return flags.gpio == 0 || flags.core == 0 || flags.i2c == 0 ||
           flags.pca == 0 || flags.ads == 0 || flags.runtime == 0;
}

static inline nocti_v4_boot_stage_t nocti_v4_boot_fault_stage(nocti_v4_boot_flags_t flags)
{
    const int step[] = {
        flags.gpio, flags.core, flags.i2c, flags.pca,
        flags.ads, flags.runtime, flags.network
    };
    unsigned index;
    for (index = 0u; index < 7u; ++index) {
        if (step[index] == 0) {
            return (nocti_v4_boot_stage_t)index;
        }
    }
    for (index = 0u; index < 7u; ++index) {
        if (step[index] != 1) {
            return (nocti_v4_boot_stage_t)index;
        }
    }
    return NOCTI_V4_BOOT_READY;
}

static inline const char *nocti_v4_boot_stage_name(nocti_v4_boot_stage_t stage)
{
    switch (stage) {
    case NOCTI_V4_BOOT_SAFE_GPIO: return "safe_gpio";
    case NOCTI_V4_BOOT_CORE: return "core";
    case NOCTI_V4_BOOT_I2C: return "i2c";
    case NOCTI_V4_BOOT_PCA: return "pca";
    case NOCTI_V4_BOOT_ADS: return "ads";
    case NOCTI_V4_BOOT_RUNTIME: return "runtime";
    case NOCTI_V4_BOOT_NETWORK: return "network";
    case NOCTI_V4_BOOT_READY: return "ready";
    default: return "unknown";
    }
}

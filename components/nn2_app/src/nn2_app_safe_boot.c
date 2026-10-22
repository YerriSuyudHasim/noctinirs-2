#include "nn2_app_safe_boot.h"

_Static_assert(NN2_APP_SAFE_BOOT_RELEASE_MS * 10u >= NN2_APP_SAFE_BOOT_RELEASE_MIN_TENTHS_MS,
    "issue #49 requires a RESET# release wait of at least 1.5 ms");
_Static_assert(NN2_APP_SAFE_BOOT_ATTEMPT_LIMIT == 1u, "safe-off has no retry loop");
_Static_assert(NN2_PORT_EMITTER_SAFE_REG_IREFALL == 0x40u, "issue #49 IREFALL");
_Static_assert(NN2_PORT_EMITTER_SAFE_REG_PWMALL == 0x3Fu, "issue #49 PWMALL");
_Static_assert(NN2_PORT_EMITTER_SAFE_REG_OFF == 0x00u, "issue #49 off value");
_Static_assert(NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT == 6u, "issue #49 LEDOUT0-5");
_Static_assert(NN2_PORT_EMITTER_SAFE_IREF_COUNT == 24u, "issue #49 IREF0-23");

const char *nn2_app_reset_reason_str(nn2_reset_reason_t reason)
{
    switch (reason) {
    case NN2_RESET_UNKNOWN: return "UNKNOWN";
    case NN2_RESET_POWERON: return "POWERON";
    case NN2_RESET_EXT: return "EXT";
    case NN2_RESET_SW: return "SW";
    case NN2_RESET_PANIC: return "PANIC";
    case NN2_RESET_INT_WDT: return "INT_WDT";
    case NN2_RESET_TASK_WDT: return "TASK_WDT";
    case NN2_RESET_WDT: return "WDT";
    case NN2_RESET_DEEPSLEEP: return "DEEPSLEEP";
    case NN2_RESET_BROWNOUT: return "BROWNOUT";
    case NN2_RESET_SDIO: return "SDIO";
    case NN2_RESET_USB: return "USB";
    case NN2_RESET_JTAG: return "JTAG";
    case NN2_RESET_EFUSE: return "EFUSE";
    case NN2_RESET_PWR_GLITCH: return "PWR_GLITCH";
    case NN2_RESET_CPU_LOCKUP: return "CPU_LOCKUP";
    default: return "UNKNOWN";
    }
}

const char *nn2_app_safe_boot_outcome_str(nn2_boot_outcome_t outcome)
{
    switch (outcome) {
    case NN2_BOOT_OK: return "OK";
    case NN2_BOOT_FAULTED: return "FAULTED (BOOT)";
    case NN2_BOOT_FAULTED_SPIRAM: return "FAULTED (SPIRAM)";
    default: return "UNKNOWN";
    }
}

static bool port_ready(const nn2_port_emitter_safe_t *port)
{
    return port->set_reset_asserted && port->write_irefall && port->write_pwmall &&
        port->write_ledout && port->read_iref && port->read_ledout && port->delay_ms;
}

static nn2_status_t fail_closed(const nn2_port_emitter_safe_t *port,
    nn2_app_safe_boot_error_fn on_error, void *log_ctx, nn2_app_safe_boot_result_t *out,
    nn2_boot_outcome_t outcome, nn2_status_t error)
{
    nn2_status_t held = port->set_reset_asserted(port->ctx, true);
    out->reset_held_low = (held == NN2_OK);
    out->outcome = outcome;
    out->error = error;
    out->emitter_enabled = false;
    on_error(log_ctx, outcome, error);
    return error;
}

nn2_status_t nn2_app_safe_boot_run(const nn2_app_safe_boot_input_t *in,
    const nn2_port_emitter_safe_t *port, nn2_app_safe_boot_error_fn on_error,
    void *log_ctx, nn2_app_safe_boot_result_t *out)
{
    nn2_status_t st;
    uint8_t index;
    uint8_t value;

    if (!in || !port || !on_error || !out || !port_ready(port)) {
        return NN2_ERR_INVALID_ARG;
    }
    if ((unsigned)in->reset_reason >= (unsigned)NN2_RESET_COUNT_) {
        return NN2_ERR_INVALID_ARG;
    }

    out->outcome = NN2_BOOT_FAULTED;
    out->error = NN2_ERR_INVALID_STATE;
    out->reset_reason = in->reset_reason;
    out->attempts = 0u;
    out->reset_held_low = false;
    out->emitter_enabled = false;

    if (in->spiram_bytes != 0u) {
        return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED_SPIRAM,
            NN2_ERR_INVALID_STATE);
    }

    st = port->set_reset_asserted(port->ctx, true);
    if (st != NN2_OK) {
        out->outcome = NN2_BOOT_FAULTED;
        out->error = st;
        out->reset_held_low = false;
        on_error(log_ctx, NN2_BOOT_FAULTED, st);
        return st;
    }
    out->reset_held_low = true;

    st = port->set_reset_asserted(port->ctx, false);
    if (st != NN2_OK) {
        return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED, st);
    }
    out->reset_held_low = false;
    out->attempts = NN2_APP_SAFE_BOOT_ATTEMPT_LIMIT;

    st = port->delay_ms(port->ctx, NN2_APP_SAFE_BOOT_RELEASE_MS);
    if (st != NN2_OK) {
        return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED, st);
    }

    st = port->write_irefall(port->ctx, (uint8_t)NN2_PORT_EMITTER_SAFE_REG_OFF);
    if (st != NN2_OK) {
        return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED, st);
    }
    st = port->write_pwmall(port->ctx, (uint8_t)NN2_PORT_EMITTER_SAFE_REG_OFF);
    if (st != NN2_OK) {
        return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED, st);
    }
    for (index = 0u; index < NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT; index++) {
        st = port->write_ledout(port->ctx, index, (uint8_t)NN2_PORT_EMITTER_SAFE_REG_OFF);
        if (st != NN2_OK) {
            return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED, st);
        }
    }
    for (index = 0u; index < NN2_PORT_EMITTER_SAFE_IREF_COUNT; index++) {
        value = 0xffu;
        st = port->read_iref(port->ctx, index, &value);
        if (st != NN2_OK || value != NN2_PORT_EMITTER_SAFE_REG_OFF) {
            if (st == NN2_OK) {
                st = NN2_ERR_IO;
            }
            return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED, st);
        }
    }
    for (index = 0u; index < NN2_PORT_EMITTER_SAFE_LEDOUT_COUNT; index++) {
        value = 0xffu;
        st = port->read_ledout(port->ctx, index, &value);
        if (st != NN2_OK || value != NN2_PORT_EMITTER_SAFE_REG_OFF) {
            if (st == NN2_OK) {
                st = NN2_ERR_IO;
            }
            return fail_closed(port, on_error, log_ctx, out, NN2_BOOT_FAULTED, st);
        }
    }

    st = port->set_reset_asserted(port->ctx, true);
    out->reset_held_low = (st == NN2_OK);
    if (st != NN2_OK) {
        out->outcome = NN2_BOOT_FAULTED;
        out->error = st;
        out->emitter_enabled = false;
        on_error(log_ctx, NN2_BOOT_FAULTED, st);
        return st;
    }
    out->outcome = NN2_BOOT_OK;
    out->error = NN2_OK;
    out->emitter_enabled = false;
    return NN2_OK;
}

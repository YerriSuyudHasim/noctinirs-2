#include "nn2_app_sequencer_internal.h"
#include <string.h>

static void seq_log(const nn2_app_seq_internal_t *seq, nn2_log_level_t lvl, const char *msg)
{
    if (seq->cfg.log.write != NULL) {
        seq->cfg.log.write(seq->cfg.log.ctx, lvl, msg);
    }
}

static nn2_status_t seq_fault(nn2_app_seq_internal_t *seq, nn2_status_t err, const char *msg)
{
    /* Fail-safe: request LED off on every error exit. The original error is
     * returned; a failed off() is additionally logged and counted. The
     * sequencer stays in FAULT either way; only a successful reset() leaves it. */
    nn2_status_t off_st = seq->cfg.led.off(seq->cfg.led.ctx, seq->cfg.channel);
    if (off_st != NN2_OK) {
        seq->off_failures++;
        seq_log(seq, NN2_LOG_ERROR, "seq: fail-safe led off FAILED");
    }
    seq->state = NN2_APP_SEQ_FAULT;
    seq->faults++;
    seq->last_error = err;
    seq_log(seq, NN2_LOG_ERROR, msg);
    return err;
}

static nn2_status_t seq_step(nn2_app_seq_internal_t *seq)
{
    nn2_status_t st;
    if (seq == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    switch (seq->state) {
    case NN2_APP_SEQ_IDLE:
        memset(&seq->pending, 0, sizeof(seq->pending));
        seq->pending.seq = seq->next_seq;
        seq->pending.channel = seq->cfg.channel;
        seq->pending.src = seq->cfg.adc.source;
        st = seq->cfg.led.on(seq->cfg.led.ctx, seq->cfg.channel);
        if (st != NN2_OK) {
            return seq_fault(seq, st, "seq: led on failed");
        }
        seq->pending.t_led_on_us = seq->cfg.clock.now_us(seq->cfg.clock.ctx);
        seq->state = NN2_APP_SEQ_LED_ON;
        return NN2_OK;

    case NN2_APP_SEQ_LED_ON:
        if (seq->cfg.clock.delay_us != NULL && seq->cfg.settle_us > 0u) {
            seq->cfg.clock.delay_us(seq->cfg.clock.ctx, seq->cfg.settle_us);
        }
        seq->state = NN2_APP_SEQ_SAMPLE;
        return NN2_OK;

    case NN2_APP_SEQ_SAMPLE: {
        uint32_t raw = 0u;
        st = seq->cfg.adc.read(seq->cfg.adc.ctx, seq->cfg.channel, &raw);
        if (st != NN2_OK) {
            return seq_fault(seq, st, "seq: adc read failed");
        }
        seq->pending.raw = raw;
        seq->pending.t_sample_us = seq->cfg.clock.now_us(seq->cfg.clock.ctx);
        seq->state = NN2_APP_SEQ_LED_OFF;
        return NN2_OK;
    }

    case NN2_APP_SEQ_LED_OFF:
        st = seq->cfg.led.off(seq->cfg.led.ctx, seq->cfg.channel);
        if (st != NN2_OK) {
            return seq_fault(seq, st, "seq: led off failed");
        }
        seq->next_seq++;
        st = nn2_core_ring_push(seq->cfg.ring, &seq->pending);
        if (st != NN2_OK) {
            /* Drop-and-count (ring tracks drops); not a fault. */
            seq_log(seq, NN2_LOG_WARN, "seq: ring full, sample dropped");
        }
        seq->cycles_ok++;
        seq->state = NN2_APP_SEQ_IDLE;
        return NN2_OK;

    case NN2_APP_SEQ_FAULT:
    default:
        return NN2_ERR_INVALID_STATE;
    }
}

static nn2_status_t seq_run_cycle(nn2_app_seq_internal_t *seq)
{
    if (seq == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (seq->state != NN2_APP_SEQ_IDLE) {
        return NN2_ERR_INVALID_STATE;
    }
    do {
        nn2_status_t st = seq_step(seq);
        if (st != NN2_OK) {
            return st;
        }
    } while (seq->state != NN2_APP_SEQ_IDLE);
    return NN2_OK;
}

static nn2_status_t seq_reset(nn2_app_seq_internal_t *seq)
{
    if (seq == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    /* Leave FAULT only after the injected off() reports success.
     * This status alone cannot establish a physical safe state. */
    nn2_status_t st = seq->cfg.led.off(seq->cfg.led.ctx, seq->cfg.channel);
    if (st != NN2_OK) {
        seq->off_failures++;
        seq->state = NN2_APP_SEQ_FAULT;
        seq->last_error = st;
        seq_log(seq, NN2_LOG_ERROR, "seq: reset led off failed, staying in FAULT");
        return st;
    }
    seq->state = NN2_APP_SEQ_IDLE;
    seq->last_error = NN2_OK;
    return NN2_OK;
}

nn2_status_t nn2_app_sequencer_init(nn2_app_seq_storage_t *storage,
    nn2_app_sequencer_t **out, const nn2_app_seq_config_t *cfg)
{
    if (storage == NULL || out == NULL || cfg == NULL || cfg->ring == NULL ||
        cfg->led.on == NULL || cfg->led.off == NULL || cfg->adc.read == NULL ||
        cfg->clock.now_us == NULL) return NN2_ERR_INVALID_ARG;
    nn2_app_seq_internal_t state = {0};
    state.cfg = *cfg;
    state.state = NN2_APP_SEQ_IDLE;
    state.last_error = NN2_OK;
    nn2_app_sequencer_t *handle = (nn2_app_sequencer_t *)(void *)storage;
    memset(storage->opaque, 0, sizeof(storage->opaque));
    nn2_app_seq_save(handle, &state);
    *out = handle;
    return NN2_OK;
}

nn2_status_t nn2_app_sequencer_step(nn2_app_sequencer_t *seq)
{
    if (seq == NULL) return NN2_ERR_INVALID_ARG;
    nn2_app_seq_internal_t state;
    nn2_app_seq_load(seq, &state);
    nn2_status_t status = seq_step(&state);
    nn2_app_seq_save(seq, &state);
    return status;
}

nn2_status_t nn2_app_sequencer_run_cycle(nn2_app_sequencer_t *seq)
{
    if (seq == NULL) return NN2_ERR_INVALID_ARG;
    nn2_app_seq_internal_t state;
    nn2_app_seq_load(seq, &state);
    nn2_status_t status = seq_run_cycle(&state);
    nn2_app_seq_save(seq, &state);
    return status;
}

nn2_status_t nn2_app_sequencer_reset(nn2_app_sequencer_t *seq)
{
    if (seq == NULL) return NN2_ERR_INVALID_ARG;
    nn2_app_seq_internal_t state;
    nn2_app_seq_load(seq, &state);
    nn2_status_t status = seq_reset(&state);
    nn2_app_seq_save(seq, &state);
    return status;
}

nn2_status_t nn2_app_sequencer_observe(const nn2_app_sequencer_t *seq,
    nn2_app_seq_observation_t *out)
{
    if (seq == NULL || out == NULL) return NN2_ERR_INVALID_ARG;
    nn2_app_seq_internal_t state;
    nn2_app_seq_load(seq, &state);
    nn2_app_seq_observation_t observation = { .state=state.state, .next_seq=state.next_seq,
        .cycles_ok=state.cycles_ok, .faults=state.faults, .off_failures=state.off_failures,
        .last_error=state.last_error };
    *out = observation;
    return NN2_OK;
}

nn2_app_seq_state_t nn2_app_sequencer_state(const nn2_app_sequencer_t *seq)
{
    if (seq == NULL) return NN2_APP_SEQ_FAULT;
    nn2_app_seq_internal_t state;
    nn2_app_seq_load(seq, &state);
    return state.state;
}

const char *nn2_app_seq_state_str(nn2_app_seq_state_t state)
{
    switch (state) {
    case NN2_APP_SEQ_IDLE: return "IDLE";
    case NN2_APP_SEQ_LED_ON: return "LED_ON";
    case NN2_APP_SEQ_SAMPLE: return "SAMPLE";
    case NN2_APP_SEQ_LED_OFF: return "LED_OFF";
    case NN2_APP_SEQ_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

/* Adapter-private control implementation. Included by the ESP translation unit
 * and host RTOS seam; no public ABI or Core/Application SDK dependency. */
static bool dispatch(device_t *d, bool *discard_events)
{
    request_t req; reply_t reply = {0};
    if (xQueueReceive(d->commands, &req, 0u) != pdTRUE) return false;
    *discard_events = false;
    uint32_t discarded;
    reply.id = req.id;
    if (now_us(d) >= req.deadline) reply.result = NN2_ERR_TIMEOUT;
    else switch (req.op) {
    case CMD_INIT:
        reply.result = d->hardware_ready && !d->poisoned && d->watchdog
            ? nn2_ads131_initialize(&d->logic, &d->board.config) : NN2_ERR_IO;
        *discard_events = d->logic.observation.state != NN2_ADC_RUNNING; break;
    case CMD_START:
        /* Only READY can synchronize. Rejected START must not drain live DRDY.
         * A failed sync enters FAULT, so its pre-sync events are also obsolete. */
        if (d->logic.observation.state == NN2_ADC_READY) {
            (void)xTaskNotifyWait(0u, UINT32_MAX, &discarded, 0u);
            *discard_events = true;
        }
        reply.result = nn2_ads131_start(&d->logic);
        if (reply.result == NN2_OK && d->logic.io.take_events) {
            /* V4 startup handoff: service/discard the two fast-start frames
             * in the ADC owner before acknowledging START to the LED caller. */
            uint64_t end = now_us(d) + 20000u;
            if (end > req.deadline) end = req.deadline;
            while (d->logic.settling && d->logic.observation.state == NN2_ADC_RUNNING) {
                if (now_us(d) >= end) {
                    d->logic.observation.drdy_timeouts++;
                    d->logic.observation.loss_unknown++;
                    reply.result = nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_DRDY);
                    break;
                }
                (void)xTaskNotifyWait(0u, UINT32_MAX, &discarded, 1u);
                if (now_us(d) >= end) continue;
                if (nn2_ads131_service(&d->logic, 0u) != NN2_OK) {
                    reply.result = NN2_ERR_IO; break;
                }
            }
        }
        break;
    case CMD_STOP:
        reply.result = nn2_ads131_stop(&d->logic);
        /* STOP can fail into FAULT after reset was attempted. Neither outcome
         * permits continued acquisition; this is not a result==OK decision. */
        *discard_events = d->logic.observation.state != NN2_ADC_RUNNING;
        if (*discard_events) (void)xTaskNotifyWait(0u, UINT32_MAX, &discarded, 0u);
        break;
    case CMD_READ: reply.result = nn2_ads131_pop(&d->logic, &reply.record); break;
    case CMD_OBSERVE: reply.observation = d->logic.observation; reply.result = NN2_OK; break;
    default: reply.result = NN2_ERR_INVALID_ARG; break;
    }
    /* A late reply never retains caller memory; replace only an unconsumed reply. */
    (void)xQueueOverwrite(d->replies, &reply); return true;
}
/* One iteration is shared by the ESP task and deterministic host RTOS mocks. */
static void owner_iteration(device_t *d, uint32_t got)
{
    bool discard_events = false;
    (void)dispatch(d, &discard_events);
    if (d->watchdog && esp_task_wdt_reset() != ESP_OK) {
        d->watchdog = false; (void)nn2_ads131_fail(&d->logic, NN2_ADC_FAULT_WATCHDOG);
    }
    if (!discard_events && d->logic.observation.state == NN2_ADC_RUNNING) {
        if (d->logic.io.take_events) {
            /* Production: the ring is the record of work. Notification bits
             * only wake the owner and must never be treated as sample counts. */
            (void)nn2_ads131_service(&d->logic, 0u);
        } else {
            /* Legacy count-only host seam, without hardware edge timestamps. */
            uint32_t pending = 0u;
            (void)xTaskNotifyWait(0u, UINT32_MAX, &pending, 0u);
            (void)nn2_ads131_service(&d->logic, got + (pending & 0x7fffffffu));
        }
    }
}
static TickType_t wait_ticks(uint64_t remaining_us)
{
    return (TickType_t)((remaining_us * configTICK_RATE_HZ + 999999u) / 1000000u);
}
static nn2_status_t call_until(device_t *d, uint32_t op, uint64_t end, reply_t *out)
{
    request_t req; reply_t reply;
    if (!d || !d->created) return NN2_ERR_INVALID_ARG;
    if (xTaskGetCurrentTaskHandle() != d->caller) return NN2_ERR_INVALID_STATE;
    uint64_t now = now_us(d);
    if (now >= end) return NN2_ERR_TIMEOUT;
    req.id = ++d->next_id; req.op = op; req.deadline = end;
    if (xQueueSend(d->commands, &req, wait_ticks(end - now)) != pdTRUE) return NN2_ERR_TIMEOUT;
    /* A queued request is copied and may still execute after caller timeout. */
    (void)xTaskNotify(d->task, 0x80000000u, eSetBits);
    if (now_us(d) >= end) return NN2_ERR_TIMEOUT;
    for (;;) {
        now = now_us(d);
        if (now >= end) return NN2_ERR_TIMEOUT;
        int received = xQueueReceive(d->replies, &reply, wait_ticks(end - now));
        /* Tick rounding affects scheduling, never the logical deadline. Check
         * before ID/result acceptance, including EMPTY and error replies. */
        if (now_us(d) >= end) return NN2_ERR_TIMEOUT;
        if (received == pdTRUE && reply.id == req.id) {
            if (reply.result == NN2_OK && out) *out = reply;
            return reply.result;
        }
    }
}
static nn2_status_t call(device_t *d, uint32_t op, uint32_t ms, reply_t *out)
{
    if (!d || ms == 0u || ms > 1000u) return NN2_ERR_INVALID_ARG;
    return call_until(d, op, now_us(d) + (uint64_t)ms * 1000u, out);
}
static nn2_status_t init_port(void *ctx, uint32_t ms) { return call(ctx, CMD_INIT, ms, NULL); }
static nn2_status_t start_port(void *ctx, uint32_t ms) { return call(ctx, CMD_START, ms, NULL); }
static nn2_status_t stop_port(void *ctx, uint32_t ms) { return call(ctx, CMD_STOP, ms, NULL); }
static nn2_status_t read_port(void *ctx, nn2_core_adc_record_t *out, uint32_t ms)
{
    reply_t reply;
    device_t *d = ctx;
    if (!out || !d || ms == 0u || ms > 1000u) return NN2_ERR_INVALID_ARG;
    const uint64_t end = now_us(d) + (uint64_t)ms * 1000u;
    for (;;) {
        /* EMPTY means timely EMPTY acknowledgments exhausted the read budget.
         * A command enqueue/reply crossing that budget remains TIMEOUT. */
        if (now_us(d) >= end) return NN2_ERR_EMPTY;
        nn2_status_t status = call_until(d, CMD_READ, end, &reply);
        if (status == NN2_OK) {
            if (now_us(d) >= end) return NN2_ERR_TIMEOUT;
            *out = reply.record; return status;
        }
        if (status != NN2_ERR_EMPTY) return status;
        vTaskDelay(1u); /* Consumer blocks; producer continues on its own core. */
    }
}
static nn2_status_t observe_port(void *ctx, nn2_core_adc_observation_t *out, uint32_t ms)
{
    reply_t reply; device_t *d = ctx;
    if (!out || !d || ms == 0u || ms > 1000u) return NN2_ERR_INVALID_ARG;
    const uint64_t end = now_us(d) + (uint64_t)ms * 1000u;
    nn2_status_t s = call_until(d, CMD_OBSERVE, end, &reply);
    if (s == NN2_OK) {
        if (now_us(d) >= end) return NN2_ERR_TIMEOUT;
        *out = reply.observation;
    }
    return s;
}

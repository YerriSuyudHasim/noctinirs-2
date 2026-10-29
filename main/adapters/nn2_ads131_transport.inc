/* V4 polling/bus-ownership pattern adapted to the existing bounded-wait contract.
 * Shared with host driver mocks. Static transaction storage survives a timeout. */
static void release_bus(device_t *d)
{
    if (d->bus_acquired && !d->poisoned) {
        spi_device_release_bus(d->spi); d->bus_acquired = false;
    }
}
static nn2_status_t transfer(void *ctx, const uint8_t *tx, uint8_t *rx)
{
    device_t *d = ctx;
    bool capture = d->logic.startup_stage != NN2_ADS131_STAGE_NONE && !d->logic.startup_failure.valid;
    if (capture) d->startup_spi_error = ESP_FAIL;
    if (!d->hardware_ready || d->poisoned) return NN2_ERR_IO;
    esp_err_t err;
    if (!d->bus_acquired) {
        /* IDF only accepts portMAX_DELAY here and in polling_start. This bus
         * is dedicated; no other device/task may queue or acquire it. This is
         * not a claim of a bounded lock-acquisition time. */
        err = spi_device_acquire_bus(d->spi, portMAX_DELAY);
        if (capture) d->startup_spi_error = err;
        if (err != ESP_OK) return mapped(d, err);
        d->bus_acquired = true;
    }
    memcpy(d->tx, tx, sizeof(d->tx));
    memset(d->rx, 0, sizeof(d->rx));
    memset(&d->transaction, 0, sizeof(d->transaction));
    d->transaction.length = NN2_ADS131_BYTES * 8u;
    d->transaction.rxlength = NN2_ADS131_BYTES * 8u;
    d->transaction.tx_buffer = d->tx; d->transaction.rx_buffer = d->rx;
    nn2_ads131_trace_t *timing = d->logic.observation.state == NN2_ADC_RUNNING &&
        !d->logic.runtime_failure.valid && d->logic.trace_head
        ? &d->logic.trace[(d->logic.trace_head - 1u) % NN2_ADS131_TRACE_CAPACITY] : NULL;
    if (timing) timing->driver_start_us = now_us(d);
    err = spi_device_polling_start(d->spi, &d->transaction, portMAX_DELAY);
    if (timing) timing->driver_started_us = now_us(d);
    if (capture) d->startup_spi_error = err;
    if (err != ESP_OK) return mapped(d, err);
    err = spi_device_polling_end(d->spi, ticks(NN2_RTOS_ADS131_SPI_WAIT_MS));
    if (capture) d->startup_spi_error = err;
    if (err != ESP_OK) {
        /* Failed polling completion is not cancellation. Retain bus, buffers
         * and descriptor until reboot; never start/release another transfer. */
        d->poisoned = true; return mapped(d, err);
    }
    memcpy(rx, d->rx, sizeof(d->rx)); return mapped(d, ESP_OK);
}

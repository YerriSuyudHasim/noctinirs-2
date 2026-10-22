#include "nn2_app_adc_diagnostic.h"
nn2_status_t nn2_app_adc_diagnostic_run(const nn2_port_adc_stream_t *p,
                                      uint32_t attempts, nn2_core_adc_observation_t *out, nn2_core_adc_record_t *last)
{
    nn2_core_adc_observation_t observation;
    nn2_core_adc_record_t record;
    nn2_status_t status, stopped;
    bool received = false;
    if (!p || !p->init || !p->start || !p->stop || !p->read || !p->observe ||
        !out || !last || attempts == 0u || attempts > 1000u) return NN2_ERR_INVALID_ARG;
    status = p->init(p->ctx, 1000u);
    if (status == NN2_OK) status = p->start(p->ctx, 1000u);
    for (uint32_t i = 0u; status == NN2_OK && i < attempts; ++i) {
        status = p->read(p->ctx, &record, 1000u);
        if (status == NN2_OK) received = true;
        if (status == NN2_ERR_EMPTY) status = NN2_OK;
    }
    stopped = p->stop(p->ctx, 1000u);
    if (status == NN2_OK) status = stopped;
    if (status == NN2_OK) status = p->observe(p->ctx, &observation, 1000u);
    if (status == NN2_OK && observation.fault != NN2_ADC_FAULT_NONE) status = NN2_ERR_IO;
    if (status == NN2_OK && !received) status = NN2_ERR_EMPTY;
    if (status == NN2_OK) { *out = observation; *last = record; }
    return status;
}

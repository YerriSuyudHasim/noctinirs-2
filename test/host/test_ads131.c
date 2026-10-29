#include "nn2_test.h"
#include "nn2_ads131_logic.h"
#include "nn2_ads131_events.h"
#include "nn2_app_adc_diagnostic.h"
#include <string.h>
#include "ads131_golden.h"
/* Independent polynomial long division oracle for dynamic register replies.
 * Complete sample known-answer vectors above do not use this or production CRC. */
static uint16_t oracle_crc(const uint8_t *p)
{
    uint32_t remainder = 0xffffu;
    for (unsigned i=0; i<120u; ++i) {
        unsigned bit = (p[i/8u] >> (7u-i%8u)) & 1u;
        remainder = (remainder << 1) ^ (bit << 16);
        if (remainder & 0x10000u) remainder ^= 0x11021u;
    }
    return (uint16_t)remainder;
}
typedef struct {
    uint16_t regs[29], pending;
    uint64_t time;
    unsigned calls, fail_at, mismatch, reset_calls, sync_calls;
    uint32_t reset_low_us, ready_ms;
    bool high, ready_failure, reset_failure, corrupt, bad_ack;
    unsigned corrupt_at, padding_at, crc_padding_at;
    bool fail_cleanup;
    nn2_status_t transfer_failure;
} fake_t;
static fake_t fake;
static uint64_t latched_edge;
static uint32_t latched_serial;
static bool edge_during_transfer;
static uint32_t transfer_elapsed_us = 18u;
static nn2_ads131_logic_t dev;
static nn2_ads131_events_t event_ring;
static bool edge_during_take;
static const nn2_ads131_config_t config = {8192000u,512u,8000u,8000000u};
static nn2_status_t fake_transfer(void *ctx, const uint8_t *tx, uint8_t *rx)
{
    fake_t *f=ctx; uint16_t cmd=(uint16_t)(((uint16_t)tx[0]<<8)|tx[1]);
    f->calls++;
    if (f->fail_at == f->calls) return f->transfer_failure ? f->transfer_failure : NN2_ERR_IO;
    memcpy(rx,golden[0],18u); rx[0]=(uint8_t)(f->pending>>8); rx[1]=(uint8_t)f->pending;
    f->pending=0x010fu;
    unsigned address=(cmd>>7)&0x3fu;
    if ((cmd&0xe000u)==0xa000u && address<29u) {
        f->pending=f->regs[address];
        if (f->mismatch==address) f->pending^=1u;
    }
    if ((cmd&0xe000u)==0x6000u && address<29u) {
        f->regs[address]=(uint16_t)(((uint16_t)tx[3]<<8)|tx[4]);
        f->pending=(uint16_t)(0x4000u|(address<<7));
        if (f->bad_ack) f->pending^=1u;
    }
    if (f->padding_at == f->calls) rx[2]=1u;
    uint16_t crc=oracle_crc(rx);rx[15]=(uint8_t)(crc>>8);rx[16]=(uint8_t)crc;
    if (f->corrupt || f->corrupt_at == f->calls) rx[3]^=1u;
    if (f->crc_padding_at == f->calls) rx[17]=1u;
    if (edge_during_transfer) ++latched_serial;
    if (edge_during_transfer && dev.io.take_events) nn2_ads131_events_push(&event_ring,f->time+(transfer_elapsed_us > 18u ? 59u : 5u));
    f->time+=transfer_elapsed_us; return NN2_OK;
}
static nn2_status_t fake_reset(void *ctx,bool high)
{
    fake_t *f=ctx; f->reset_calls++;f->high=high;
    if (!high && dev.io.take_events) { event_ring.enabled=false; nn2_ads131_events_clear(&event_ring); }
    return f->reset_failure || (f->fail_cleanup && f->calls) ? NN2_ERR_IO : NN2_OK;
}
static nn2_status_t fake_sync(void *ctx) { fake_t *f=ctx;f->sync_calls++;return NN2_OK; }
static void fake_delay(void *ctx,uint32_t us)
{
    fake_t *f=ctx;f->time+=us;
    if (!f->high) {
        f->reset_low_us=us; f->regs[0]=0x24abu;f->pending=0xff24u;
        for (unsigned ch=0u;ch<4u;++ch) f->regs[12u+5u*ch]=0x8000u;
    }
}
static nn2_status_t fake_ready(void *ctx,uint32_t ms)
{
    fake_t *f=ctx; f->ready_ms=ms; return f->ready_failure?NN2_ERR_TIMEOUT:NN2_OK;
}
static uint64_t fake_now(void *ctx) { return ((fake_t*)ctx)->time; }
static void bind_fake(void)
{
    memset(&fake,0,sizeof(fake));fake.mismatch=99u; transfer_elapsed_us=18u; edge_during_transfer=false;
    nn2_ads131_io_t io={&fake,fake_transfer,fake_reset,fake_sync,fake_delay,fake_ready,fake_now,NULL,NULL,NULL};
    nn2_ads131_logic_bind(&dev,io);
}
/* User-supplied boot 38 / image 16699b46... capture, 2026-10-29 KST.
 * The aligned counterpart is an ANALYTICAL hypothesis, not a second capture.
 * No shift/correction is performed in the production path or this replay. */
static const uint8_t boot38_rx[18] = {
    0x7f,0x92,0,0,0,0,0,0,0,0,0,0,0,0,0,0x6e,0x7f,0
};
static const uint8_t boot38_aligned_hypothesis[18] = {
    0xff,0x24,0,0,0,0,0,0,0,0,0,0,0,0,0,0xdc,0xfe,0
};
static const uint8_t *startup_replay;
static nn2_status_t replay_transfer(void *ctx,const uint8_t *tx,uint8_t *rx)
{
    nn2_status_t result=fake_transfer(ctx,tx,rx);
    if (result==NN2_OK && fake.calls==1u) memcpy(rx,startup_replay,18u);
    return result;
}
static void captured_boot38_startup_regression(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x8fcdu,oracle_crc(boot38_rx));
    TEST_ASSERT_EQUAL_HEX16(0xdcfeu,oracle_crc(boot38_aligned_hypothesis));
    bind_fake(); startup_replay=boot38_rx; dev.io.transfer=replay_transfer;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(1u,fake.calls); TEST_ASSERT_FALSE(fake.high);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_CRC,dev.observation.fault);
    TEST_ASSERT_EQUAL(NN2_ADS131_STAGE_RESET_ACK,dev.startup_failure.stage);
    TEST_ASSERT_EQUAL_HEX16(0u,dev.startup_failure.command);
    TEST_ASSERT_EQUAL_HEX16(0x8fcdu,dev.startup_failure.crc_calculated);
    TEST_ASSERT_EQUAL_HEX16(0x6e7fu,dev.startup_failure.crc_received);
    TEST_ASSERT_EQUAL_MEMORY(boot38_rx,dev.startup_failure.rx,18u);
    TEST_ASSERT_EQUAL(0u,dev.startup_failure.response_padding);
    TEST_ASSERT_EQUAL(0u,dev.startup_failure.crc_padding);
    bind_fake(); startup_replay=boot38_aligned_hypothesis; dev.io.transfer=replay_transfer;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(68u,fake.calls); TEST_ASSERT_FALSE(dev.startup_failure.valid);
    /* A later corrupt frame still fails; valid first response is no waiver. */
    bind_fake(); startup_replay=boot38_aligned_hypothesis; dev.io.transfer=replay_transfer;
    fake.corrupt_at=2u;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(2u,dev.startup_failure.transaction);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_CRC,dev.startup_failure.fault);
}
static void running(void)
{
    bind_fake(); TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_start(&dev));
    for (unsigned i=0u;i<3u;++i) TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1u));
}
static uint64_t event_latch(void *ctx,uint32_t *serial)
{ (void)ctx; *serial=event_ring.serial; return event_ring.latest_us; }
static uint32_t event_take(void *ctx,nn2_ads131_event_t *event,uint32_t *dropped)
{
    (void)ctx;
    if (edge_during_take) { edge_during_take=false; nn2_ads131_events_push(&event_ring,++fake.time); }
    return nn2_ads131_events_take(&event_ring,event,dropped);
}
static void event_running(void)
{
    running(); memset(&event_ring,0,sizeof(event_ring)); event_ring.enabled=true; edge_during_take=false;
    dev.io.drdy_latch=event_latch; dev.io.take_events=event_take;
}
static void recorded_events_are_work_not_notification_counts(void)
{
    event_running(); fake.time+=125u;
    nn2_ads131_events_push(&event_ring,fake.time);
    /* Any wakeup count, including coalesced control/DRDY bits, cannot create
     * extra events. The three settling frames are already drained. */
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0xffffffffu));
    TEST_ASSERT_EQUAL(1u,dev.count); TEST_ASSERT_EQUAL(0u,dev.observation.lost);
    TEST_ASSERT_EQUAL_UINT64(event_ring.latest_us,dev.items[0].drdy_us);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,77u));
    TEST_ASSERT_EQUAL(1u,dev.count);
    fake.time+=125u; edge_during_take=true;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(2u,dev.count); /* ISR arriving during snapshot is timely. */
}
static void boot42_backlog_and_runtime_snapshot(void)
{
    event_running();
    for (unsigned i=0;i<26u;++i) { fake.time+=125u; nn2_ads131_events_push(&event_ring,fake.time); }
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_GAP,dev.observation.fault);
    TEST_ASSERT_EQUAL(26u,dev.observation.lost); TEST_ASSERT_EQUAL(25u,dev.observation.adc_missed);
    TEST_ASSERT_EQUAL(1u,dev.observation.loss_unknown); TEST_ASSERT_EQUAL(0u,dev.observation.crc_bad);
    TEST_ASSERT_TRUE(dev.runtime_failure.valid); TEST_ASSERT_EQUAL(26u,dev.runtime_failure.pending);
    TEST_ASSERT_EQUAL(26u,dev.runtime_failure.observed_serial);
    TEST_ASSERT_EQUAL(0u,dev.runtime_failure.dropped); TEST_ASSERT_FALSE(event_ring.enabled);
    nn2_ads131_runtime_failure_t first=dev.runtime_failure;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_stop(&dev));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL_MEMORY(&first,&dev.runtime_failure,sizeof(first));
}
static void recorded_event_stale_and_transfer_crossing(void)
{
    event_running(); fake.time+=125u; nn2_ads131_events_push(&event_ring,fake.time); fake.time+=250u;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_FALSE(dev.runtime_failure.valid); /* Age alone is not V4 loss evidence. */
    event_running(); fake.time+=125u; nn2_ads131_events_push(&event_ring,fake.time);
    edge_during_transfer=true;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    edge_during_transfer=false;
    TEST_ASSERT_FALSE(dev.runtime_failure.valid);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(0u,dev.observation.lost); TEST_ASSERT_EQUAL(2u,dev.count);
    TEST_ASSERT_EQUAL(1u,dev.items[0].sequence-3u);
    TEST_ASSERT_EQUAL(2u,dev.items[1].sequence-3u);
}
/* Boot 49 timing replay: first read finishes at edge+169 us, with a
 * following DRDY at edge+125 us. Host timing is a fixture, not board evidence. */
static void boot49_first_settling_read_allows_one_following_event(void)
{
    event_running(); dev.settling=3u; dev.expected_resync=1u;
    fake.time+=125u;
    nn2_ads131_events_push(&event_ring,fake.time);
    fake.time+=66u; transfer_elapsed_us=103u; edge_during_transfer=true;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    edge_during_transfer=false; transfer_elapsed_us=18u;
    TEST_ASSERT_EQUAL(2u,dev.settling);
    TEST_ASSERT_EQUAL(103u,dev.runtime_failure.spi_elapsed_us);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(1u,dev.settling);
    fake.time+=125u; nn2_ads131_events_push(&event_ring,fake.time);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(0u,dev.settling);
    TEST_ASSERT_EQUAL(0u,dev.count);
    TEST_ASSERT_EQUAL(0u,dev.observation.lost);
    TEST_ASSERT_FALSE(dev.runtime_failure.valid);
}
static void settling_resync_is_bounded_and_never_published(void)
{
    event_running(); dev.settling=3u; dev.expected_resync=1u;
    fake.time+=125u; nn2_ads131_events_push(&event_ring,fake.time);
    fake.pending=0x410fu;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(0u,dev.expected_resync); TEST_ASSERT_EQUAL(2u,dev.settling);
    TEST_ASSERT_EQUAL(0u,dev.count);
    fake.time+=125u; nn2_ads131_events_push(&event_ring,fake.time);
    fake.pending=0x410fu;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_READBACK,dev.observation.fault);
    TEST_ASSERT_EQUAL(0u,dev.count);
}
/* No host compiler is invoked by this patch; these are regression sources. */
static void qualification_discards_samples_and_requires_real_rate(void)
{
    for (unsigned slow=0u; slow<2u; ++slow) {
        event_running(); nn2_ads131_rate_begin(&dev);
        for (unsigned i=0u;i<1024u;++i) {
            fake.time+=slow ? 150u : 107u; /* transfer itself adds 18 us */
            nn2_ads131_events_push(&event_ring,fake.time);
            nn2_status_t result=nn2_ads131_service(&dev,0u);
            TEST_ASSERT_EQUAL(slow && i==1023u ? NN2_ERR_IO : NN2_OK,result);
        }
        TEST_ASSERT_EQUAL(1024u,dev.rate.samples);
        TEST_ASSERT_EQUAL(0u,dev.count);
        TEST_ASSERT_EQUAL(!slow,dev.rate.valid);
        if (!slow) TEST_ASSERT_EQUAL(8000u,dev.rate.hz);
        else TEST_ASSERT_EQUAL(NN2_ADS131_RUNTIME_RATE,dev.runtime_failure.detail);
    }
}
static void fault_history_preserves_previous_and_rejected_read(void)
{
    event_running();
    for (unsigned i=0u;i<10u;++i) {
        fake.time+=107u; nn2_ads131_events_push(&event_ring,fake.time);
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    }
    fake.time+=230u; nn2_ads131_events_push(&event_ring,fake.time); fake.time+=31u;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(1u,dev.observation.loss_unknown);
    TEST_ASSERT_TRUE((dev.items[(dev.head+dev.count-1u)%NN2_ADS131_CAPACITY].quality & NN2_ADC_QUALITY_GAP)!=0u);
    fake.time+=107u; nn2_ads131_events_push(&event_ring,fake.time); fake.corrupt=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_CRC,dev.observation.fault);
    const nn2_ads131_trace_t *last=&dev.trace[(dev.trace_head-1u)%8u];
    TEST_ASSERT_EQUAL(2u,last->phase);
    nn2_ads131_trace_t frozen[8]; memcpy(frozen,dev.trace,sizeof(frozen));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_stop(&dev));
    TEST_ASSERT_EQUAL_MEMORY(frozen,dev.trace,sizeof(frozen));
}
static void first_unknown_distinguishes_causes_and_survives_stop(void)
{
    for (unsigned reasons=1u; reasons<=3u; ++reasons) {
        event_running(); fake.time+=125u;
        nn2_ads131_events_push(&event_ring,fake.time);
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
        uint64_t previous_edge=event_ring.latest_us;
        fake.time+=(reasons & NN2_ADS131_UNKNOWN_PERIOD) ? 230u : 107u;
        nn2_ads131_events_push(&event_ring,fake.time);
        if (reasons & NN2_ADS131_UNKNOWN_FRESHNESS) fake.pending=0x0107u;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
        TEST_ASSERT_EQUAL(1u,dev.observation.loss_unknown);
        TEST_ASSERT_TRUE(dev.first_unknown.valid);
        TEST_ASSERT_TRUE(dev.first_unknown.previous_valid);
        TEST_ASSERT_EQUAL(reasons,dev.first_unknown.reasons);
        TEST_ASSERT_EQUAL_UINT64(previous_edge,dev.first_unknown.previous.edge_us);
        TEST_ASSERT_EQUAL_UINT64(event_ring.latest_us,dev.first_unknown.current.edge_us);
        TEST_ASSERT_EQUAL((reasons & NN2_ADS131_UNKNOWN_FRESHNESS) ? 0x0107u : 0x010fu,
                          dev.first_unknown.current.status);
        nn2_ads131_unknown_t frozen=dev.first_unknown;
        fake.time+=230u; nn2_ads131_events_push(&event_ring,fake.time);
        fake.pending=0x0107u;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
        TEST_ASSERT_EQUAL_MEMORY(&frozen,&dev.first_unknown,sizeof(frozen));
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_stop(&dev));
        TEST_ASSERT_EQUAL_MEMORY(&frozen,&dev.first_unknown,sizeof(frozen));
    }
}
static void latency_capture_is_bounded_and_does_not_change_quality(void)
{
    event_running();
    for (unsigned i=0u; i<6u; ++i) {
        fake.time+=7u; nn2_ads131_events_push(&event_ring,fake.time);
        fake.time+=110u; dev.owner_resume_us=fake.time;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    }
    TEST_ASSERT_EQUAL(6u,dev.latency_seen);
    TEST_ASSERT_EQUAL(4u,dev.latency_count);
    TEST_ASSERT_EQUAL(NN2_ADS131_LATENCY_START_LATE,dev.latency[0].reasons);
    TEST_ASSERT_EQUAL_UINT64(dev.latency[0].current.edge_us+110u,
                             dev.latency[0].current.service_entry_us);
    TEST_ASSERT_EQUAL_UINT64(dev.latency[0].current.service_entry_us,
                             dev.latency[0].current.event_pop_us);
    TEST_ASSERT_EQUAL(0u,dev.observation.loss_unknown);
    TEST_ASSERT_EQUAL(0u,dev.quality);
    nn2_ads131_latency_event_t first=dev.latency[0];
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_stop(&dev));
    TEST_ASSERT_EQUAL_MEMORY(&first,&dev.latency[0],sizeof(first));
}
static void two_events_read_in_order_three_events_fault(void)
{
    event_running(); fake.time+=125u;
    uint64_t first=fake.time;
    nn2_ads131_events_push(&event_ring,fake.time); fake.time+=125u;
    nn2_ads131_events_push(&event_ring,fake.time);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(2u,dev.count);
    TEST_ASSERT_EQUAL_UINT64(first,dev.items[0].drdy_us);
    TEST_ASSERT_EQUAL_UINT64(first+125u,dev.items[1].drdy_us);
    event_running();
    for (unsigned i=0u;i<3u;++i) {
        fake.time+=125u; nn2_ads131_events_push(&event_ring,fake.time);
    }
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(NN2_ADS131_RUNTIME_BACKLOG,dev.runtime_failure.detail);
    TEST_ASSERT_EQUAL(0u,dev.count);
}
static void event_ring_gating_clear_and_wrap(void)
{
    nn2_ads131_events_t q={0}; nn2_ads131_event_t event; uint32_t dropped;
    nn2_ads131_events_push(&q,100u); TEST_ASSERT_EQUAL(0u,q.serial);
    q.enabled=true; q.head=q.tail=UINT32_MAX-1u; q.serial=UINT32_MAX-1u;
    nn2_ads131_events_push(&q,100u); nn2_ads131_events_push(&q,225u); nn2_ads131_events_push(&q,350u);
    TEST_ASSERT_EQUAL(3u,nn2_ads131_events_take(&q,&event,&dropped));
    TEST_ASSERT_EQUAL(UINT32_MAX,event.serial); TEST_ASSERT_EQUAL_UINT64(100u,event.time_us);
    TEST_ASSERT_EQUAL(1u,q.serial); TEST_ASSERT_EQUAL(0u,dropped);
    TEST_ASSERT_EQUAL(2u,nn2_ads131_events_take(&q,&event,&dropped));
    TEST_ASSERT_EQUAL(0u,event.serial);
    TEST_ASSERT_EQUAL(1u,nn2_ads131_events_take(&q,&event,&dropped));
    TEST_ASSERT_EQUAL(1u,event.serial);
    TEST_ASSERT_EQUAL(0u,nn2_ads131_events_take(&q,&event,&dropped));
    nn2_ads131_events_push(&q,475u); nn2_ads131_events_clear(&q);
    TEST_ASSERT_EQUAL(2u,q.serial); TEST_ASSERT_EQUAL(0u,nn2_ads131_events_take(&q,&event,&dropped));
}
static void complete_vectors(void)
{
    const int32_t extremes[]={0,8388607,-8388608,-1};
    for(unsigned rotation=0;rotation<4u;++rotation) {
        nn2_core_adc_record_t frame;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_parse(golden[rotation],123u,&frame));
        for(unsigned ch=0;ch<4u;++ch) TEST_ASSERT_EQUAL_INT32(extremes[(ch+rotation)%4u],frame.channel[ch]);
        TEST_ASSERT_EQUAL_UINT64(123u,frame.read_complete_us);
        TEST_ASSERT_EQUAL(NN2_SAMPLE_SRC_ADC,frame.source);
    }
}
static void corruption_output_validity(void)
{
    uint8_t raw[18];nn2_core_adc_record_t out,before;
    memset(&before,0xa5,sizeof(before));
    for(unsigned byte=0;byte<18u;++byte) {
        memcpy(raw,golden[0],18u);raw[byte]^=1u;out=before;
        TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_parse(raw,1u,&out));
        TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
    }
    memcpy(raw,golden[0],18u);raw[17]=1u;out=before;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_parse(raw,1u,&out));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_ads131_parse(NULL,0u,&out));
}
static void signed_and_crc_answers(void)
{
    static const uint8_t check[]="123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29b1u,nn2_ads131_crc(check,9u));
    TEST_ASSERT_EQUAL_INT32(-8388608,nn2_ads131_sext24(0x800000u));
    TEST_ASSERT_EQUAL_INT32(-1,nn2_ads131_sext24(0xffffffffu));
}
static void tuple_rejection(void)
{
    nn2_ads131_config_t c=config;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_validate(&c));
    c.osr=1024u;TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_ads131_validate(&c));
    c=config;c.clkin_hz=4096000u;TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_ads131_validate(&c));
    c=config;c.rate_hz=4000u;TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_ads131_validate(&c));
    c=config;c.spi_hz=8000001u;TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_ads131_validate(&c));
}
static void initialize_pipeline_readback(void)
{
    bind_fake();TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL_HEX16(0x0111u,fake.regs[2]);TEST_ASSERT_EQUAL_HEX16(0x0f0au,fake.regs[3]);
    TEST_ASSERT_EQUAL_HEX16(0u,fake.regs[4]);TEST_ASSERT_EQUAL(68u,fake.calls);
    TEST_ASSERT_EQUAL_HEX16(0x0600u,fake.regs[6]);
    for (unsigned ch=0u;ch<4u;++ch) TEST_ASSERT_EQUAL_HEX16(0u,fake.regs[9u+5u*ch]);
    TEST_ASSERT_TRUE(fake.reset_low_us>250u);TEST_ASSERT_EQUAL(20u,fake.ready_ms);
    TEST_ASSERT_EQUAL(NN2_ADC_READY,dev.observation.state);
    TEST_ASSERT_FALSE(dev.startup_failure.valid);
}
/* Synthetic injections, not a replay of the reported board fault. Exercise
 * both halves of RREG/WREG so the NULL response retains its register address. */
static void startup_first_failure_snapshot(void)
{
    static const uint16_t commands[68] = {0x0u,0x655u,0xa000u,0x0u,0x6100u,0x0u,0xa100u,0x0u,0x6180u,0x0u,0xa180u,0x0u,0x6200u,0x0u,0xa200u,0x0u,0x6300u,0x0u,0xa300u,0x0u,0x6480u,0x0u,0xa480u,0x0u,0x6700u,0x0u,0xa700u,0x0u,0x6980u,0x0u,0xa980u,0x0u,0x6c00u,0x0u,0xac00u,0x0u,0xa500u,0x0u,0xa580u,0x0u,0xa600u,0x0u,0xa680u,0x0u,0xa780u,0x0u,0xa800u,0x0u,0xa880u,0x0u,0xa900u,0x0u,0xaa00u,0x0u,0xaa80u,0x0u,0xab00u,0x0u,0xab80u,0x0u,0xac80u,0x0u,0xad00u,0x0u,0xad80u,0x0u,0xae00u,0x0u};
    static const uint8_t addresses[68] = {255,255,0,0,2,2,2,2,3,3,3,3,4,4,4,4,6,6,6,6,9,9,9,9,14,14,14,14,19,19,19,19,24,24,24,24,10,10,11,11,12,12,13,13,15,15,16,16,17,17,18,18,20,20,21,21,22,22,23,23,25,25,26,26,27,27,28,28};
    static const nn2_ads131_stage_t stages[68] = {
        NN2_ADS131_STAGE_RESET_ACK,
        NN2_ADS131_STAGE_UNLOCK,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_WREG,
        NN2_ADS131_STAGE_WREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE,
        NN2_ADS131_STAGE_RREG,
        NN2_ADS131_STAGE_RREG_RESPONSE
    };
    for (unsigned at=1u;at<=68u;++at) {
        bind_fake(); fake.corrupt_at=at;
        TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
        const nn2_ads131_startup_failure_t *f=&dev.startup_failure;
        TEST_ASSERT_TRUE(f->valid); TEST_ASSERT_TRUE(f->tx_valid); TEST_ASSERT_TRUE(f->rx_valid);
        TEST_ASSERT_EQUAL(at,f->transaction); TEST_ASSERT_EQUAL(stages[at-1u],f->stage);
        TEST_ASSERT_EQUAL(commands[at-1u],f->command);
        TEST_ASSERT_EQUAL(at>1u?commands[at-2u]:0u,f->previous_command);
        TEST_ASSERT_EQUAL(addresses[at-1u],f->address);
        uint8_t expected_tx[18]={0};
        expected_tx[0]=(uint8_t)(commands[at-1u]>>8);
        expected_tx[1]=(uint8_t)commands[at-1u];
        if (at==5u) { expected_tx[3]=0x01u; expected_tx[4]=0x11u; }
        if (at==9u) { expected_tx[3]=0x0fu; expected_tx[4]=0x0au; }
        if (at==17u) expected_tx[3]=0x06u;
        TEST_ASSERT_EQUAL_MEMORY(expected_tx,f->tx,sizeof(expected_tx));
        if (at==1u) { TEST_ASSERT_EQUAL(0xffu,f->rx[0]); TEST_ASSERT_EQUAL(0x24u,f->rx[1]); }
        TEST_ASSERT_EQUAL(NN2_ADC_FAULT_CRC,f->fault); TEST_ASSERT_FALSE(f->crc_match);
        TEST_ASSERT_EQUAL(0u,f->response_padding); TEST_ASSERT_EQUAL(0u,f->crc_padding);
        TEST_ASSERT_EQUAL(oracle_crc(f->rx),f->crc_calculated);
        TEST_ASSERT_EQUAL((uint16_t)(((uint16_t)f->rx[15]<<8)|f->rx[16]),f->crc_received);
        nn2_ads131_startup_failure_t before=*f;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_stop(&dev));
        fake.corrupt_at=0u;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config));
        TEST_ASSERT_EQUAL_MEMORY(&before,f,sizeof(before));
        fake.corrupt_at=fake.calls+1u;
        TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
        TEST_ASSERT_EQUAL_MEMORY(&before,f,sizeof(before));
    }
}
static void startup_padding_and_timeout_diagnostics(void)
{
    for (unsigned pad=0;pad<2u;++pad) {
        bind_fake();
        if (pad) fake.crc_padding_at=2u; else fake.padding_at=2u;
        TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
        TEST_ASSERT_TRUE(dev.startup_failure.crc_match);
        TEST_ASSERT_EQUAL(pad?0u:1u,dev.startup_failure.response_padding);
        TEST_ASSERT_EQUAL(pad?1u:0u,dev.startup_failure.crc_padding);
    }
    bind_fake(); fake.fail_at=2u; fake.transfer_failure=NN2_ERR_TIMEOUT; fake.fail_cleanup=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_TRUE(dev.startup_failure.valid); TEST_ASSERT_FALSE(dev.startup_failure.rx_valid);
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,dev.startup_failure.result);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_SPI,dev.startup_failure.fault);
    TEST_ASSERT_EQUAL(NN2_ERR_IO,dev.startup_failure.cleanup_result);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_RESET,dev.observation.fault);
    static const uint8_t unavailable[18]={0};
    TEST_ASSERT_EQUAL_MEMORY(unavailable,dev.startup_failure.rx,18u);
    bind_fake(); fake.ready_failure=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(NN2_ADS131_STAGE_READY,dev.startup_failure.stage);
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,dev.startup_failure.result);
    TEST_ASSERT_FALSE(dev.startup_failure.rx_valid);
    running(); fake.corrupt=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_FALSE(dev.startup_failure.valid); /* Runtime still fails closed. */
}
static void mismatch_rollback(void)
{
    for(unsigned address=2u;address<=4u;++address) {
        bind_fake();fake.mismatch=address;
        TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
        TEST_ASSERT_EQUAL(NN2_ADC_FAULT_READBACK,dev.observation.fault);TEST_ASSERT_FALSE(fake.high);
    }
    bind_fake();fake.bad_ack=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));TEST_ASSERT_FALSE(fake.high);
}
static nn2_status_t wrong_id_transfer(void *ctx,const uint8_t *tx,uint8_t *rx)
{
    fake.regs[0]=0x2500u;return fake_transfer(ctx,tx,rx);
}
static void masked_id(void)
{
    bind_fake();TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config)); /* 24AB accepted */
    bind_fake();dev.io.transfer=wrong_id_transfer;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_ID,dev.observation.fault);TEST_ASSERT_FALSE(fake.high);
}
static void each_spi_failure_rolls_back(void)
{
    for(unsigned at=1u;at<=68u;++at) {
        bind_fake();fake.fail_at=at;
        TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
        TEST_ASSERT_EQUAL(1u,dev.observation.spi_errors);TEST_ASSERT_FALSE(fake.high);
    }
}
static void ready_reset_timeout(void)
{
    bind_fake();fake.ready_failure=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(1u,dev.observation.drdy_timeouts);TEST_ASSERT_FALSE(fake.high);
    bind_fake();fake.reset_failure=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_RESET,dev.observation.fault);
}
static void repeated_lifecycle(void)
{
    running();TEST_ASSERT_EQUAL(NN2_ERR_INVALID_STATE,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_stop(&dev));TEST_ASSERT_FALSE(fake.high);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_stop(&dev));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_STATE,nn2_ads131_start(&dev));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(2u,dev.observation.session);TEST_ASSERT_EQUAL(1u,dev.observation.lost);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_start(&dev));TEST_ASSERT_EQUAL(2u,fake.sync_calls);
}
static void overflow_faults_without_overwrite(void)
{
    running();
    for (unsigned i=0u;i<128u;++i) TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(128u,dev.count); TEST_ASSERT_EQUAL(1u,dev.observation.overflow);
    TEST_ASSERT_EQUAL(1u,dev.observation.lost);
    TEST_ASSERT_EQUAL(4u,dev.items[dev.head].sequence);
    TEST_ASSERT_EQUAL(NN2_ADS131_RUNTIME_RAW_OVERFLOW,dev.runtime_failure.detail);
    TEST_ASSERT_FALSE(fake.high);
}
static nn2_status_t full_publisher(void *ctx, const nn2_core_adc_record_t *record, uint32_t *depth)
{ (void)ctx; (void)record; *depth=256u; return NN2_ERR_IO; }
static void production_ring_full_faults_before_silent_loss(void)
{
    running(); dev.io.publish=full_publisher;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(1u,dev.observation.overflow);
    TEST_ASSERT_EQUAL(NN2_ADS131_RUNTIME_RAW_OVERFLOW,dev.runtime_failure.detail);
}
static void stale_notifications(void)
{
    running();unsigned calls=fake.calls;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,4u));
    TEST_ASSERT_EQUAL(calls,fake.calls);TEST_ASSERT_EQUAL(3u,dev.observation.adc_missed);
    TEST_ASSERT_EQUAL(4u,dev.observation.lost);TEST_ASSERT_EQUAL(1u,dev.observation.loss_unknown);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_GAP,dev.observation.fault);TEST_ASSERT_FALSE(fake.high);
}
static void service_gap_and_stall(void)
{
    running();fake.time+=251u;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_GAP,dev.observation.fault);
    running();fake.time+=30000u;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,0u));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_DRDY,dev.observation.fault);TEST_ASSERT_FALSE(fake.high);
}
static void acquisition_integrity_faults(void)
{
    running();fake.corrupt=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(1u,dev.observation.crc_bad);TEST_ASSERT_EQUAL(0u,dev.count);
    running();fake.fail_at=fake.calls+1u;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(1u,dev.observation.spi_errors);TEST_ASSERT_FALSE(fake.high);
}
static void wrap_and_output(void)
{
    running();dev.observation.sequence=0xffffffffu;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1u));
    nn2_core_adc_record_t out,before;TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_pop(&dev,&out));
    TEST_ASSERT_EQUAL(0u,out.sequence);before=out;
    TEST_ASSERT_EQUAL(NN2_ERR_EMPTY,nn2_ads131_pop(&dev,&out));TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
}
static unsigned app_stop_count,app_reads;static nn2_status_t app_failure;static bool app_observed_fault;
static nn2_status_t app_init(void *ctx,uint32_t ms) {(void)ctx;(void)ms;return app_failure;}
static nn2_status_t app_start(void *ctx,uint32_t ms) {(void)ctx;(void)ms;return NN2_OK;}
static nn2_status_t app_stop(void *ctx,uint32_t ms) {(void)ctx;(void)ms;app_stop_count++;return NN2_OK;}
static nn2_status_t app_read(void *ctx,nn2_core_adc_record_t *out,uint32_t ms)
{(void)ctx;(void)ms;app_reads++;memset(out,0,sizeof(*out));return NN2_OK;}
static nn2_status_t app_observe(void *ctx,nn2_core_adc_observation_t *out,uint32_t ms)
{(void)ctx;(void)ms;memset(out,0,sizeof(*out));if(app_observed_fault)out->fault=NN2_ADC_FAULT_GAP;return NN2_OK;}
static void diagnostic_fake_profile(void)
{
    nn2_port_adc_stream_t p={NULL,app_init,app_start,app_stop,app_read,app_observe};
    nn2_core_adc_observation_t out;nn2_core_adc_record_t record;
    app_stop_count=0u;app_reads=0u;app_failure=NN2_OK;app_observed_fault=false;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_adc_diagnostic_run(&p,8u,&out,&record));
    TEST_ASSERT_EQUAL(1u,app_stop_count);TEST_ASSERT_EQUAL(8u,app_reads);
    app_failure=NN2_ERR_IO;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_app_adc_diagnostic_run(&p,8u,&out,&record));
    TEST_ASSERT_EQUAL(2u,app_stop_count);TEST_ASSERT_EQUAL(8u,app_reads);
}
static void diagnostic_retained_fault_is_not_success(void)
{
    nn2_port_adc_stream_t p={NULL,app_init,app_start,app_stop,app_read,app_observe};
    nn2_core_adc_observation_t out,before;nn2_core_adc_record_t record;
    memset(&before,0xa5,sizeof(before));out=before;
    app_failure=NN2_OK;app_observed_fault=true;app_stop_count=0u;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_app_adc_diagnostic_run(&p,8u,&out,&record));
    TEST_ASSERT_EQUAL(1u,app_stop_count);
    TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
    app_observed_fault=false;
}
static uint64_t fake_latch(void *ctx,uint32_t *serial)
{ (void)ctx; *serial=latched_serial; return latched_edge; }
static void drdy_timestamp_and_crossing(void)
{
    running(); dev.io.drdy_latch=fake_latch; dev.edge_serial=0;
    latched_edge=fake.time;latched_serial=1;edge_during_transfer=false;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1));
    nn2_core_adc_record_t r;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_pop(&dev,&r));
    TEST_ASSERT_EQUAL_UINT64(latched_edge,r.drdy_us);
    TEST_ASSERT_EQUAL_UINT64(latched_edge+18,r.read_complete_us);
    TEST_ASSERT_EQUAL(0,r.quality);
    fake.time=latched_edge+125;latched_edge=fake.time;latched_serial=2;
    edge_during_transfer=true;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1));
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_GAP,dev.observation.fault);
    TEST_ASSERT_EQUAL(0,dev.count);
    edge_during_transfer=false;
}
static void stale_drdy_rejected(void)
{
    running();dev.io.drdy_latch=fake_latch;dev.edge_serial=0;
    latched_edge=fake.time;latched_serial=1;fake.time+=125;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_ads131_service(&dev,1));
    TEST_ASSERT_EQUAL(1,dev.observation.loss_unknown);
}
void test_ads131_suite(void)
{
    RUN_TEST(recorded_events_are_work_not_notification_counts); RUN_TEST(boot42_backlog_and_runtime_snapshot);
    RUN_TEST(recorded_event_stale_and_transfer_crossing); RUN_TEST(event_ring_gating_clear_and_wrap);
    RUN_TEST(boot49_first_settling_read_allows_one_following_event);
    RUN_TEST(two_events_read_in_order_three_events_fault);
    RUN_TEST(latency_capture_is_bounded_and_does_not_change_quality);
    RUN_TEST(first_unknown_distinguishes_causes_and_survives_stop);
    RUN_TEST(qualification_discards_samples_and_requires_real_rate);
    RUN_TEST(fault_history_preserves_previous_and_rejected_read);
    RUN_TEST(settling_resync_is_bounded_and_never_published);
    RUN_TEST(captured_boot38_startup_regression);
    RUN_TEST(startup_first_failure_snapshot); RUN_TEST(startup_padding_and_timeout_diagnostics);
    RUN_TEST(drdy_timestamp_and_crossing); RUN_TEST(stale_drdy_rejected);
    RUN_TEST(complete_vectors);RUN_TEST(corruption_output_validity);RUN_TEST(signed_and_crc_answers);
    RUN_TEST(tuple_rejection);RUN_TEST(initialize_pipeline_readback);RUN_TEST(mismatch_rollback);
    RUN_TEST(masked_id);RUN_TEST(each_spi_failure_rolls_back);RUN_TEST(ready_reset_timeout);
    RUN_TEST(repeated_lifecycle);RUN_TEST(overflow_faults_without_overwrite);RUN_TEST(production_ring_full_faults_before_silent_loss);RUN_TEST(stale_notifications);
    RUN_TEST(service_gap_and_stall);RUN_TEST(acquisition_integrity_faults);RUN_TEST(wrap_and_output);
    RUN_TEST(diagnostic_fake_profile);RUN_TEST(diagnostic_retained_fault_is_not_success);
}

#include "nn2_test.h"
#include "nn2_ads131_logic.h"
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
    uint16_t regs[5], pending;
    uint64_t time;
    unsigned calls, fail_at, mismatch, reset_calls, sync_calls;
    uint32_t reset_low_us, ready_ms;
    bool high, ready_failure, reset_failure, corrupt, bad_ack;
} fake_t;
static fake_t fake;
static nn2_ads131_logic_t dev;
static const nn2_ads131_config_t config = {8192000u,512u,8000u,8000000u};
static nn2_status_t fake_transfer(void *ctx, const uint8_t *tx, uint8_t *rx)
{
    fake_t *f=ctx; uint16_t cmd=(uint16_t)(((uint16_t)tx[0]<<8)|tx[1]);
    f->calls++;
    if (f->fail_at == f->calls) return NN2_ERR_IO;
    memcpy(rx,golden[0],18u); rx[0]=(uint8_t)(f->pending>>8); rx[1]=(uint8_t)f->pending;
    f->pending=0x010fu;
    unsigned address=(cmd>>7)&0x3fu;
    if ((cmd&0xe000u)==0xa000u && address<5u) {
        f->pending=f->regs[address];
        if (f->mismatch==address) f->pending^=1u;
    }
    if ((cmd&0xe000u)==0x6000u && address<5u) {
        f->regs[address]=(uint16_t)(((uint16_t)tx[3]<<8)|tx[4]);
        f->pending=(uint16_t)(0x4000u|(address<<7));
        if (f->bad_ack) f->pending^=1u;
    }
    uint16_t crc=oracle_crc(rx);rx[15]=(uint8_t)(crc>>8);rx[16]=(uint8_t)crc;
    if (f->corrupt) rx[3]^=1u;
    f->time+=18u; return NN2_OK;
}
static nn2_status_t fake_reset(void *ctx,bool high)
{
    fake_t *f=ctx; f->reset_calls++;f->high=high;
    return f->reset_failure?NN2_ERR_IO:NN2_OK;
}
static nn2_status_t fake_sync(void *ctx) { fake_t *f=ctx;f->sync_calls++;return NN2_OK; }
static void fake_delay(void *ctx,uint32_t us)
{
    fake_t *f=ctx;f->time+=us;
    if (!f->high) { f->reset_low_us=us; f->regs[0]=0x24abu;f->pending=0x050fu; }
}
static nn2_status_t fake_ready(void *ctx,uint32_t ms)
{
    fake_t *f=ctx; f->ready_ms=ms; return f->ready_failure?NN2_ERR_TIMEOUT:NN2_OK;
}
static uint64_t fake_now(void *ctx) { return ((fake_t*)ctx)->time; }
static void bind_fake(void)
{
    memset(&fake,0,sizeof(fake));fake.mismatch=99u;
    nn2_ads131_io_t io={&fake,fake_transfer,fake_reset,fake_sync,fake_delay,fake_ready,fake_now};
    nn2_ads131_logic_bind(&dev,io);
}
static void running(void)
{
    bind_fake(); TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_initialize(&dev,&config));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_start(&dev));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1u));
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
    TEST_ASSERT_EQUAL_HEX16(0x0110u,fake.regs[2]);TEST_ASSERT_EQUAL_HEX16(0x0f0au,fake.regs[3]);
    TEST_ASSERT_EQUAL_HEX16(0u,fake.regs[4]);TEST_ASSERT_EQUAL(14u,fake.calls);
    TEST_ASSERT_TRUE(fake.reset_low_us>250u);TEST_ASSERT_EQUAL(20u,fake.ready_ms);
    TEST_ASSERT_EQUAL(NN2_ADC_READY,dev.observation.state);
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
    for(unsigned at=1u;at<=14u;++at) {
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
static void overflow_drop_oldest(void)
{
    running();for(unsigned i=0;i<134u;++i) TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_service(&dev,1u));
    TEST_ASSERT_EQUAL(128u,dev.count);TEST_ASSERT_EQUAL(6u,dev.observation.overflow);
    TEST_ASSERT_EQUAL(6u,dev.observation.lost);TEST_ASSERT_EQUAL(134u,dev.observation.produced);
    nn2_core_adc_record_t out;TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_pop(&dev,&out));
    TEST_ASSERT_EQUAL(9u,out.sequence); /* 2 settling + 6 overwritten + first */
    for(unsigned i=1;i<128u;++i) TEST_ASSERT_EQUAL(NN2_OK,nn2_ads131_pop(&dev,&out));
    TEST_ASSERT_TRUE((out.quality&NN2_ADC_QUALITY_OVERFLOW)!=0u);
    TEST_ASSERT_EQUAL(NN2_ERR_EMPTY,nn2_ads131_pop(&dev,&out));
    TEST_ASSERT_EQUAL(128u,dev.observation.consumed);
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
static unsigned app_stop_count,app_reads;static nn2_status_t app_failure;
static nn2_status_t app_init(void *ctx,uint32_t ms) {(void)ctx;(void)ms;return app_failure;}
static nn2_status_t app_start(void *ctx,uint32_t ms) {(void)ctx;(void)ms;return NN2_OK;}
static nn2_status_t app_stop(void *ctx,uint32_t ms) {(void)ctx;(void)ms;app_stop_count++;return NN2_OK;}
static nn2_status_t app_read(void *ctx,nn2_core_adc_record_t *out,uint32_t ms)
{(void)ctx;(void)ms;app_reads++;memset(out,0,sizeof(*out));return NN2_OK;}
static nn2_status_t app_observe(void *ctx,nn2_core_adc_observation_t *out,uint32_t ms)
{(void)ctx;(void)ms;memset(out,0,sizeof(*out));return NN2_OK;}
static void diagnostic_fake_profile(void)
{
    nn2_port_adc_stream_t p={NULL,app_init,app_start,app_stop,app_read,app_observe};
    nn2_core_adc_observation_t out;nn2_core_adc_record_t record;
    app_stop_count=0u;app_reads=0u;app_failure=NN2_OK;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_adc_diagnostic_run(&p,8u,&out,&record));
    TEST_ASSERT_EQUAL(1u,app_stop_count);TEST_ASSERT_EQUAL(8u,app_reads);
    app_failure=NN2_ERR_IO;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_app_adc_diagnostic_run(&p,8u,&out,&record));
    TEST_ASSERT_EQUAL(2u,app_stop_count);TEST_ASSERT_EQUAL(8u,app_reads);
}
void test_ads131_suite(void)
{
    RUN_TEST(complete_vectors);RUN_TEST(corruption_output_validity);RUN_TEST(signed_and_crc_answers);
    RUN_TEST(tuple_rejection);RUN_TEST(initialize_pipeline_readback);RUN_TEST(mismatch_rollback);
    RUN_TEST(masked_id);RUN_TEST(each_spi_failure_rolls_back);RUN_TEST(ready_reset_timeout);
    RUN_TEST(repeated_lifecycle);RUN_TEST(overflow_drop_oldest);RUN_TEST(stale_notifications);
    RUN_TEST(service_gap_and_stall);RUN_TEST(acquisition_integrity_faults);RUN_TEST(wrap_and_output);
    RUN_TEST(diagnostic_fake_profile);
}

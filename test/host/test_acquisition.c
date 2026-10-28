#include "nn2_test.h"
#include "nn2_core_acquisition.h"
#include "nn2_app_comm.h"
#include <string.h>
static nn2_core_acquisition_t a;
static uint8_t payload[NN2_ACQ_PAYLOAD_SIZE];
static uint32_t seq;
static nn2_status_t sample(uint64_t t, int32_t value)
{
    nn2_core_adc_record_t r={.drdy_us=t,.read_complete_us=t+5000,
        .session=7,.sequence=++seq,.source=NN2_SAMPLE_SRC_ADC};
    for (unsigned c=0;c<4;++c) r.channel[c]=value+(int32_t)c;
    return nn2_core_acquisition_sample(&a,&r,payload);
}
static void window(uint32_t frame, unsigned phase, uint64_t begin)
{
    nn2_core_acq_window_t w={.commit_us=begin+100,.end_us=begin+2400,
        .origin_us=1000+(uint64_t)frame*100000,.slot_start_us=begin-(phase&1u)*2500,
        .frame=frame,.phase=(uint8_t)phase};
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_acquisition_window(&a,&w));
}
static void setup(void) { nn2_core_acquisition_reset(&a);seq=0;memset(payload,0,sizeof(payload)); }
static void paired_frame(void)
{
    setup();
    for(unsigned phase=0;phase<40;++phase) {
        uint64_t start=101000+(uint64_t)phase*2500;
        window(1,phase,start);
        /* Read completion deliberately lies in another phase. */
        for(unsigned k=0;k<20;++k) TEST_ASSERT_EQUAL(NN2_ERR_EMPTY,sample(start+k*125,phase&1u?40:100));
    }
    window(2,0,201000);
    TEST_ASSERT_EQUAL(NN2_OK,sample(201125,0));
    TEST_ASSERT_EQUAL_MEMORY("ACQ1",payload,4);
    TEST_ASSERT_EQUAL(1,nn2_core_get_u32(payload+4));
    for(unsigned s=0;s<20;++s) {
        uint8_t *q=payload+36+s*56;
        TEST_ASSERT_EQUAL(10,q[1]);TEST_ASSERT_EQUAL(10,q[2]);
        TEST_ASSERT_EQUAL(NN2_ACQ_TARGET,q[3]);
        TEST_ASSERT_EQUAL(1000,nn2_core_get_u32(q+8));
        TEST_ASSERT_EQUAL(400,nn2_core_get_u32(q+24));
    }
}
static void filter_and_underk(void)
{
    setup(); window(1,0,101000); window(1,1,103500);
    TEST_ASSERT_EQUAL(NN2_ERR_EMPTY,sample(101974,900)); /* Before full filter settle. */
    for(unsigned k=0;k<8;++k) sample(102000+k*125,100);
    for(unsigned k=0;k<7;++k) sample(104500+k*125,40);
    window(2,0,201000);TEST_ASSERT_EQUAL(NN2_OK,sample(201125,0));
    TEST_ASSERT_EQUAL(8,payload[37]);TEST_ASSERT_EQUAL(7,payload[38]);
    TEST_ASSERT_EQUAL(NN2_ACQ_UNDER_K,payload[39]);
    TEST_ASSERT_EQUAL(800,nn2_core_get_u32(payload+44)); /* Raw ON survives invalid pair. */
}
static void gap_session_and_restart(void)
{
    setup(); window(1,0,101000); sample(102000,100); ++seq;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,sample(102125,100));
    setup();window(1,0,101000);sample(102000,100);
    nn2_core_adc_record_t r={.drdy_us=102125,.sequence=2,.session=8,.source=NN2_SAMPLE_SRC_ADC};
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_core_acquisition_sample(&a,&r,payload));
    setup();window(2,1,203500);sample(204500,40);window(3,0,301000);
    TEST_ASSERT_EQUAL(NN2_OK,sample(301125,0));
    TEST_ASSERT_EQUAL(0,payload[37]); /* No prior-session ON reuse. */
}
static void allowed_rates(void)
{
    for(unsigned fs=0;fs<=12;++fs)
        TEST_ASSERT_EQUAL(fs==2||fs==4||fs==5||fs==8||fs==10,
            nn2_core_acquisition_fs_valid((uint8_t)fs));
}
static uint8_t optical_state;
static nn2_status_t safety_off(void *ctx) { (void)ctx;optical_state=NN2_OPTICAL_IDLE;return NN2_OK; }
static uint8_t optical_get(void *ctx) { (void)ctx;return optical_state; }
static nn2_status_t optical_clear(void *ctx) { (void)ctx;return NN2_OK; }
static nn2_status_t optical_start(void *ctx,const nn2_port_optical_config_t *c)
{ (void)ctx;(void)c;optical_state=NN2_OPTICAL_RUNNING;return NN2_OK; }
static nn2_status_t frame_pop(void *ctx,uint8_t *p,size_t cap,uint64_t *t)
{ (void)ctx;(void)cap;memset(p,0,NN2_ACQ_PAYLOAD_SIZE);memcpy(p,"ACQ1",4);*t=12345;return NN2_OK; }
static void frame_session_provenance(void)
{
    nn2_app_comm_storage_t app;
    nn2_port_safety_t safety={.hold_off=safety_off};
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_init(&app,1,&safety,10));
    nn2_port_optical_t port={.start=optical_start,.state=optical_get,.clear_fault=optical_clear,
        .current_limit_ma=30,.real_samples=true,.read_frame=frame_pop};
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_bind_optical(&app,&port));
    uint8_t wire[NN2_CORE_PACKET_MAX],reply[NN2_CORE_PACKET_MAX]; uint64_t peer=0;
    TEST_ASSERT_EQUAL(0,nn2_app_comm_frame(&app,&peer,wire,sizeof(wire)));
    nn2_core_packet_t request={.type=NN2_MSG_REQUEST,.protocol_version=1,.length=6};
    nn2_core_put_u32(request.payload,1);nn2_core_put_u16(request.payload+4,NN2_CMD_START);
    size_t n=nn2_core_packet_encode(&request,wire,sizeof(wire));
    n=nn2_app_comm_process(&app,99,1,wire,n,reply,sizeof(reply));
    nn2_core_packet_t result;
    TEST_ASSERT_TRUE(nn2_core_packet_decode(reply,n,&result));
    TEST_ASSERT_EQUAL(0,result.flags);TEST_ASSERT_EQUAL(0,result.payload[11]);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_tick(&app,2,true));
    n=nn2_app_comm_frame(&app,&peer,wire,sizeof(wire));
    TEST_ASSERT_TRUE(nn2_core_packet_decode(wire,n,&result));
    TEST_ASSERT_EQUAL(99,peer);TEST_ASSERT_EQUAL(NN2_MSG_FRAME,result.type);
    TEST_ASSERT_EQUAL(NN2_FLAG_TS_ESTIMATED,result.flags);
    TEST_ASSERT_NOT_EQUAL(0,result.session_id);TEST_ASSERT_EQUAL_UINT64(12345,result.timestamp_us);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_safe_off(&app));
    TEST_ASSERT_EQUAL(0,nn2_app_comm_frame(&app,&peer,wire,sizeof(wire)));
}
void test_acquisition_suite(void)
{
    RUN_TEST(frame_session_provenance); RUN_TEST(paired_frame); RUN_TEST(filter_and_underk);
    RUN_TEST(gap_session_and_restart); RUN_TEST(allowed_rates);
}

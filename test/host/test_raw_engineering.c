#include "nn2_test.h"
#include "nn2_app_comm.h"
#include "nn2_core_acquisition.h"
#include "nn2_core_raw_buffer.h"
#include <string.h>
static nn2_raw_buffer_t buffer;
static nn2_app_comm_storage_t app;
static uint8_t wire[NN2_CORE_PACKET_MAX], reply[NN2_CORE_PACKET_MAX];
static uint8_t acq[NN2_ACQ_PAYLOAD_SIZE];
static uint8_t state;
static bool mode;
static uint64_t now;
static nn2_status_t off(void *ctx) { (void)ctx; return NN2_OK; }
static uint8_t get_state(void *ctx) { (void)ctx; return state; }
static nn2_status_t clear(void *ctx) { (void)ctx; return NN2_OK; }
static nn2_status_t start(void *ctx, const nn2_port_optical_config_t *cfg)
{ (void)ctx; mode=cfg->engineering; state=NN2_OPTICAL_RUNNING; return NN2_OK; }
static nn2_status_t old_frame(void *ctx, uint8_t *out, size_t cap, uint64_t *ts)
{ (void)ctx; (void)cap; memcpy(out,acq,sizeof(acq)); *ts=12345; return NN2_OK; }
static nn2_status_t raw_frame(void *ctx, uint8_t *out, size_t cap, uint64_t *ts, size_t *n)
{ (void)ctx; *n=nn2_raw_buffer_encode(&buffer,out,cap,ts); return *n ? NN2_OK : NN2_ERR_INVALID_STATE; }
static void tx(void *ctx, bool sent) { (void)ctx; nn2_raw_buffer_tx_result(&buffer,sent); }
static size_t raw_info(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx; (void)cap;
    const char *text=";raw_packets_sent=0;raw_dropped_samples=0;raw_buffer_highwater=0";
    memcpy(out,text,strlen(text)); return strlen(text);
}
static void setup(bool raw)
{
    state=NN2_OPTICAL_IDLE; mode=false; now=100;
    nn2_raw_buffer_reset(&buffer);
    nn2_port_safety_t safety={.hold_off=off};
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_init(&app,42,&safety,123));
    nn2_port_optical_t optical={.start=start,.state=get_state,.clear_fault=clear,
        .current_limit_ma=NN2_OPTICAL_MAX_MA,.real_samples=true,.frame_tx_result=tx};
    if (raw) { optical.read_frame_ex=raw_frame; optical.read_raw_info=raw_info; }
    else optical.read_frame=old_frame;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_bind_optical(&app,&optical));
}
static nn2_core_packet_t request(uint32_t id, uint16_t op, const uint8_t *arg, uint16_t n)
{
    nn2_core_packet_t p={.type=NN2_MSG_REQUEST,.protocol_version=1,.length=(uint16_t)(6+n)};
    nn2_core_put_u32(p.payload,id); nn2_core_put_u16(p.payload+4,op);
    if (n) memcpy(p.payload+6,arg,n);
    size_t count=nn2_core_packet_encode(&p,wire,sizeof(wire));
    count=nn2_app_comm_process(&app,99,now++,wire,count,reply,sizeof(reply));
    TEST_ASSERT_TRUE(nn2_core_packet_decode(reply,count,&p));
    return p;
}
static void acq1_bytes_unchanged(void)
{
    setup(false);
    for (size_t i=0;i<sizeof(acq);++i) acq[i]=(uint8_t)i;
    memcpy(acq,"ACQ1",4);
    nn2_core_put_u32(acq+4,77);
    nn2_core_packet_t result=request(1,NN2_CMD_START,NULL,0);
    TEST_ASSERT_FALSE(mode);
    uint32_t session=result.session_id;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_tick(&app,now++,true));
    uint64_t peer;
    size_t count=nn2_app_comm_frame(&app,&peer,wire,sizeof(wire));
    nn2_core_packet_t expected={.type=NN2_MSG_FRAME,.protocol_version=1,
        .flags=NN2_FLAG_TS_ESTIMATED,.session_id=session,.sequence=2,
        .timestamp_us=12345,.length=NN2_ACQ_PAYLOAD_SIZE};
    memcpy(expected.payload,acq,sizeof(acq));
    size_t wanted=nn2_core_packet_encode(&expected,reply,sizeof(reply));
    TEST_ASSERT_EQUAL(wanted,count); TEST_ASSERT_EQUAL(99,peer);
    TEST_ASSERT_EQUAL_MEMORY(reply,wire,count);
    uint8_t echo[8]={0};
    result=request(2,NN2_CMD_HEARTBEAT,echo,8);
    TEST_ASSERT_EQUAL(3,result.sequence);
    nn2_core_put_u32(acq+4,78);
    count=nn2_app_comm_frame(&app,&peer,wire,sizeof(wire));
    TEST_ASSERT_TRUE(nn2_core_packet_decode(wire,count,&result));
    TEST_ASSERT_EQUAL(4,result.sequence);
    TEST_ASSERT_EQUAL(78,nn2_core_get_u32(result.payload+4));
    TEST_ASSERT_EQUAL_MEMORY(acq,result.payload,sizeof(acq));
}
static void mode_and_raw_sequence(void)
{
    setup(true);
    uint8_t engineering=1;
    nn2_core_packet_t p=request(90,NN2_CMD_INFO,NULL,0);
    p.payload[p.length]=0;
    TEST_ASSERT_NOT_NULL(strstr((char *)p.payload+28,"mode=ACQ"));
    TEST_ASSERT_NOT_NULL(strstr((char *)p.payload+28,"raw_dropped_samples=0"));
    p=request(1,NN2_CMD_SET_MODE,&engineering,1);
    TEST_ASSERT_EQUAL(0,nn2_core_get_u16(p.payload+6));
    p=request(2,NN2_CMD_START,NULL,0); TEST_ASSERT_TRUE(mode);
    p=request(3,NN2_CMD_SET_MODE,&engineering,1);
    TEST_ASSERT_EQUAL(NN2_REPLY_STATE,nn2_core_get_u16(p.payload+6));
    TEST_ASSERT_TRUE(mode);
    uint64_t peer;
    TEST_ASSERT_EQUAL(0,nn2_app_comm_frame(&app,&peer,wire,sizeof(wire)));
    int32_t ain[4]={-8388608,-1,0,8388607};
    nn2_raw_buffer_push(&buffer,ain,0xffffffffu,12345,2,0,255);
    size_t count=nn2_app_comm_frame(&app,&peer,wire,sizeof(wire));
    TEST_ASSERT_EQUAL(62,count);
    TEST_ASSERT_TRUE(nn2_core_packet_decode(wire,count,&p));
    TEST_ASSERT_EQUAL(3,p.sequence); TEST_ASSERT_EQUAL(99,peer);
    nn2_oc_raw_t raw; nn2_oc_stats_t stats={0};
    TEST_ASSERT_EQUAL(NN2_OC_OK,nn2_oc_decode(p.payload,p.length,NULL,&raw,&stats));
    TEST_ASSERT_EQUAL(1,raw.raw_flags); TEST_ASSERT_EQUAL(1,raw.sample_count);
    TEST_ASSERT_EQUAL_MEMORY(ain,raw.sample[0].ain,sizeof(ain));
    nn2_app_comm_frame_tx_result(&app,false);
    TEST_ASSERT_EQUAL(0,buffer.packets_sent);
    nn2_app_comm_frame_tx_result(&app,true);
    TEST_ASSERT_EQUAL(1,buffer.packets_sent);
    nn2_raw_buffer_push(&buffer,ain,0,12470,2,4,0);
    count=nn2_app_comm_frame(&app,&peer,wire,sizeof(wire));
    TEST_ASSERT_TRUE(nn2_core_packet_decode(wire,count,&p));
    TEST_ASSERT_EQUAL(4,p.sequence);
    TEST_ASSERT_EQUAL(1,nn2_core_get_u16(p.payload+10)); /* sample wrap is contiguous */
    for (uint32_t i=1;i<=100;++i) nn2_raw_buffer_push(&buffer,ain,i,12470+i*125u,2,4,0);
    count=nn2_app_comm_frame(&app,&peer,wire,sizeof(wire));
    TEST_ASSERT_EQUAL(1448,count);
    TEST_ASSERT_TRUE(nn2_core_packet_decode(wire,count,&p));
    TEST_ASSERT_EQUAL(5,p.sequence);
    TEST_ASSERT_EQUAL(100,nn2_core_get_u16(p.payload+8));
    p=request(4,NN2_CMD_STOP,NULL,0);
    engineering=0;
    p=request(5,NN2_CMD_SET_MODE,&engineering,1);
    TEST_ASSERT_EQUAL(NN2_REPLY_STATE,nn2_core_get_u16(p.payload+6));
    state=NN2_OPTICAL_IDLE;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_tick(&app,now++,true));
    p=request(6,NN2_CMD_SET_MODE,&engineering,1);
    TEST_ASSERT_EQUAL(0,nn2_core_get_u16(p.payload+6));
    p=request(7,NN2_CMD_START,NULL,0); TEST_ASSERT_FALSE(mode);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_comm_tick(&app,now++,true));
    nn2_raw_buffer_push(&buffer,ain,101,25000,3,4,0);
    TEST_ASSERT_EQUAL(0,nn2_app_comm_frame(&app,&peer,wire,sizeof(wire)));
}
static void full_buffer_gap_and_codec_limits(void)
{
    nn2_raw_buffer_reset(&buffer);
    int32_t ain[4]={1,2,3,4};
    for (uint32_t i=0;i<NN2_RAW_BUFFER_CAPACITY+7u;++i)
        nn2_raw_buffer_push(&buffer,ain,i,i*125u,1,4,0);
    TEST_ASSERT_EQUAL(7,buffer.dropped);
    TEST_ASSERT_EQUAL(NN2_RAW_BUFFER_CAPACITY,buffer.highwater);
    uint64_t timestamp; nn2_oc_raw_t raw; nn2_oc_stats_t stats={0};
    size_t n=nn2_raw_buffer_encode(&buffer,wire,sizeof(wire),&timestamp);
    TEST_ASSERT_EQUAL(1416,n);
    TEST_ASSERT_EQUAL(NN2_OC_OK,nn2_oc_decode(wire,n,NULL,&raw,&stats));
    TEST_ASSERT_EQUAL(100,raw.sample_count); TEST_ASSERT_EQUAL(1,raw.raw_flags);
    while (nn2_raw_buffer_encode(&buffer,wire,sizeof(wire),&timestamp)) { }
    nn2_raw_buffer_push(&buffer,ain,263,32875,1,0,255);
    n=nn2_raw_buffer_encode(&buffer,wire,sizeof(wire),&timestamp);
    TEST_ASSERT_EQUAL(NN2_OC_OK,nn2_oc_decode(wire,n,NULL,&raw,&stats));
    TEST_ASSERT_EQUAL(3,raw.raw_flags); TEST_ASSERT_EQUAL(263,raw.first_sample_sequence);
    TEST_ASSERT_EQUAL(7,buffer.dropped);
    size_t untouched=123;
    raw.sample_count=0;
    TEST_ASSERT_EQUAL(NN2_OC_INVALID,nn2_oc_encode_raw(&raw,wire,sizeof(wire),&untouched,&stats));
    TEST_ASSERT_EQUAL(123,untouched);
    raw.sample_count=101;
    TEST_ASSERT_EQUAL(NN2_OC_TOO_LARGE,nn2_oc_encode_raw(&raw,wire,sizeof(wire),&untouched,&stats));
    TEST_ASSERT_EQUAL(123,untouched);
}
void test_raw_engineering_suite(void)
{
    RUN_TEST(acq1_bytes_unchanged);
    RUN_TEST(mode_and_raw_sequence);
    RUN_TEST(full_buffer_gap_and_codec_limits);
}

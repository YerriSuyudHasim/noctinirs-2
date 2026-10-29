/* Production control glue with deterministic RTOS/clock seams at 100 Hz.
 * This does not simulate ESP scheduling or electrical behavior. */
#include "nn2_test.h"
#include "nn2_ads131_logic.h"
#include "nn2_ads131_events.h"
#include "ads131_golden.h"
#include <stdlib.h>
#include <string.h>
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 100u
#define pdTRUE 1
#define ESP_OK 0
#define eSetBits 1
enum { CMD_INIT, CMD_START, CMD_STOP, CMD_READ, CMD_OBSERVE };
typedef struct { uint32_t id, op; uint64_t deadline; } request_t;
typedef struct {
    uint32_t id; nn2_status_t result;
    nn2_core_adc_record_t record; nn2_core_adc_observation_t observation;
} reply_t;
typedef struct {
    struct { nn2_ads131_config_t config; } board;
    nn2_ads131_logic_t logic;
    bool created, poisoned, hardware_ready, watchdog;
    void *task, *caller, *commands, *replies;
    uint32_t next_id;
} device_t;
static device_t d;
static nn2_ads131_events_t events;
static bool event_mode, no_events, edge_in_sync;
static uint64_t clock_us, send_delay, delay_us_step;
static uint32_t pending, transfer_count, sync_count, receive_count, sends, notify_count;
static uint64_t deadlines[8];
static TickType_t last_wait;
static bool have_command, reset_failure, sync_failure, watchdog_failure, pump;
static request_t queued;
static reply_t posted;
typedef struct { uint64_t delay; int offset; nn2_status_t result; } delivery_t;
static delivery_t deliveries[8];
static unsigned delivery_count, delivery_index;
static int command_queue, reply_queue, caller_task;
static uint64_t now_us(void *ctx) { (void)ctx; return clock_us; }
/* Used by the exact legacy extraction, not by corrected deadline arithmetic. */
#ifdef NN2_CONTROL_LEGACY
static TickType_t ticks(uint32_t ms) { return (ms + 9u) / 10u; }
#endif
static void *xTaskGetCurrentTaskHandle(void) { return &caller_task; }
static int xTaskNotify(void *task, uint32_t bits, int action)
{ (void)task; (void)action; TEST_ASSERT_EQUAL_HEX32(0x80000000u,bits); notify_count++; return 1; }
static int xTaskNotifyWait(uint32_t entry, uint32_t exit, uint32_t *out, TickType_t wait)
{
    (void)entry; (void)exit;
    if (event_mode && wait && d.logic.observation.state==NN2_ADC_RUNNING && events.head==events.tail) {
        clock_us+=no_events?10000u:125u;
        if (!no_events) { nn2_ads131_events_push(&events,clock_us); pending|=1u; }
    }
    *out=pending; pending=0u; return 1;
}
static int esp_task_wdt_reset(void) { return watchdog_failure ? -1 : ESP_OK; }
static void vTaskDelay(TickType_t wait) { clock_us += (uint64_t)wait * delay_us_step; }
static int xQueueSend(void *q, const void *item, TickType_t wait)
{
    TEST_ASSERT_EQUAL_PTR(&command_queue,q); last_wait=wait;
    queued=*(const request_t *)item; have_command=true;
    TEST_ASSERT_TRUE(sends<8u); deadlines[sends++]=queued.deadline;
    clock_us+=send_delay; return pdTRUE;
}
static int xQueueOverwrite(void *q, const void *item)
{ TEST_ASSERT_EQUAL_PTR(&reply_queue,q); posted=*(const reply_t *)item; return pdTRUE; }
static void dispatch_during_receive(void);
static int receive_sized(void *q, void *out, size_t size, TickType_t wait)
{
    if (q==&command_queue) {
        TEST_ASSERT_EQUAL(sizeof(queued),size);
        if (size!=sizeof(queued)) return 0;
        if (!have_command) return 0;
        memcpy(out,&queued,size); have_command=false; return pdTRUE;
    }
    TEST_ASSERT_EQUAL_PTR(&reply_queue,q); last_wait=wait; receive_count++;
    TEST_ASSERT_EQUAL(sizeof(posted),size);
    if (q!=&reply_queue || size!=sizeof(posted)) return 0;
    if (pump) dispatch_during_receive();
    if (delivery_index<delivery_count) {
        delivery_t v=deliveries[delivery_index++]; clock_us+=v.delay;
        posted.id=queued.id+(uint32_t)v.offset; posted.result=v.result;
        memset(&posted.record,0x3c,sizeof(posted.record));
        memset(&posted.observation,0x4d,sizeof(posted.observation));
        memcpy(out,&posted,size); return pdTRUE;
    }
    if (pump) { memcpy(out,&posted,size); return pdTRUE; }
    clock_us+=(uint64_t)wait*10000u; return 0;
}
#define xQueueReceive(q,out,wait) receive_sized((q),(out),sizeof(*(out)),(wait))
#ifndef NN2_CONTROL_IMPLEMENTATION
#define NN2_CONTROL_IMPLEMENTATION "nn2_ads131_control.inc"
#endif
#include NN2_CONTROL_IMPLEMENTATION
static void dispatch_during_receive(void) { bool discard=false; (void)dispatch(&d,&discard); }
static nn2_status_t reset_fake(void *ctx,bool high)
{ (void)ctx; if (!high) { events.enabled=false; nn2_ads131_events_clear(&events); } return reset_failure?NN2_ERR_IO:NN2_OK; }
static nn2_status_t sync_fake(void *ctx)
{
    (void)ctx; sync_count++;
    if (event_mode && !sync_failure) {
        nn2_ads131_events_clear(&events); events.enabled=true;
        if (edge_in_sync) { clock_us+=125u; nn2_ads131_events_push(&events,clock_us); pending|=1u; }
    }
    return sync_failure?NN2_ERR_IO:NN2_OK;
}
static uint32_t take_fake(void *ctx,nn2_ads131_event_t *event,uint32_t *dropped)
{ (void)ctx; return nn2_ads131_events_take(&events,event,dropped); }
static uint64_t latch_fake(void *ctx,uint32_t *serial)
{ (void)ctx; *serial=events.serial; return events.latest_us; }
static nn2_status_t transfer_fake(void *ctx,const uint8_t *tx,uint8_t *rx)
{ (void)ctx; (void)tx; transfer_count++; memcpy(rx,golden[0],18u); return NN2_OK; }
static void fresh(void)
{
    memset(&d,0,sizeof(d)); memset(&posted,0,sizeof(posted));
    memset(&events,0,sizeof(events)); event_mode=no_events=edge_in_sync=false;
    clock_us=send_delay=0u; delay_us_step=10000u;
    pending=transfer_count=sync_count=receive_count=sends=notify_count=0u;
    have_command=reset_failure=sync_failure=watchdog_failure=pump=false;
    delivery_count=delivery_index=0u;
    d.created=d.hardware_ready=d.watchdog=true;
    d.caller=&caller_task; d.commands=&command_queue; d.replies=&reply_queue;
    d.board.config=(nn2_ads131_config_t){8192000u,512u,8000u,8000000u};
    nn2_ads131_io_t io={0}; io.reset=reset_fake; io.sync=sync_fake;
    io.transfer=transfer_fake; io.now_us=now_us;
    nn2_ads131_logic_bind(&d.logic,io);
    d.logic.observation.state=NN2_ADC_RUNNING;
}
static void command(uint32_t op,uint64_t deadline)
{ queued=(request_t){7u,op,deadline}; have_command=true; }
static void delivery(uint64_t delay,int offset,nn2_status_t result)
{ TEST_ASSERT_TRUE(delivery_count<8u); deliveries[delivery_count++]=(delivery_t){delay,offset,result}; }
static void preserved(uint32_t op,bool expired)
{
    fresh(); clock_us=100u; command(op,expired?50u:10000u);
    owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(expired?NN2_ERR_TIMEOUT:NN2_ERR_INVALID_STATE,posted.result);
    TEST_ASSERT_EQUAL(NN2_ADC_RUNNING,d.logic.observation.state);
    TEST_ASSERT_EQUAL(1u,d.logic.observation.sequence);
    TEST_ASSERT_EQUAL(1u,d.logic.observation.produced);
    clock_us=225u; owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(2u,d.logic.observation.sequence);
    TEST_ASSERT_EQUAL(2u,transfer_count); TEST_ASSERT_EQUAL(2u,d.logic.count);
    TEST_ASSERT_EQUAL(0u,d.logic.observation.lost); TEST_ASSERT_EQUAL(0u,d.logic.observation.loss_unknown);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_NONE,d.logic.observation.fault);
    TEST_ASSERT_EQUAL(NN2_ADC_QUALITY_NO_EDGE,d.logic.items[1].quality);
    owner_iteration(&d,0u); TEST_ASSERT_EQUAL(2u,transfer_count);
}
static void r1_rejected_init(void) { preserved(CMD_INIT,false); }
static void r1_rejected_start(void) { preserved(CMD_START,false); }
static void r1_expired_stop(void) { preserved(CMD_STOP,true); }
static void r1_captured_and_pending_start(void)
{
    fresh(); clock_us=100u; command(CMD_START,10000u); pending=0x80000001u;
    owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_STATE,posted.result);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT,d.logic.observation.state);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_GAP,d.logic.observation.fault);
    TEST_ASSERT_EQUAL(2u,d.logic.observation.sequence); TEST_ASSERT_EQUAL(2u,d.logic.observation.lost);
    TEST_ASSERT_EQUAL(1u,d.logic.observation.adc_missed); TEST_ASSERT_EQUAL(1u,d.logic.observation.loss_unknown);
    TEST_ASSERT_TRUE((d.logic.quality&NN2_ADC_QUALITY_GAP)!=0u);
    TEST_ASSERT_EQUAL(0u,d.logic.observation.produced); TEST_ASSERT_EQUAL(0u,transfer_count);
    owner_iteration(&d,0u); TEST_ASSERT_EQUAL(2u,d.logic.observation.lost);
}
static void positive_acquisition(void)
{
    fresh(); clock_us=100u; owner_iteration(&d,1u); clock_us=225u; owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(2u,d.logic.observation.sequence); TEST_ASSERT_EQUAL(2u,d.logic.count);
    TEST_ASSERT_EQUAL(0u,d.logic.observation.lost); TEST_ASSERT_EQUAL(NN2_ADC_QUALITY_NO_EDGE,d.logic.items[1].quality);
}
static void positive_start_stop_resynchronization(void)
{
    fresh(); d.logic.observation.state=NN2_ADC_READY; pending=5u; command(CMD_START,10000u);
    owner_iteration(&d,4u); TEST_ASSERT_EQUAL(NN2_OK,posted.result);
    TEST_ASSERT_EQUAL(1u,sync_count); TEST_ASSERT_EQUAL(0u,transfer_count); TEST_ASSERT_EQUAL(0u,pending);
    TEST_ASSERT_EQUAL(NN2_ADC_RUNNING,d.logic.observation.state); TEST_ASSERT_EQUAL(3u,d.logic.settling);
    for (unsigned i=0u;i<4u;++i) owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(4u,d.logic.observation.sequence); TEST_ASSERT_EQUAL(1u,d.logic.count);
    command(CMD_STOP,10000u); pending=2u; owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(NN2_OK,posted.result); TEST_ASSERT_EQUAL(NN2_ADC_STOPPED,d.logic.observation.state);
    TEST_ASSERT_EQUAL(0u,pending); TEST_ASSERT_EQUAL(4u,transfer_count);
}
static void positive_partial_lifecycle_failure(void)
{
    fresh(); d.logic.observation.state=NN2_ADC_READY; sync_failure=true;
    command(CMD_START,10000u); pending=2u; owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(NN2_ERR_IO,posted.result); TEST_ASSERT_EQUAL(NN2_ADC_FAULT,d.logic.observation.state);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_RESET,d.logic.observation.fault); TEST_ASSERT_EQUAL(0u,transfer_count);
    fresh(); reset_failure=true; command(CMD_STOP,10000u); pending=1u; owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(NN2_ERR_IO,posted.result); TEST_ASSERT_EQUAL(NN2_ADC_FAULT,d.logic.observation.state);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_RESET,d.logic.observation.fault); TEST_ASSERT_EQUAL(0u,pending);
    TEST_ASSERT_EQUAL(0u,transfer_count); TEST_ASSERT_TRUE((d.logic.quality&NN2_ADC_QUALITY_GAP)!=0u);
}
static void positive_watchdog_failure(void)
{
    fresh(); watchdog_failure=true; command(CMD_START,10000u); owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_WATCHDOG,d.logic.observation.fault); TEST_ASSERT_EQUAL(0u,transfer_count);
}
static void positive_reply_before_deadline(void)
{
    fresh(); delivery(999u,0,NN2_OK); reply_t out;
    TEST_ASSERT_EQUAL(NN2_OK,call(&d,CMD_READ,1u,&out));
    TEST_ASSERT_EQUAL(1u,out.id); TEST_ASSERT_EQUAL(1u,last_wait); TEST_ASSERT_EQUAL(1000u,queued.deadline);
    TEST_ASSERT_EQUAL(1u,notify_count);
}
static void late_call(uint64_t delay)
{
    fresh(); delivery(delay,0,NN2_OK); reply_t out,before; memset(&before,0xa5,sizeof(before)); out=before;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,call(&d,CMD_READ,1u,&out));
    TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
}
static void r2_reply_at_deadline(void) { late_call(1000u); }
static void r2_reply_after_deadline(void) { late_call(2000u); }
static void positive_enqueue_expiry(void)
{
    fresh(); send_delay=1000u; delivery(0u,0,NN2_OK);
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,start_port(&d,1u)); TEST_ASSERT_EQUAL(0u,receive_count);
    TEST_ASSERT_EQUAL(1000u,queued.deadline);
}
static void r2_dispatch_then_reply_expiry(void)
{
    fresh(); pump=true; delivery(2000u,0,NN2_OK);
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,stop_port(&d,1u));
    TEST_ASSERT_EQUAL(NN2_ADC_STOPPED,d.logic.observation.state); /* timeout is not cancellation */
}
static void positive_dispatch_expired(void)
{
    fresh(); clock_us=1000u; command(CMD_STOP,1000u); bool discard=true;
    TEST_ASSERT_TRUE(dispatch(&d,&discard)); TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,posted.result);
    TEST_ASSERT_EQUAL(NN2_ADC_RUNNING,d.logic.observation.state);
}
static void positive_stale_then_matching(void)
{
    fresh(); delivery(400u,-1,NN2_OK); delivery(599u,0,NN2_OK);
    reply_t out; TEST_ASSERT_EQUAL(NN2_OK,call(&d,CMD_READ,1u,&out));
    TEST_ASSERT_EQUAL(1u,out.id); TEST_ASSERT_EQUAL(2u,receive_count); TEST_ASSERT_EQUAL(1u,sends);
}
static void r2_stale_then_late_matching(void)
{
    fresh(); delivery(900u,-1,NN2_OK); delivery(100u,0,NN2_OK);
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,init_port(&d,1u)); TEST_ASSERT_EQUAL(2u,receive_count);
}
static void positive_consecutive_ids(void)
{
    fresh(); delivery(2000u,0,NN2_OK); TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,call(&d,CMD_READ,1u,NULL));
    delivery(100u,-1,NN2_OK); delivery(100u,0,NN2_OK); reply_t out;
    TEST_ASSERT_EQUAL(NN2_OK,call(&d,CMD_READ,1u,&out)); TEST_ASSERT_EQUAL(2u,out.id);
    TEST_ASSERT_EQUAL(2u,sends); TEST_ASSERT_EQUAL(1000u,deadlines[0]); TEST_ASSERT_EQUAL(3000u,deadlines[1]);
}
static void positive_empty_read_budget(void)
{
    fresh(); delivery(900u,0,NN2_ERR_EMPTY); nn2_core_adc_record_t out,before;
    memset(&before,0xa5,sizeof(before)); out=before;
    TEST_ASSERT_EQUAL(NN2_ERR_EMPTY,read_port(&d,&out,1u));
    TEST_ASSERT_EQUAL(1u,sends); TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
}
static void r2_empty_retry_no_budget_renewal(void)
{
    fresh(); delay_us_step=10000u; delivery(500u,0,NN2_ERR_EMPTY); delivery(500u,0,NN2_OK);
    nn2_core_adc_record_t out,before; memset(&before,0xa5,sizeof(before)); out=before;
    /* 11ms outward budget; retry at 10.5ms has only 500us, despite a whole tick wait. */
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,read_port(&d,&out,11u));
    TEST_ASSERT_EQUAL(2u,sends); TEST_ASSERT_EQUAL(11000u,deadlines[1]);
    TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
}
static void r2_read_output_unchanged(void)
{
    fresh(); delivery(2000u,0,NN2_OK); nn2_core_adc_record_t out,before;
    memset(&before,0xa5,sizeof(before)); out=before;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,read_port(&d,&out,1u)); TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
}
static void r2_observation_output_unchanged(void)
{
    fresh(); delivery(2000u,0,NN2_OK); nn2_core_adc_observation_t out,before;
    memset(&before,0xa5,sizeof(before)); out=before;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,observe_port(&d,&out,1u)); TEST_ASSERT_EQUAL_MEMORY(&before,&out,sizeof(out));
}
static void positive_port_outputs(void)
{
    fresh(); delivery(999u,0,NN2_OK); nn2_core_adc_record_t record={0};
    TEST_ASSERT_EQUAL(NN2_OK,read_port(&d,&record,1u)); TEST_ASSERT_EQUAL_HEX8(0x3c,((uint8_t *)&record)[0]);
    fresh(); delivery(999u,0,NN2_OK); nn2_core_adc_observation_t observation={0};
    TEST_ASSERT_EQUAL(NN2_OK,observe_port(&d,&observation,1u)); TEST_ASSERT_EQUAL_HEX8(0x4d,((uint8_t *)&observation)[0]);
}
static void event_start(bool early)
{
    fresh(); event_mode=true; edge_in_sync=early;
    d.logic.io.take_events=take_fake; d.logic.io.drdy_latch=latch_fake;
    d.logic.observation.state=NN2_ADC_READY;
    command(CMD_START,500000u); owner_iteration(&d,0xffffffffu);
    TEST_ASSERT_EQUAL(NN2_OK,posted.result); TEST_ASSERT_EQUAL(1027u,transfer_count);
    TEST_ASSERT_TRUE(d.logic.rate.valid); TEST_ASSERT_EQUAL(8000u,d.logic.rate.hz);
    TEST_ASSERT_EQUAL(1024u,d.logic.rate.samples);
    TEST_ASSERT_EQUAL(0u,d.logic.settling); TEST_ASSERT_EQUAL(0u,d.logic.count);
    TEST_ASSERT_EQUAL(NN2_ADC_RUNNING,d.logic.observation.state);
    TEST_ASSERT_EQUAL(1027u,d.logic.edge_serial); TEST_ASSERT_EQUAL(0u,d.logic.observation.lost);
    clock_us+=125u; nn2_ads131_events_push(&events,clock_us);
    pending=1u; owner_iteration(&d,1u); /* Two doorbells, only ONE recorded edge. */
    TEST_ASSERT_EQUAL(1028u,transfer_count); TEST_ASSERT_EQUAL(1u,d.logic.count);
    /* Qualification advances the fake clock by about 128 ms. An absolute
     * 10000 us deadline is already expired, so STOP must be clock-relative. */
    command(CMD_STOP,clock_us + 10000u); owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(NN2_OK,posted.result); TEST_ASSERT_FALSE(events.enabled);
    TEST_ASSERT_EQUAL(NN2_ADC_STOPPED,d.logic.observation.state);
}
static void event_start_primes_before_ack(void) { event_start(false); }
static void event_start_captures_early_sync_edge(void) { event_start(true); }
static void event_start_timeout_never_acknowledges_running(void)
{
    fresh(); event_mode=true; no_events=true;
    d.logic.io.take_events=take_fake; d.logic.io.drdy_latch=latch_fake;
    d.logic.observation.state=NN2_ADC_READY;
    command(CMD_START,50000u); owner_iteration(&d,1u);
    TEST_ASSERT_EQUAL(NN2_ERR_IO,posted.result); TEST_ASSERT_EQUAL(0u,transfer_count);
    TEST_ASSERT_EQUAL(NN2_ADC_FAULT_DRDY,d.logic.runtime_failure.fault);
    TEST_ASSERT_EQUAL(10000u,clock_us); TEST_ASSERT_FALSE(events.enabled);
}
void test_ads131_control_suite(void)
{
#ifdef NN2_CONTROL_STANDALONE
    const char *group=getenv("NN2_CONTROL_GROUP");
#else
    const char *group=NULL; /* Product suite cannot be filtered by environment. */
#endif
    if (!group || strcmp(group,"R1")==0) {
        RUN_TEST(r1_rejected_init); RUN_TEST(r1_rejected_start); RUN_TEST(r1_expired_stop);
        RUN_TEST(r1_captured_and_pending_start);
    }
    if (!group || strcmp(group,"R2")==0) {
        RUN_TEST(r2_reply_at_deadline); RUN_TEST(r2_reply_after_deadline); RUN_TEST(r2_dispatch_then_reply_expiry);
        RUN_TEST(r2_stale_then_late_matching); RUN_TEST(r2_empty_retry_no_budget_renewal);
        RUN_TEST(r2_read_output_unchanged); RUN_TEST(r2_observation_output_unchanged);
    }
    if (!group || strcmp(group,"positive")==0) {
        RUN_TEST(positive_acquisition); RUN_TEST(positive_start_stop_resynchronization);
        RUN_TEST(positive_partial_lifecycle_failure); RUN_TEST(positive_watchdog_failure);
        RUN_TEST(positive_reply_before_deadline); RUN_TEST(positive_enqueue_expiry);
        RUN_TEST(positive_dispatch_expired); RUN_TEST(positive_stale_then_matching);
        RUN_TEST(positive_empty_read_budget); RUN_TEST(positive_port_outputs);
    }
    /* This positive requires the correction; the legacy first reply is wrongly OK. */
    if (!group) {
        RUN_TEST(positive_consecutive_ids);
        RUN_TEST(event_start_primes_before_ack); RUN_TEST(event_start_captures_early_sync_edge);
        RUN_TEST(event_start_timeout_never_acknowledges_running);
    }
}
#ifdef NN2_CONTROL_STANDALONE
void setUp(void) {}
void tearDown(void) {}
int main(void)
{
    UNITY_BEGIN(); test_ads131_control_suite(); int result=UNITY_END();
    return Unity.NumberOfTests>0u && Unity.TestIgnores==0u && result==0 ? 0 : 1;
}
#endif

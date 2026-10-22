#include "nn2_test.h"
#include "nn2_test_private.h"
#include <limits.h>
#include <string.h>

/* Compare fields, never object padding. */
static nn2_core_sample_t sample(uint32_t n)
{
    nn2_core_sample_t s = { .seq=n, .channel=(uint8_t)(n % 251u),
        .raw=n+123u, .t_led_on_us=UINT64_C(0x100000000)+n,
        .t_sample_us=UINT64_C(0x200000000)+n, .src=NN2_SAMPLE_SRC_FAKE };
    return s;
}
static void same_sample(nn2_core_sample_t a, nn2_core_sample_t b)
{
    TEST_ASSERT_EQUAL_UINT32(a.seq, b.seq);
    TEST_ASSERT_EQUAL_UINT8(a.channel, b.channel);
    TEST_ASSERT_EQUAL_UINT32(a.raw, b.raw);
    TEST_ASSERT_EQUAL_UINT64(a.t_led_on_us, b.t_led_on_us);
    TEST_ASSERT_EQUAL_UINT64(a.t_sample_us, b.t_sample_us);
    TEST_ASSERT_EQUAL(a.src, b.src);
}
static void same_ring(const nn2_core_ring_state_t *a, const nn2_core_ring_state_t *b)
{
    TEST_ASSERT_EQUAL(a->head,b->head); TEST_ASSERT_EQUAL(a->tail,b->tail);
    TEST_ASSERT_EQUAL(a->count,b->count); TEST_ASSERT_EQUAL(a->dropped,b->dropped);
    for (size_t i=0; i<NN2_CORE_RING_CAPACITY; ++i) same_sample(a->items[i],b->items[i]);
}
static void same_ring_handle(const nn2_core_ring_state_t *before, const nn2_core_ring_t *handle)
{
    nn2_core_ring_state_t after=ring_view(handle);
    same_ring(before,&after);
}
static void ring_failure_nonmutation_and_null_queries(void)
{
    DECLARE_RING(r); nn2_core_ring_state_t before;
    nn2_core_sample_t out=sample(77u), sentinel=out;
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_init(NULL,&r));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r)); before=ring_view(r);
    TEST_ASSERT_FALSE(nn2_core_ring_is_full(r));
    TEST_ASSERT_EQUAL(0u,nn2_core_ring_count(NULL));
    TEST_ASSERT_FALSE(nn2_core_ring_is_full(NULL));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_push(NULL,&out));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_push(r,NULL));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_pop(NULL,&out));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_pop(r,NULL));
    TEST_ASSERT_EQUAL(NN2_ERR_EMPTY,nn2_core_ring_pop(r,&out));
    same_sample(sentinel,out); same_ring_handle(&before,r);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_push(r,&out)); before=ring_view(r);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_pop(r,NULL));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_push(r,NULL));
    same_ring_handle(&before,r); same_sample(sentinel,out);
}
static void ring_multielement_wrap_refill_and_repeated_full(void)
{
    DECLARE_RING(r); nn2_core_ring_state_t before; nn2_core_sample_t out;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
    uint32_t next=0u, expected=0u;
    for (unsigned round=0; round<5u; ++round) {
        while (!nn2_core_ring_is_full(r)) {
            nn2_core_sample_t in=sample(next++);
            TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_push(r,&in));
        }
        before=ring_view(r);
        nn2_core_sample_t rejected=sample(999u);
        for (unsigned j=0; j<3u; ++j) {
            TEST_ASSERT_EQUAL(NN2_ERR_FULL,nn2_core_ring_push(r,&rejected));
            ++before.dropped; same_ring_handle(&before,r); same_sample(sample(999u),rejected);
        }
        for (unsigned j=0; j<7u; ++j) {
            TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(r,&out));
            same_sample(sample(expected++),out);
        }
        TEST_ASSERT_EQUAL(NN2_CORE_RING_CAPACITY-7u,ring_view(r).count);
    }
    while (ring_view(r).count) {
        TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(r,&out));
        same_sample(sample(expected++),out);
    }
    TEST_ASSERT_EQUAL(next,expected);
    TEST_ASSERT_EQUAL(ring_view(r).head,ring_view(r).tail);
    TEST_ASSERT_EQUAL(15u,ring_view(r).dropped);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
    TEST_ASSERT_EQUAL(0u,ring_view(r).count); TEST_ASSERT_EQUAL(0u,ring_view(r).head);
    TEST_ASSERT_EQUAL(0u,ring_view(r).tail); TEST_ASSERT_EQUAL(0u,ring_view(r).dropped);
}
static void complete_enum_names_and_invalid_values(void)
{
    const char *names[]={"OK","INVALID_ARG","INVALID_STATE","FULL","EMPTY","IO","NOT_SUPPORTED","TIMEOUT","MISSING","INVALID","NVS_CORRUPT","AUTH_FAIL","CANCELLED","NO_MEM","BUSY","OVERFLOW"};
    for (int i=0;i<NN2_STATUS_COUNT_;++i) TEST_ASSERT_EQUAL_STRING(names[i],nn2_core_status_str((nn2_status_t)i));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",nn2_core_status_str(NN2_STATUS_COUNT_));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",nn2_core_status_str((nn2_status_t)-1));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",nn2_core_status_str((nn2_status_t)1234));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",nn2_core_sample_src_str(NN2_SAMPLE_SRC_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("FAKE",nn2_core_sample_src_str(NN2_SAMPLE_SRC_FAKE));
    TEST_ASSERT_EQUAL_STRING("ADC",nn2_core_sample_src_str(NN2_SAMPLE_SRC_ADC));
    TEST_ASSERT_EQUAL_STRING("INVALID",nn2_core_sample_src_str((nn2_sample_src_t)-1));
    TEST_ASSERT_EQUAL_STRING("INVALID",nn2_core_sample_src_str((nn2_sample_src_t)99));
    const char *states[]={"IDLE","LED_ON","SAMPLE","LED_OFF","FAULT"};
    for (int i=0;i<=NN2_APP_SEQ_FAULT;++i) TEST_ASSERT_EQUAL_STRING(states[i],nn2_app_seq_state_str((nn2_app_seq_state_t)i));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",nn2_app_seq_state_str((nn2_app_seq_state_t)-1));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",nn2_app_seq_state_str((nn2_app_seq_state_t)99));
}

typedef struct {
    char trace[256]; size_t n;
    uint8_t on_ch,off_ch,adc_ch;
    unsigned on,off,reads,delay,logs,fail_off_remaining;
    nn2_status_t on_error,adc_error,off_error;
    uint64_t now; uint32_t raw;
} model_t;
static model_t null_context_model;
static model_t *model(void *ctx) { return ctx ? ctx : &null_context_model; }
static void record(model_t *m,char c)
{
    if (m->n+1u<sizeof(m->trace)) { m->trace[m->n++]=c; m->trace[m->n]='\0'; }
}
static nn2_status_t on(void *ctx,uint8_t ch)
{
    model_t *m=model(ctx); record(m,'N'); ++m->on; m->on_ch=ch; return m->on_error;
}
static nn2_status_t off(void *ctx,uint8_t ch)
{
    model_t *m=model(ctx); record(m,'F'); ++m->off; m->off_ch=ch;
    if (m->fail_off_remaining) { --m->fail_off_remaining; return m->off_error; }
    return NN2_OK;
}
static nn2_status_t adc(void *ctx,uint8_t ch,uint32_t *out)
{
    model_t *m=model(ctx); record(m,'A'); ++m->reads; m->adc_ch=ch;
    if (m->adc_error) return m->adc_error;
    *out=m->raw++; return NN2_OK;
}
static uint64_t now(void *ctx)
{
    model_t *m=model(ctx); record(m,'T'); m->now+=10u; return m->now;
}
static void delay(void *ctx,uint32_t us)
{
    model_t *m=model(ctx); record(m,'D'); ++m->delay; m->now+=us;
}
static void log_msg(void *ctx,nn2_log_level_t level,const char *msg)
{
    model_t *m=model(ctx); (void)level; (void)msg; ++m->logs;
}
static nn2_app_seq_config_t config(model_t *m,nn2_core_ring_t *r)
{
    nn2_app_seq_config_t c={ .led={on,off,m}, .adc={adc,m,NN2_SAMPLE_SRC_FAKE},
        .clock={now,delay,m}, .log={log_msg,m}, .ring=r, .channel=9u, .settle_us=73u };
    return c;
}
static void initialize(model_t *m,nn2_core_ring_storage_t *rs,nn2_core_ring_t **r,
    nn2_app_seq_storage_t *ss,nn2_app_sequencer_t **s)
{
    memset(m,0,sizeof(*m)); m->raw=1234u;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(rs,r));
    nn2_app_seq_config_t c=config(m,*r);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(ss,s,&c));
}
static void seq_each_missing_mandatory_and_null_operation(void)
{
    model_t m; DECLARE_RING(r); DECLARE_SEQ(s);
    initialize(&m,&r_storage,&r,&s_storage,&s);
    nn2_app_seq_config_t c=config(&m,r),bad;
    for (int i=0;i<6;++i) {
        bad=c;
        switch(i) {
        case 0: bad.led.on=NULL; break; case 1: bad.led.off=NULL; break;
        case 2: bad.adc.read=NULL; break; case 3: bad.clock.now_us=NULL; break;
        case 4: bad.ring=NULL; break; default: break;
        }
        TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_init(&s_storage,&s,i==5?NULL:&bad));
        TEST_ASSERT_EQUAL(NN2_APP_SEQ_IDLE,seq_view(s).state);
        TEST_ASSERT_EQUAL(1234u,m.raw); TEST_ASSERT_EQUAL(0u,m.on);
    }
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_init(NULL,&s,&c));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_step(NULL));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_run_cycle(NULL));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_reset(NULL));
    TEST_ASSERT_EQUAL(NN2_APP_SEQ_FAULT,nn2_app_sequencer_state(NULL));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(s));
}
static void seq_exact_order_channel_fields_and_publication(void)
{
    model_t m; DECLARE_RING(r); DECLARE_SEQ(s); nn2_core_sample_t out;
    initialize(&m,&r_storage,&r,&s_storage,&s);
    for (unsigned i=0;i<3u;++i) {
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_step(s));
        TEST_ASSERT_EQUAL(0u,ring_view(r).count);
    }
    TEST_ASSERT_EQUAL_STRING("NTDAT",m.trace);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_STATE,nn2_app_sequencer_run_cycle(s));
    TEST_ASSERT_EQUAL(0u,m.off);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_step(s));
    TEST_ASSERT_EQUAL_STRING("NTDATF",m.trace);
    TEST_ASSERT_EQUAL(1u,ring_view(r).count); TEST_ASSERT_EQUAL(1u,seq_view(s).cycles_ok);
    TEST_ASSERT_EQUAL(9u,m.on_ch); TEST_ASSERT_EQUAL(9u,m.off_ch); TEST_ASSERT_EQUAL(9u,m.adc_ch);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(r,&out));
    TEST_ASSERT_EQUAL(0u,out.seq); TEST_ASSERT_EQUAL(1234u,out.raw);
    TEST_ASSERT_EQUAL(9u,out.channel); TEST_ASSERT_EQUAL(NN2_SAMPLE_SRC_FAKE,out.src);
    TEST_ASSERT_EQUAL_UINT64(10u,out.t_led_on_us); TEST_ASSERT_EQUAL_UINT64(93u,out.t_sample_us);
}
static void seq_optional_callbacks_zero_wait_and_null_contexts(void)
{
    DECLARE_RING(r); DECLARE_SEQ(s);
    for (unsigned option=0;option<3u;++option) {
        memset(&null_context_model,0,sizeof(null_context_model));
        TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
        nn2_app_seq_config_t c=config(NULL,r);
        if(option!=2u) c.log.write=NULL;
        if(option==0u) c.clock.delay_us=NULL;
        if(option==1u) c.settle_us=0u;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&s_storage,&s,&c));
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(s));
        TEST_ASSERT_EQUAL(option==2u?1u:0u,null_context_model.delay);
        /* Optional logging is also safe on error. */
        null_context_model.adc_error=NN2_ERR_TIMEOUT;
        TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,nn2_app_sequencer_run_cycle(s));
        TEST_ASSERT_EQUAL(option==2u?1u:0u,null_context_model.logs);
    }
}
static void assert_fault_terminal(nn2_app_sequencer_t *s,model_t *m)
{
    unsigned before=m->on;
    for(unsigned i=0;i<3u;++i) {
        TEST_ASSERT_EQUAL(NN2_ERR_INVALID_STATE,nn2_app_sequencer_step(s));
        TEST_ASSERT_EQUAL(NN2_ERR_INVALID_STATE,nn2_app_sequencer_run_cycle(s));
    }
    TEST_ASSERT_EQUAL(before,m->on);
}
static void seq_primary_errors_cleanup_retry_and_recovery(void)
{
    for(unsigned phase=0;phase<3u;++phase) {
        model_t m; DECLARE_RING(r); DECLARE_SEQ(s); nn2_core_sample_t out;
        initialize(&m,&r_storage,&r,&s_storage,&s);
        if(phase==0u) m.on_error=NN2_ERR_TIMEOUT;
        if(phase==1u) m.adc_error=NN2_ERR_TIMEOUT;
        m.off_error=NN2_ERR_IO; m.fail_off_remaining=phase==2u?1u:3u;
        TEST_ASSERT_EQUAL(phase==2u?NN2_ERR_IO:NN2_ERR_TIMEOUT,nn2_app_sequencer_run_cycle(s));
        TEST_ASSERT_EQUAL(phase==2u?NN2_ERR_IO:NN2_ERR_TIMEOUT,seq_view(s).last_error);
        TEST_ASSERT_EQUAL(phase==2u?0u:1u,seq_view(s).off_failures);
        TEST_ASSERT_EQUAL(phase==2u?2u:1u,m.off);
        TEST_ASSERT_EQUAL(0u,ring_view(r).count); TEST_ASSERT_EQUAL(0u,seq_view(s).cycles_ok);
        TEST_ASSERT_EQUAL(0u,seq_view(s).next_seq); TEST_ASSERT_EQUAL(1u,seq_view(s).faults);
        assert_fault_terminal(s,&m);
        if(phase!=2u) {
            for(unsigned i=0;i<2u;++i) {
                TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_app_sequencer_reset(s));
                TEST_ASSERT_EQUAL(NN2_ERR_IO,seq_view(s).last_error);
                assert_fault_terminal(s,&m);
            }
            TEST_ASSERT_EQUAL(3u,seq_view(s).off_failures); TEST_ASSERT_EQUAL(1u,seq_view(s).faults);
        }
        m.on_error=NN2_OK; m.adc_error=NN2_OK; m.raw=9999u;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_reset(s));
        TEST_ASSERT_EQUAL(NN2_OK,seq_view(s).last_error); TEST_ASSERT_EQUAL(NN2_APP_SEQ_IDLE,seq_view(s).state);
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(s));
        TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(r,&out));
        TEST_ASSERT_EQUAL(9999u,out.raw); TEST_ASSERT_EQUAL(0u,out.seq);
        TEST_ASSERT_EQUAL(0u,ring_view(r).count); TEST_ASSERT_EQUAL(1u,seq_view(s).cycles_ok);
        TEST_ASSERT_EQUAL(1u,seq_view(s).faults);
    }
}
static void seq_full_fifo_cycle_and_unsigned_overflow(void)
{
    model_t m; DECLARE_RING(r); DECLARE_SEQ(s); nn2_core_sample_t out;
    initialize(&m,&r_storage,&r,&s_storage,&s);
    for(unsigned i=0;i<NN2_CORE_RING_CAPACITY+3u;++i)
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(s));
    TEST_ASSERT_EQUAL(NN2_CORE_RING_CAPACITY,ring_view(r).count); TEST_ASSERT_EQUAL(3u,ring_view(r).dropped);
    TEST_ASSERT_EQUAL(NN2_CORE_RING_CAPACITY+3u,seq_view(s).cycles_ok); TEST_ASSERT_EQUAL(0u,seq_view(s).faults);
    for(unsigned i=0;i<NN2_CORE_RING_CAPACITY;++i) {
        TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(r,&out));
        TEST_ASSERT_EQUAL(i,out.seq); TEST_ASSERT_EQUAL(1234u+i,out.raw);
        TEST_ASSERT_EQUAL_UINT64(10u+93u*i,out.t_led_on_us);
        TEST_ASSERT_EQUAL_UINT64(93u+93u*i,out.t_sample_us);
    }
    nn2_app_seq_internal_t boundary=seq_view(s);
    boundary.next_seq=UINT32_MAX; boundary.cycles_ok=UINT32_MAX;
    nn2_app_seq_save(s,&boundary);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(s));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(r,&out));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,out.seq);
    TEST_ASSERT_EQUAL(0u,seq_view(s).next_seq); TEST_ASSERT_EQUAL(0u,seq_view(s).cycles_ok);
}
static void seq_init_copies_config_and_reinit_at_quiescence(void)
{
    model_t m={0}; DECLARE_RING(r); DECLARE_SEQ(s);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
    nn2_app_seq_config_t c=config(&m,r);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&s_storage,&s,&c));
    c.channel=77u; c.led.on=NULL;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(s));
    TEST_ASSERT_EQUAL(9u,m.on_ch);
    c=config(&m,r);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&s_storage,&s,&c));
    TEST_ASSERT_EQUAL(0u,seq_view(s).cycles_ok); TEST_ASSERT_EQUAL(0u,seq_view(s).next_seq);
    TEST_ASSERT_EQUAL(1u,ring_view(r).count); /* Ring lifetime is independent. */
}
void test_contract_suite(void)
{
    RUN_TEST(ring_failure_nonmutation_and_null_queries);
    RUN_TEST(ring_multielement_wrap_refill_and_repeated_full);
    RUN_TEST(complete_enum_names_and_invalid_values);
    RUN_TEST(seq_each_missing_mandatory_and_null_operation);
    RUN_TEST(seq_exact_order_channel_fields_and_publication);
    RUN_TEST(seq_optional_callbacks_zero_wait_and_null_contexts);
    RUN_TEST(seq_primary_errors_cleanup_retry_and_recovery);
    RUN_TEST(seq_full_fifo_cycle_and_unsigned_overflow);
    RUN_TEST(seq_init_copies_config_and_reinit_at_quiescence);
}

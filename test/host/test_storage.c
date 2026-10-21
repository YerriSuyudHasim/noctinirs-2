#include "nn2_test.h"
#include "nn2_test_private.h"
#include <stdio.h>
#include <string.h>

static nn2_status_t noop(void *ctx, uint8_t channel)
{ (void)ctx; (void)channel; return NN2_OK; }
static nn2_status_t read_value(void *ctx, uint8_t channel, uint32_t *out)
{ (void)ctx; *out=channel; return NN2_OK; }
static uint64_t read_time(void *ctx) { (void)ctx; return 7u; }
static nn2_status_t off_error(void *ctx, uint8_t channel)
{ (void)ctx; (void)channel; return NN2_ERR_IO; }
static nn2_app_seq_config_t storage_config(nn2_core_ring_t *ring)
{
    nn2_app_seq_config_t cfg={ .led={noop,noop,NULL},
        .adc={read_value,NULL,NN2_SAMPLE_SRC_FAKE}, .clock={read_time,NULL,NULL},
        .ring=ring, .channel=3u };
    return cfg;
}
static void ring_storage_alignment_array_independence_and_reinit(void)
{
    static nn2_core_ring_storage_t storage[2];
    nn2_core_ring_t *a=NULL,*b=NULL;
    TEST_ASSERT_EQUAL(0u,(uintptr_t)&storage[0] % _Alignof(nn2_core_ring_t));
    TEST_ASSERT_EQUAL(0u,(uintptr_t)&storage[1] % _Alignof(nn2_core_ring_t));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&storage[0],&a));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&storage[1],&b));
    TEST_ASSERT_TRUE(a!=b);
    nn2_core_sample_t sample={.raw=42u},out={0};
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_push(a,&sample));
    TEST_ASSERT_EQUAL(NN2_ERR_EMPTY,nn2_core_ring_pop(b,&out));
    TEST_ASSERT_EQUAL(1u,nn2_core_ring_count(a));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&storage[0],&a));
    TEST_ASSERT_EQUAL(0u,nn2_core_ring_count(a));
    TEST_ASSERT_EQUAL(0u,nn2_core_ring_dropped(a));
    TEST_ASSERT_EQUAL(0u,nn2_core_ring_dropped(NULL));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_push(b,&sample));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(b,&out));
    TEST_ASSERT_EQUAL(42u,out.raw);
}
static void opaque_init_errors_preserve_storage_and_output_handles(void)
{
    DECLARE_RING(r);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
    nn2_core_ring_t *saved=r;
    unsigned char before[NN2_CORE_RING_STORAGE_SIZE];
    memcpy(before,r_storage.opaque,sizeof(before));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_init(NULL,&r));
    TEST_ASSERT_EQUAL_PTR(saved,r);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_core_ring_init(&r_storage,NULL));
    TEST_ASSERT_EQUAL_MEMORY(before,r_storage.opaque,sizeof(before));
    DECLARE_SEQ(s);
    nn2_app_seq_config_t cfg=storage_config(r);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&s_storage,&s,&cfg));
    nn2_app_sequencer_t *seq_saved=s;
    unsigned char seq_before[NN2_APP_SEQ_STORAGE_SIZE];
    memcpy(seq_before,s_storage.opaque,sizeof(seq_before));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_init(NULL,&s,&cfg));
    TEST_ASSERT_EQUAL_PTR(seq_saved,s);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_init(&s_storage,NULL,&cfg));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_init(&s_storage,&s,NULL));
    TEST_ASSERT_EQUAL_PTR(seq_saved,s);
    TEST_ASSERT_EQUAL_MEMORY(seq_before,s_storage.opaque,sizeof(seq_before));
}
static void seq_copied_observations_and_separate_caller_storage(void)
{
    struct owner { char prefix; nn2_app_seq_storage_t storage[2]; } owner;
    DECLARE_RING(r);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
    nn2_app_seq_config_t cfg=storage_config(r);
    nn2_app_sequencer_t *a=NULL,*b=NULL;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&owner.storage[0],&a,&cfg));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&owner.storage[1],&b,&cfg));
    TEST_ASSERT_EQUAL(0u,(uintptr_t)&owner.storage[1] % _Alignof(nn2_app_sequencer_t));
    /* Same ring is used sequentially only; distinct object lifetimes/counters. */
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(a));
    nn2_app_seq_observation_t observation;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_observe(a,&observation));
    TEST_ASSERT_EQUAL(1u,observation.cycles_ok);
    observation.cycles_ok=999u; observation.state=NN2_APP_SEQ_FAULT;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_observe(a,&observation));
    TEST_ASSERT_EQUAL(1u,observation.cycles_ok);
    TEST_ASSERT_EQUAL(NN2_APP_SEQ_IDLE,observation.state);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_observe(b,&observation));
    TEST_ASSERT_EQUAL(0u,observation.cycles_ok);
    observation.last_error=NN2_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_observe(NULL,&observation));
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,observation.last_error);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG,nn2_app_sequencer_observe(a,NULL));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_reset(a));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&owner.storage[0],&a,&cfg));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_observe(a,&observation));
    TEST_ASSERT_EQUAL(0u,observation.cycles_ok);
    TEST_ASSERT_EQUAL(1u,nn2_core_ring_count(r));
    printf("Storage bytes/alignment: ring=%zu/%zu internal=%zu; seq=%zu/%zu internal=%zu\n",
        sizeof(r_storage),_Alignof(nn2_core_ring_storage_t),sizeof(nn2_core_ring_state_t),
        sizeof(owner.storage[0]),_Alignof(nn2_app_seq_storage_t),sizeof(nn2_app_seq_internal_t));
}
static void automatic_storage_repeated_lifetimes(void)
{
    for(unsigned i=0;i<8u;++i) {
        /* Each block constructs ordinary C storage; all aliases expire before exit. */
        DECLARE_RING(r); DECLARE_SEQ(s);
        TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
        nn2_app_seq_config_t cfg=storage_config(r);
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&s_storage,&s,&cfg));
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_run_cycle(s));
        nn2_core_sample_t out;
        TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_pop(r,&out));
        TEST_ASSERT_EQUAL(0u,out.seq); TEST_ASSERT_EQUAL(3u,out.raw);
        TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_reset(s));
        s=NULL; r=NULL; /* No destructor/heap; no aliases escape this scope. */
    }
}
static void public_observation_copies_fault_and_cleanup_counters(void)
{
    DECLARE_RING(r); DECLARE_SEQ(s);
    TEST_ASSERT_EQUAL(NN2_OK,nn2_core_ring_init(&r_storage,&r));
    nn2_app_seq_config_t cfg=storage_config(r);
    cfg.led.off=off_error;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_init(&s_storage,&s,&cfg));
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_app_sequencer_run_cycle(s));
    nn2_app_seq_observation_t out;
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_observe(s,&out));
    TEST_ASSERT_EQUAL(NN2_APP_SEQ_FAULT,out.state);
    TEST_ASSERT_EQUAL(NN2_ERR_IO,out.last_error);
    TEST_ASSERT_EQUAL(1u,out.faults); TEST_ASSERT_EQUAL(1u,out.off_failures);
    TEST_ASSERT_EQUAL(0u,out.cycles_ok); TEST_ASSERT_EQUAL(0u,out.next_seq);
    TEST_ASSERT_EQUAL(NN2_ERR_IO,nn2_app_sequencer_reset(s));
    TEST_ASSERT_EQUAL(NN2_OK,nn2_app_sequencer_observe(s,&out));
    TEST_ASSERT_EQUAL(2u,out.off_failures); TEST_ASSERT_EQUAL(1u,out.faults);
}
void test_storage_suite(void)
{
    RUN_TEST(ring_storage_alignment_array_independence_and_reinit);
    RUN_TEST(opaque_init_errors_preserve_storage_and_output_handles);
    RUN_TEST(seq_copied_observations_and_separate_caller_storage);
    RUN_TEST(automatic_storage_repeated_lifetimes);
    RUN_TEST(public_observation_copies_fault_and_cleanup_counters);
}

/* V4-7.2 handoff adapted to NN2 / IDF 6.1. Included by the LED adapter:
 * one interlock protects deadline identity, cancellation and emitter reset.
 * Priority 21 owns deadline dispatch and completion/window publication;
 * priority 22 owns I2C and never waits for the scheduler. No allocation here. */
#define NN2_SCHED_DEADLINES 8u
#define NN2_SCHED_COMPLETIONS 4u
typedef struct {
    uint64_t alarm;
    uint32_t generation, id, phase, frame;
} led_deadline_t;
typedef struct {
    led_deadline_t edge;
    uint64_t begin, end, commit;
    nn2_status_t result;
    esp_err_t count_result;
} led_completion_t;
static TaskHandle_t s_scheduler_task;
static led_deadline_t s_deadlines[NN2_SCHED_DEADLINES], s_edge;
static led_completion_t s_completions[NN2_SCHED_COMPLETIONS];
static uint32_t s_deadline_head, s_deadline_tail, s_completion_head, s_completion_tail;
static uint32_t s_next_phase, s_next_frame, s_next_command;
static bool s_edge_pending, s_scheduler_processing;
_Static_assert(sizeof(s_deadlines)+sizeof(s_completions)+sizeof(s_edge)<=1024u,
    "Scheduler handoff storage exceeds 1 KiB");
static void scheduler_commit(const led_completion_t *done);
/* Caller holds the interlock; reset only while the previous timer is stopped. */
static void scheduler_reset_locked(void)
{
    s_deadline_head=s_deadline_tail=s_completion_head=s_completion_tail=0u;
    s_next_phase=s_next_frame=s_next_command=0u;
    s_edge_pending=false;
}
static void scheduler_step(void)
{
    LED_ENTER(); s_scheduler_processing=true; LED_EXIT();
    /* Completions precede deadlines: a completed prior edge must be accounted
     * before dispatching its successor. Never wait synchronously for I2C. */
    for (;;) {
        led_completion_t done;
        LED_ENTER();
        bool have=s_completion_tail!=s_completion_head;
        if (have) done=s_completions[s_completion_tail++ % NN2_SCHED_COMPLETIONS];
        bool active=have && s_armed && done.edge.generation==s_generation;
        bool match=active && s_busy && done.edge.id==s_edge.id &&
            done.edge.phase==s_edge.phase && done.edge.frame==s_edge.frame;
        if (active && !match) stop_locked(STOP_IO,ESP_ERR_INVALID_STATE);
        LED_EXIT();
        if (!have) break;
        if (match) scheduler_commit(&done);
    }
    for (;;) {
        led_deadline_t edge;
        LED_ENTER();
        bool have=s_deadline_tail!=s_deadline_head;
        if (have) edge=s_deadlines[s_deadline_tail++ % NN2_SCHED_DEADLINES];
        bool active=have && s_armed && edge.generation==s_generation &&
            s_status.state==NN2_OPTICAL_RUNNING;
        bool dispatch=active && !s_busy;
        if (dispatch) {
            s_edge=edge; s_phase=edge.phase; s_alarm=edge.alarm;
            s_busy=true; s_edge_pending=true;
        } else if (active) {
            ++s_status.deadline_misses;
            stop_locked(STOP_DEADLINE,ESP_ERR_TIMEOUT);
        }
        LED_EXIT();
        if (!have) break;
        if (dispatch) xTaskNotifyGive(s_task);
    }
    LED_ENTER();
    s_scheduler_processing=false;
    bool stopped=!s_armed && !s_status.bus_released;
    LED_EXIT();
    if (stopped && s_task) xTaskNotifyGive(s_task);
}
static void scheduler_task(void *ctx)
{
    (void)ctx;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        scheduler_step();
    }
}
static void scheduler_complete(const led_completion_t *done)
{
    LED_ENTER();
    if (s_armed && done->edge.generation==s_generation) {
        if (s_completion_head-s_completion_tail==NN2_SCHED_COMPLETIONS)
            stop_locked(STOP_IO,ESP_ERR_NO_MEM);
        else s_completions[s_completion_head++ % NN2_SCHED_COMPLETIONS]=*done;
    }
    LED_EXIT();
    if (s_scheduler_task) xTaskNotifyGive(s_scheduler_task);
}

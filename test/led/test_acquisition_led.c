/* Production LED window publication with deterministic fake completion times. */
#define CONFIG_NN2_OPTICAL_ACQUISITION 1
#define main original_led_main
#include "test_led.c"
#undef main
static nn2_core_acq_window_t captured[80];
static unsigned published;
static bool window_full;
int64_t esp_timer_get_time(void) { return (int64_t)(1000000u+clock_count); }
bool nn2_acquisition_window(const nn2_core_acq_window_t *w)
{
    if (window_full || published>=80) return false;
    captured[published++]=*w;return true;
}
int main(void)
{
    CHECK(nn2_led_bringup_init()==ESP_OK);
    begin(10);
    for(unsigned phase=0;phase<40;++phase) alarm_tick(true);
    CHECK(published==40);
    for(unsigned phase=0;phase<40;++phase) {
        CHECK(captured[phase].phase==phase && captured[phase].frame==0);
        CHECK(captured[phase].commit_us<captured[phase].end_us);
        CHECK(captured[phase].origin_us==1002500u);
        CHECK(captured[phase].slot_start_us==1002500u+(phase/2u)*5000u);
        CHECK(captured[phase].end_us==1002500u+(phase+1u)*2500u-100u);
    }
    alarm_tick(true);CHECK(captured[40].frame==1 && captured[40].phase==0);
    (void)cancel(NULL);owner_step();restore();
    nn2_port_optical_config_t cfg=configuration(3);
    CHECK(start(NULL,&cfg)==NN2_ERR_INVALID_ARG);
    cfg=configuration(1);CHECK(start(NULL,&cfg)==NN2_ERR_INVALID_ARG);
    begin(10);window_full=true;alarm_tick(true);
    CHECK(reset_low && state(NULL)==NN2_OPTICAL_FAULT);
    window_full=false;restore();published=0;
    begin(10);cancel_on_wait=true;defer_completion=true;alarm_tick(true);
    CHECK(reset_low && published==0);
    restore();
    printf("%u acquisition LED checks, %u failures\n",checks,failures);
    return failures?1:0;
}

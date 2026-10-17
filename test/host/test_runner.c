#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_cred_validate_bounds(void);
void test_cred_store_round_trip_hides_password_from_log(void);
void test_invalid_console_input_is_not_saved(void);
void test_corrupt_store_reprovisions_without_keeping_old_secret(void);
void test_console_text_is_not_an_emitter_command(void);
void test_valid_store_skips_console(void);
void test_backoff_grows_then_caps(void);
void test_backoff_rejects_bad_config(void);
void test_sta_loss_schedules_reconnect_and_does_not_stop_or_command_emitters(void);
void test_dropped_link_event_still_schedules_reconnect(void);
void test_reconnect_failure_reschedules(void);
void test_slow_attempt_does_not_reconnect_while_in_flight(void);
void test_connect_guard_timeout_schedules_reconnect(void);
void test_disconnect_clears_connect_in_flight(void);
void test_link_up_is_report_only(void);
void test_rtos_config_pins_console_and_wifi_to_core_0(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cred_validate_bounds);
    RUN_TEST(test_cred_store_round_trip_hides_password_from_log);
    RUN_TEST(test_invalid_console_input_is_not_saved);
    RUN_TEST(test_corrupt_store_reprovisions_without_keeping_old_secret);
    RUN_TEST(test_console_text_is_not_an_emitter_command);
    RUN_TEST(test_valid_store_skips_console);
    RUN_TEST(test_backoff_grows_then_caps);
    RUN_TEST(test_backoff_rejects_bad_config);
    RUN_TEST(test_sta_loss_schedules_reconnect_and_does_not_stop_or_command_emitters);
    RUN_TEST(test_dropped_link_event_still_schedules_reconnect);
    RUN_TEST(test_reconnect_failure_reschedules);
    RUN_TEST(test_slow_attempt_does_not_reconnect_while_in_flight);
    RUN_TEST(test_connect_guard_timeout_schedules_reconnect);
    RUN_TEST(test_disconnect_clears_connect_in_flight);
    RUN_TEST(test_link_up_is_report_only);
    RUN_TEST(test_rtos_config_pins_console_and_wifi_to_core_0);
    return UNITY_END();
}

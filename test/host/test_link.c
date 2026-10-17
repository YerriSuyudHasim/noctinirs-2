#include "nn2_app_wifi.h"
#include "unity.h"

#include <string.h>

static int g_safe_off_calls;
static int g_led_on_calls;
static int g_led_off_calls;
static int g_stop_stream_calls;
static int g_schedule_calls;
static int g_other_station_calls;

static nn2_status_t spy_schedule(void *ctx)
{
    (void)ctx;
    g_schedule_calls++;
    return NN2_OK;
}

static nn2_status_t spy_other(void *ctx)
{
    (void)ctx;
    g_other_station_calls++;
    return NN2_ERR_IO;
}

static nn2_status_t spy_other_evt(void *ctx, nn2_wifi_sta_evt_t *evt, uint32_t timeout_ms)
{
    (void)ctx;
    (void)evt;
    (void)timeout_ms;
    g_other_station_calls++;
    return NN2_ERR_IO;
}

static nn2_status_t spy_other_status(void *ctx, nn2_wifi_sta_status_t *out)
{
    (void)ctx;
    (void)out;
    g_other_station_calls++;
    return NN2_ERR_IO;
}

static nn2_status_t spy_other_connect(void *ctx, const nn2_wifi_credentials_t *creds)
{
    (void)ctx;
    (void)creds;
    g_other_station_calls++;
    return NN2_ERR_IO;
}

static void reset_spies(void)
{
    g_safe_off_calls = 0;
    g_led_on_calls = 0;
    g_led_off_calls = 0;
    g_stop_stream_calls = 0;
    g_schedule_calls = 0;
    g_other_station_calls = 0;
}

static void bind_station(nn2_port_wifi_station_t *station)
{
    memset(station, 0, sizeof(*station));
    station->connect = spy_other_connect;
    station->disconnect = spy_other;
    station->get_status = spy_other_status;
    station->schedule_reconnect = spy_schedule;
    station->reconnect_now = spy_other;
    station->wait_event = spy_other_evt;
}

void test_sta_loss_schedules_reconnect_and_does_not_stop_or_command_emitters(void)
{
    nn2_port_wifi_station_t station;
    nn2_wifi_sta_status_t status;

    reset_spies();
    bind_station(&station);
    memset(&status, 0, sizeof(status));
    status.link = NN2_WIFI_LINK_UP;
    status.ipv4_addr = 0x0100a8c0u;

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_on_link_event(&station, NN2_WIFI_LINK_DOWN, &status));
    TEST_ASSERT_EQUAL(NN2_WIFI_LINK_DOWN, status.link);
    TEST_ASSERT_EQUAL_UINT32(0u, status.ipv4_addr);
    TEST_ASSERT_EQUAL(1, g_schedule_calls);
    TEST_ASSERT_EQUAL(0, g_other_station_calls);
    TEST_ASSERT_EQUAL(0, g_safe_off_calls);
    TEST_ASSERT_EQUAL(0, g_led_on_calls);
    TEST_ASSERT_EQUAL(0, g_led_off_calls);
    TEST_ASSERT_EQUAL(0, g_stop_stream_calls);
}

void test_dropped_link_event_still_schedules_reconnect(void)
{
    nn2_port_wifi_station_t station;
    nn2_wifi_sta_status_t status;

    reset_spies();
    bind_station(&station);

    /* LINK_DOWN was not delivered. The queue overflowed, the link is down,
       a connection is still wanted, and no backoff timer is armed. */
    memset(&status, 0, sizeof(status));
    status.link = NN2_WIFI_LINK_DOWN;
    status.want_connected = true;
    status.reconnect_timer_armed = false;
    status.event_overflow_count = 1u;
    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_on_wait_timeout(&station, &status));
    TEST_ASSERT_EQUAL(1, g_schedule_calls);

    /* RECONNECT_DUE was dropped after the one-shot timer fired. The timer
       is no longer armed, so the next poll must schedule again. */
    reset_spies();
    status.event_overflow_count = 2u;
    status.reconnect_timer_armed = false;
    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_on_wait_timeout(&station, &status));
    TEST_ASSERT_EQUAL(1, g_schedule_calls);

    /* An armed timer already owns the next attempt. */
    reset_spies();
    status.reconnect_timer_armed = true;
    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_on_wait_timeout(&station, &status));
    TEST_ASSERT_EQUAL(0, g_schedule_calls);
    TEST_ASSERT_EQUAL(0, g_other_station_calls);
    TEST_ASSERT_EQUAL(0, g_safe_off_calls);
    TEST_ASSERT_EQUAL(0, g_stop_stream_calls);
}

void test_reconnect_failure_reschedules(void)
{
    nn2_port_wifi_station_t station;

    reset_spies();
    bind_station(&station);

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_on_reconnect_result(&station, NN2_ERR_IO));
    TEST_ASSERT_EQUAL(1, g_schedule_calls);
    TEST_ASSERT_EQUAL(0, g_other_station_calls);
    TEST_ASSERT_EQUAL(0, g_safe_off_calls);
    TEST_ASSERT_EQUAL(0, g_led_on_calls);
    TEST_ASSERT_EQUAL(0, g_stop_stream_calls);

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_on_reconnect_result(&station, NN2_OK));
    TEST_ASSERT_EQUAL(1, g_schedule_calls);
}

void test_link_up_is_report_only(void)
{
    nn2_port_wifi_station_t station;
    nn2_wifi_sta_status_t status;

    reset_spies();
    bind_station(&station);
    memset(&status, 0, sizeof(status));
    status.link = NN2_WIFI_LINK_DOWN;

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_on_link_event(&station, NN2_WIFI_LINK_UP, &status));
    TEST_ASSERT_EQUAL(NN2_WIFI_LINK_UP, status.link);
    TEST_ASSERT_EQUAL(0, g_schedule_calls);
    TEST_ASSERT_EQUAL(0, g_other_station_calls);
    TEST_ASSERT_EQUAL(0, g_safe_off_calls);
    TEST_ASSERT_EQUAL(0, g_led_off_calls);
    TEST_ASSERT_EQUAL(0, g_stop_stream_calls);
    TEST_ASSERT_EQUAL(0, g_led_on_calls);
}

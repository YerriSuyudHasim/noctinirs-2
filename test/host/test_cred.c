#include "nn2_app_wifi.h"
#include "test_support.h"
#include "unity.h"

#include <string.h>

#define FIXTURE_SSID "unit-test-ssid"
#define FIXTURE_PASS "unitpass"

typedef struct {
    nn2_wifi_credentials_t slot;
    int occupied;
    int corrupt;
    int save_calls;
    int clear_calls;
    char log[160];
} fake_store_t;

typedef struct {
    nn2_wifi_credentials_t preset;
    nn2_status_t result;
    int reads;
    char log[64];
} fake_console_t;

typedef struct {
    int connect_calls;
    int led_on_calls;
    int stop_stream_calls;
    char log[96];
} fake_station_spy_t;

static void log_append(char *log, size_t cap, const char *text)
{
    size_t used;
    size_t add;

    used = strlen(log);
    add = strlen(text);
    if (used + add + 1u > cap) {
        return;
    }
    memcpy(log + used, text, add + 1u);
}

static nn2_status_t fake_load(void *ctx, nn2_wifi_credentials_t *out)
{
    fake_store_t *store = ctx;

    if (store->corrupt) {
        return NN2_ERR_NVS_CORRUPT;
    }
    if (!store->occupied) {
        return NN2_ERR_MISSING;
    }
    *out = store->slot;
    return NN2_OK;
}

static nn2_status_t fake_save(void *ctx, const nn2_wifi_credentials_t *in)
{
    fake_store_t *store = ctx;

    store->save_calls++;
    store->slot = *in;
    store->occupied = 1;
    store->corrupt = 0;
    log_append(store->log, sizeof(store->log), "save:");
    log_append(store->log, sizeof(store->log), in->ssid);
    log_append(store->log, sizeof(store->log), ";");
    return NN2_OK;
}

static nn2_status_t fake_clear(void *ctx)
{
    fake_store_t *store = ctx;

    store->clear_calls++;
    nn2_wifi_cred_wipe(&store->slot);
    store->occupied = 0;
    store->corrupt = 0;
    log_append(store->log, sizeof(store->log), "clear;");
    return NN2_OK;
}

static nn2_status_t fake_read(void *ctx, nn2_wifi_credentials_t *out, uint32_t timeout_ms)
{
    fake_console_t *console = ctx;

    (void)timeout_ms;
    console->reads++;
    log_append(console->log, sizeof(console->log), "read;");
    if (console->result != NN2_OK) {
        return console->result;
    }
    *out = console->preset;
    return NN2_OK;
}

static nn2_status_t fake_connect(void *ctx, const nn2_wifi_credentials_t *creds)
{
    fake_station_spy_t *spy = ctx;

    spy->connect_calls++;
    log_append(spy->log, sizeof(spy->log), "connect:");
    log_append(spy->log, sizeof(spy->log), creds->ssid);
    log_append(spy->log, sizeof(spy->log), ";");
    return NN2_OK;
}

static void bind_store(nn2_port_wifi_cred_store_t *port, fake_store_t *store)
{
    memset(port, 0, sizeof(*port));
    port->load = fake_load;
    port->save = fake_save;
    port->clear = fake_clear;
    port->ctx = store;
}

static void bind_console(nn2_port_prov_console_t *port, fake_console_t *console)
{
    memset(port, 0, sizeof(*port));
    port->read_once = fake_read;
    port->ctx = console;
}

void test_cred_validate_bounds(void)
{
    nn2_wifi_credentials_t cred;
    char ssid32[NN2_WIFI_SSID_MAX + 1u];
    char pass64[NN2_WIFI_PASS_MAX + 1u];
    char ssid33[NN2_WIFI_SSID_MAX + 2u];

    memset(ssid32, 's', sizeof(ssid32));
    ssid32[NN2_WIFI_SSID_MAX] = '\0';
    memset(pass64, 'p', sizeof(pass64));
    pass64[NN2_WIFI_PASS_MAX] = '\0';
    memset(ssid33, 's', sizeof(ssid33));
    ssid33[NN2_WIFI_SSID_MAX + 1u] = '\0';

    test_fill_cred(&cred, FIXTURE_SSID, FIXTURE_PASS);
    TEST_ASSERT_EQUAL(NN2_OK, nn2_wifi_cred_validate(&cred));

    test_fill_cred(&cred, ssid32, pass64);
    TEST_ASSERT_EQUAL(NN2_OK, nn2_wifi_cred_validate(&cred));

    test_fill_cred(&cred, "", FIXTURE_PASS);
    cred.ssid_len = 0u;
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID, nn2_wifi_cred_validate(&cred));

    test_fill_cred(&cred, FIXTURE_SSID, "short");
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID, nn2_wifi_cred_validate(&cred));

    test_fill_cred(&cred, FIXTURE_SSID, "");
    cred.password_len = 0u;
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID, nn2_wifi_cred_validate(&cred));

    memset(&cred, 0, sizeof(cred));
    memcpy(cred.ssid, ssid33, NN2_WIFI_SSID_MAX);
    cred.ssid_len = (uint8_t)(NN2_WIFI_SSID_MAX + 1u);
    memcpy(cred.password, FIXTURE_PASS, strlen(FIXTURE_PASS));
    cred.password_len = (uint8_t)strlen(FIXTURE_PASS);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID, nn2_wifi_cred_validate(&cred));

    test_fill_cred(&cred, FIXTURE_SSID, pass64);
    cred.password_len = (uint8_t)(NN2_WIFI_PASS_MAX + 1u);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID, nn2_wifi_cred_validate(&cred));

    test_fill_cred(&cred, "bad\nssid", FIXTURE_PASS);
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID, nn2_wifi_cred_validate(&cred));

    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_wifi_cred_validate(NULL));
}

void test_cred_store_round_trip_hides_password_from_log(void)
{
    fake_store_t store;
    fake_console_t console;
    nn2_port_wifi_cred_store_t store_port;
    nn2_port_prov_console_t console_port;
    nn2_app_wifi_provision_deps_t deps;
    nn2_wifi_credentials_t out;
    nn2_wifi_credentials_t loaded;

    memset(&store, 0, sizeof(store));
    memset(&console, 0, sizeof(console));
    test_fill_cred(&console.preset, FIXTURE_SSID, FIXTURE_PASS);
    console.result = NN2_OK;
    bind_store(&store_port, &store);
    bind_console(&console_port, &console);
    deps.store = &store_port;
    deps.console = &console_port;
    deps.console_timeout_ms = 1000u;

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_provision_if_needed(&deps, &out));
    TEST_ASSERT_EQUAL_STRING(FIXTURE_SSID, out.ssid);
    TEST_ASSERT_EQUAL_STRING(FIXTURE_PASS, out.password);
    TEST_ASSERT_EQUAL(1, store.save_calls);
    TEST_ASSERT_EQUAL(1, console.reads);

    memset(&loaded, 0, sizeof(loaded));
    TEST_ASSERT_EQUAL(NN2_OK, store_port.load(store_port.ctx, &loaded));
    TEST_ASSERT_EQUAL_STRING(FIXTURE_SSID, loaded.ssid);
    TEST_ASSERT_EQUAL_STRING(FIXTURE_PASS, loaded.password);
    TEST_ASSERT_EQUAL_UINT8(loaded.ssid_len, out.ssid_len);
    TEST_ASSERT_EQUAL_UINT8(loaded.password_len, out.password_len);

    TEST_ASSERT_NULL(strstr(store.log, FIXTURE_PASS));
    TEST_ASSERT_NULL(strstr(console.log, FIXTURE_PASS));
    TEST_ASSERT_NOT_NULL(strstr(store.log, FIXTURE_SSID));

    TEST_ASSERT_EQUAL(NN2_OK, store_port.clear(store_port.ctx));
    TEST_ASSERT_EQUAL(NN2_ERR_MISSING, store_port.load(store_port.ctx, &loaded));
    TEST_ASSERT_EQUAL(1, store.clear_calls);
}

void test_invalid_console_input_is_not_saved(void)
{
    fake_store_t store;
    fake_console_t console;
    nn2_port_wifi_cred_store_t store_port;
    nn2_port_prov_console_t console_port;
    nn2_app_wifi_provision_deps_t deps;
    nn2_wifi_credentials_t out;

    memset(&store, 0, sizeof(store));
    memset(&console, 0, sizeof(console));
    test_fill_cred(&console.preset, "", FIXTURE_PASS);
    console.preset.ssid_len = 0u;
    console.result = NN2_OK;
    bind_store(&store_port, &store);
    bind_console(&console_port, &console);
    deps.store = &store_port;
    deps.console = &console_port;
    deps.console_timeout_ms = 50u;

    TEST_ASSERT_EQUAL(NN2_ERR_INVALID, nn2_app_wifi_provision_if_needed(&deps, &out));
    TEST_ASSERT_EQUAL(0, store.save_calls);
    TEST_ASSERT_EQUAL(0, store.occupied);
}

void test_corrupt_store_reprovisions_without_keeping_old_secret(void)
{
    fake_store_t store;
    fake_console_t console;
    nn2_port_wifi_cred_store_t store_port;
    nn2_port_prov_console_t console_port;
    nn2_app_wifi_provision_deps_t deps;
    nn2_wifi_credentials_t out;

    memset(&store, 0, sizeof(store));
    store.corrupt = 1;
    test_fill_cred(&store.slot, "stale-ssid", "stale-secret-value");
    store.occupied = 1;
    memset(&console, 0, sizeof(console));
    test_fill_cred(&console.preset, FIXTURE_SSID, FIXTURE_PASS);
    console.result = NN2_OK;
    bind_store(&store_port, &store);
    bind_console(&console_port, &console);
    deps.store = &store_port;
    deps.console = &console_port;
    deps.console_timeout_ms = 10u;

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_provision_if_needed(&deps, &out));
    TEST_ASSERT_EQUAL(1, store.clear_calls);
    TEST_ASSERT_EQUAL(1, store.save_calls);
    TEST_ASSERT_EQUAL_STRING(FIXTURE_PASS, out.password);
    TEST_ASSERT_NULL(strstr(store.log, "stale-secret-value"));
    TEST_ASSERT_NULL(strstr(store.log, FIXTURE_PASS));
}

void test_console_text_is_not_an_emitter_command(void)
{
    fake_store_t store;
    fake_console_t console;
    fake_station_spy_t station_spy;
    nn2_port_wifi_cred_store_t store_port;
    nn2_port_prov_console_t console_port;
    nn2_port_wifi_station_t station;
    nn2_app_wifi_provision_deps_t deps;
    nn2_wifi_credentials_t out;

    memset(&store, 0, sizeof(store));
    memset(&console, 0, sizeof(console));
    memset(&station_spy, 0, sizeof(station_spy));
    test_fill_cred(&console.preset, "led on", FIXTURE_PASS);
    console.result = NN2_OK;
    bind_store(&store_port, &store);
    bind_console(&console_port, &console);
    memset(&station, 0, sizeof(station));
    station.connect = fake_connect;
    station.ctx = &station_spy;
    deps.store = &store_port;
    deps.console = &console_port;
    deps.console_timeout_ms = 10u;

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_provision_if_needed(&deps, &out));
    TEST_ASSERT_EQUAL_STRING("led on", out.ssid);
    TEST_ASSERT_EQUAL(0, station_spy.connect_calls);
    TEST_ASSERT_EQUAL(0, station_spy.led_on_calls);
    TEST_ASSERT_EQUAL(0, station_spy.stop_stream_calls);

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_connect_from_creds(&station, &out));
    TEST_ASSERT_EQUAL(1, station_spy.connect_calls);
    TEST_ASSERT_EQUAL(0, station_spy.led_on_calls);
    TEST_ASSERT_EQUAL(0, station_spy.stop_stream_calls);
    TEST_ASSERT_NULL(strstr(station_spy.log, FIXTURE_PASS));
    TEST_ASSERT_NOT_NULL(strstr(station_spy.log, "led on"));
}

void test_valid_store_skips_console(void)
{
    fake_store_t store;
    fake_console_t console;
    nn2_port_wifi_cred_store_t store_port;
    nn2_port_prov_console_t console_port;
    nn2_app_wifi_provision_deps_t deps;
    nn2_wifi_credentials_t out;

    memset(&store, 0, sizeof(store));
    memset(&console, 0, sizeof(console));
    test_fill_cred(&store.slot, FIXTURE_SSID, FIXTURE_PASS);
    store.occupied = 1;
    console.result = NN2_ERR_TIMEOUT;
    bind_store(&store_port, &store);
    bind_console(&console_port, &console);
    deps.store = &store_port;
    deps.console = &console_port;
    deps.console_timeout_ms = 10u;

    TEST_ASSERT_EQUAL(NN2_OK, nn2_app_wifi_provision_if_needed(&deps, &out));
    TEST_ASSERT_EQUAL(0, console.reads);
    TEST_ASSERT_EQUAL(0, store.save_calls);
    TEST_ASSERT_EQUAL_STRING(FIXTURE_SSID, out.ssid);
}

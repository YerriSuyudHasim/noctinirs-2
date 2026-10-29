/* Executes the actual adapter-private polling implementation with IDF mocks.
 * This establishes descriptor/lifetime/timeout behavior, not hardware timing. */
#include "nn2_test.h"
#include "nn2_ads131_logic.h"
#include <string.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 263
#define portMAX_DELAY UINT32_MAX
#define NN2_RTOS_ADS131_SPI_WAIT_MS 20u
typedef struct { size_t length, rxlength; const void *tx_buffer; void *rx_buffer; } spi_transaction_t;
typedef struct {
    nn2_ads131_logic_t logic;
    void *spi;
    spi_transaction_t transaction;
    uint8_t tx[18], rx[18];
    bool hardware_ready, poisoned, bus_acquired;
    esp_err_t startup_spi_error;
} device_t;
static unsigned acquires, starts, ends, releases;
static esp_err_t acquire_error, start_error, end_error;
static spi_transaction_t *in_flight;
static TickType_t ticks(uint32_t ms) { return (ms + 9u) / 10u; }
static nn2_status_t mapped(device_t *d, esp_err_t error)
{ (void)d; return error==ESP_OK ? NN2_OK : error==ESP_ERR_TIMEOUT ? NN2_ERR_TIMEOUT : NN2_ERR_IO; }
static esp_err_t spi_device_acquire_bus(void *spi, uint32_t wait)
{ (void)spi; TEST_ASSERT_EQUAL(portMAX_DELAY,wait); acquires++; return acquire_error; }
static void spi_device_release_bus(void *spi) { (void)spi; releases++; }
static esp_err_t spi_device_polling_start(void *spi, spi_transaction_t *t, uint32_t wait)
{
    (void)spi; starts++; TEST_ASSERT_EQUAL(portMAX_DELAY,wait);
    TEST_ASSERT_EQUAL(144u,t->length); TEST_ASSERT_EQUAL(144u,t->rxlength);
    in_flight=t; return start_error;
}
static esp_err_t spi_device_polling_end(void *spi, TickType_t wait)
{
    (void)spi; ends++; TEST_ASSERT_EQUAL(2u,wait);
    if (end_error==ESP_OK) memset(in_flight->rx_buffer,0x5a,18u);
    return end_error;
}
#include "nn2_ads131_transport.inc"
static device_t d;
static uint8_t tx[18], rx[18];
static void fresh(void)
{
    memset(&d,0,sizeof(d)); memset(tx,0x12,sizeof(tx)); memset(rx,0xa5,sizeof(rx));
    d.hardware_ready=true; d.logic.startup_stage=NN2_ADS131_STAGE_RREG;
    acquires=starts=ends=releases=0u; acquire_error=start_error=end_error=ESP_OK;
}
static void polling_holds_bus_until_reset_cleanup(void)
{
    fresh(); TEST_ASSERT_EQUAL(NN2_OK,transfer(&d,tx,rx));
    TEST_ASSERT_EQUAL_MEMORY(tx,d.tx,18u); TEST_ASSERT_EQUAL_HEX8(0x5a,rx[17]);
    TEST_ASSERT_EQUAL_PTR(d.tx,in_flight->tx_buffer); TEST_ASSERT_EQUAL_PTR(d.rx,in_flight->rx_buffer);
    TEST_ASSERT_EQUAL(NN2_OK,transfer(&d,tx,rx));
    TEST_ASSERT_EQUAL(1u,acquires); TEST_ASSERT_EQUAL(2u,starts); TEST_ASSERT_EQUAL(2u,ends);
    TEST_ASSERT_EQUAL(0u,releases); release_bus(&d); release_bus(&d);
    TEST_ASSERT_EQUAL(1u,releases); TEST_ASSERT_FALSE(d.bus_acquired);
    TEST_ASSERT_EQUAL(NN2_OK,transfer(&d,tx,rx)); TEST_ASSERT_EQUAL(2u,acquires);
}
static void polling_timeout_retains_driver_storage(void)
{
    fresh(); end_error=ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,transfer(&d,tx,rx));
    TEST_ASSERT_TRUE(d.poisoned); TEST_ASSERT_TRUE(d.bus_acquired);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,d.startup_spi_error);
    uint8_t before[18]; memset(before,0xa5,sizeof(before));
    TEST_ASSERT_EQUAL_MEMORY(before,rx,18u);
    release_bus(&d); TEST_ASSERT_EQUAL(0u,releases);
    TEST_ASSERT_EQUAL(NN2_ERR_IO,transfer(&d,tx,rx));
    TEST_ASSERT_EQUAL(1u,starts); TEST_ASSERT_EQUAL(1u,ends);
    TEST_ASSERT_EQUAL_PTR(&d.transaction,in_flight);
}
static void polling_start_and_lock_failure_have_no_rx(void)
{
    fresh(); acquire_error=ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(NN2_ERR_TIMEOUT,transfer(&d,tx,rx));
    TEST_ASSERT_FALSE(d.bus_acquired); TEST_ASSERT_FALSE(d.poisoned); TEST_ASSERT_EQUAL(0u,starts);
    fresh(); start_error=ESP_FAIL;
    TEST_ASSERT_EQUAL(NN2_ERR_IO,transfer(&d,tx,rx));
    TEST_ASSERT_EQUAL(0u,ends); TEST_ASSERT_FALSE(d.poisoned);
    TEST_ASSERT_EQUAL_HEX8(0xa5,rx[0]); release_bus(&d); TEST_ASSERT_EQUAL(1u,releases);
}
void test_ads131_transport_suite(void)
{
    RUN_TEST(polling_holds_bus_until_reset_cleanup);
    RUN_TEST(polling_timeout_retains_driver_storage);
    RUN_TEST(polling_start_and_lock_failure_have_no_rx);
}

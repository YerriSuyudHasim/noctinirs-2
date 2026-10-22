#include "nn2_wifi_host_backoff.h"
#include "nn2_wifi_backoff.h"
#include "nn2_wifi_limits.h"
#include "unity.h"

void test_backoff_grows_then_caps(void)
{
    nn2_wifi_backoff_t backoff;
    uint32_t delays[8];
    int i;
    uint32_t elapsed;
    int attempts_inside_grace;

    TEST_ASSERT_EQUAL(NN2_OK, nn2_wifi_backoff_init(&backoff,
                                                    NN2_WIFI_BACKOFF_FIRST_MS,
                                                    NN2_WIFI_BACKOFF_MAX_MS,
                                                    NN2_WIFI_BACKOFF_FACTOR));
    for (i = 0; i < 8; i++) {
        delays[i] = nn2_wifi_backoff_next_ms(&backoff);
        TEST_ASSERT_TRUE(delays[i] <= NN2_WIFI_BACKOFF_MAX_MS);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, delays[0]);
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_FIRST_MS, delays[1]);
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_FIRST_MS * 2u, delays[2]);
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_FIRST_MS * 4u, delays[3]);
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_FIRST_MS * 8u, delays[4]);
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_MAX_MS, delays[5]);
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_MAX_MS, delays[6]);
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_MAX_MS, delays[7]);

    elapsed = 0u;
    attempts_inside_grace = 0;
    for (i = 0; i < 8; i++) {
        if (delays[i] >= 3000u || elapsed > 3000u - delays[i]) {
            break;
        }
        elapsed += delays[i];
        if (elapsed < 3000u) {
            attempts_inside_grace++;
        }
    }
    TEST_ASSERT_TRUE(attempts_inside_grace >= 2);
    TEST_ASSERT_TRUE(delays[0] + delays[1] < 3000u);

    nn2_wifi_backoff_reset(&backoff);
    TEST_ASSERT_EQUAL_UINT32(0u, nn2_wifi_backoff_next_ms(&backoff));
    TEST_ASSERT_EQUAL_UINT32(NN2_WIFI_BACKOFF_FIRST_MS, nn2_wifi_backoff_next_ms(&backoff));
}

void test_backoff_rejects_bad_config(void)
{
    nn2_wifi_backoff_t backoff;

    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_wifi_backoff_init(NULL, 500u, 8000u, 2u));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_wifi_backoff_init(&backoff, 0u, 8000u, 2u));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_wifi_backoff_init(&backoff, 5000u, 1000u, 2u));
    TEST_ASSERT_EQUAL(NN2_ERR_INVALID_ARG, nn2_wifi_backoff_init(&backoff, 500u, 8000u, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, nn2_wifi_backoff_next_ms(NULL));
}

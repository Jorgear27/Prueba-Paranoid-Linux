/**
 * @file test_mqtt_handler.c
 * @brief Unity unit tests for MQTT payload construction and courier_state.
 *
 * These tests run on the host using the Unity C test framework.
 *
 * Build (host, not Zephyr):
 *   gcc -I../src -Iunity/src \
 *       test_mqtt_handler.c ../src/courier_state.c unity/src/unity.c \
 *       -lpthread -o test_mqtt_handler && ./test_mqtt_handler
 *
 */

#include "courier_state.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Stub out Zephyr mutex/sem for host builds
 * ------------------------------------------------------------------------- */
#ifndef ZEPHYR_STUB_TYPES
#include <pthread.h>
#include <semaphore.h>

/* Map Zephyr types to POSIX equivalents on the host */
struct k_mutex
{
    pthread_mutex_t m;
};
struct k_sem
{
    sem_t s;
};

int k_mutex_init(struct k_mutex* mu)
{
    return pthread_mutex_init(&mu->m, NULL);
}
int k_mutex_lock(struct k_mutex* mu, int t)
{
    (void)t;
    return pthread_mutex_lock(&mu->m);
}
int k_mutex_unlock(struct k_mutex* mu)
{
    return pthread_mutex_unlock(&mu->m);
}
int k_sem_init(struct k_sem* s, int i, int m)
{
    (void)m;
    return sem_init(&s->s, 0, (unsigned)i);
}
int k_sem_take(struct k_sem* s, int t)
{
    (void)t;
    return sem_trywait(&s->s);
}
int k_sem_give(struct k_sem* s)
{
    sem_post(&s->s);
    return 0;
}
#endif

/* ---------------------------------------------------------------------------
 * Helpers: JSON payload builders (mirror the logic in mqtt_handler.c)
 * These are extracted here so they can be tested independently of the MQTT
 * stack and the Zephyr networking APIs.
 * ------------------------------------------------------------------------- */

static int build_tracking_payload(char* buf, size_t len, int32_t lat_e6, int32_t lon_e6, const char* ts)
{
    return snprintf(buf, len, "{\"lat\":%d.%06d,\"lon\":%d.%06d,\"ts\":\"%s\"}", (int)(lat_e6 / 1000000),
                    (int)(lat_e6 < 0 ? -lat_e6 % 1000000 : lat_e6 % 1000000), (int)(lon_e6 / 1000000),
                    (int)(lon_e6 < 0 ? -lon_e6 % 1000000 : lon_e6 % 1000000), ts);
}

static int build_sos_payload(char* buf, size_t len, int32_t lat_e6, int32_t lon_e6, const char* ts)
{
    /* SOS uses the same payload as tracking */
    return build_tracking_payload(buf, len, lat_e6, lon_e6, ts);
}

static int build_delivered_payload(char* buf, size_t len, const char* stop_name)
{
    return snprintf(buf, len, "{\"stop\":\"%s\",\"status\":\"done\"}", stop_name);
}

/* ---------------------------------------------------------------------------
 * Test: setUp / tearDown
 * ------------------------------------------------------------------------- */

void setUp(void)
{
    courier_state_init("E001");
}

void tearDown(void)
{
    /* Nothing to clean up — courier_state_init resets all state */
}

/* ---------------------------------------------------------------------------
 * courier_state tests
 * ------------------------------------------------------------------------- */

void test_InitialStopCountIsZero(void)
{
    char buf[MAX_STOP_NAME];
    bool ok = courier_state_get_current_stop(buf, sizeof(buf));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("---", buf);
}

void test_LoadStops_SetsStopCount(void)
{
    k_mutex_lock(&g_courier.lock, 0);
    strcpy(g_courier.stops[0], "Mercado Sur");
    strcpy(g_courier.stops[1], "Mercado Norte");
    g_courier.stop_count = 2;
    g_courier.current_stop_idx = 0;
    k_mutex_unlock(&g_courier.lock);

    char buf[MAX_STOP_NAME];
    bool ok = courier_state_get_current_stop(buf, sizeof(buf));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("Mercado Sur", buf);
}

void test_GetNextStop_ReturnsSecondStop(void)
{
    k_mutex_lock(&g_courier.lock, 0);
    strcpy(g_courier.stops[0], "Mercado Sur");
    strcpy(g_courier.stops[1], "Mercado Norte");
    g_courier.stop_count = 2;
    g_courier.current_stop_idx = 0;
    k_mutex_unlock(&g_courier.lock);

    char buf[MAX_STOP_NAME];
    bool ok = courier_state_get_next_stop(buf, sizeof(buf));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("Mercado Norte", buf);
}

void test_AdvanceStop_IncrementsIndex(void)
{
    k_mutex_lock(&g_courier.lock, 0);
    strcpy(g_courier.stops[0], "A");
    strcpy(g_courier.stops[1], "B");
    g_courier.stop_count = 2;
    g_courier.current_stop_idx = 0;
    k_mutex_unlock(&g_courier.lock);

    bool more = courier_state_advance_stop();
    TEST_ASSERT_TRUE(more);

    char buf[MAX_STOP_NAME];
    courier_state_get_current_stop(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("B", buf);
}

void test_AdvanceStop_ReturnsFalseAtLastStop(void)
{
    k_mutex_lock(&g_courier.lock, 0);
    strcpy(g_courier.stops[0], "Only Stop");
    g_courier.stop_count = 1;
    g_courier.current_stop_idx = 0;
    k_mutex_unlock(&g_courier.lock);

    bool more = courier_state_advance_stop();
    TEST_ASSERT_FALSE(more);
}

void test_GetNextStop_ReturnsTripleDashAtLastStop(void)
{
    k_mutex_lock(&g_courier.lock, 0);
    strcpy(g_courier.stops[0], "Final Stop");
    g_courier.stop_count = 1;
    g_courier.current_stop_idx = 0;
    k_mutex_unlock(&g_courier.lock);

    char buf[MAX_STOP_NAME];
    bool ok = courier_state_get_next_stop(buf, sizeof(buf));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("---", buf);
}

void test_UpdateAndGetGPS(void)
{
    courier_state_update_gps(-31413000, -64183000, true);

    gps_fix_t fix;
    courier_state_get_gps(&fix);
    TEST_ASSERT_EQUAL_INT32(-31413000, fix.lat_e6);
    TEST_ASSERT_EQUAL_INT32(-64183000, fix.lon_e6);
    TEST_ASSERT_TRUE(fix.fix_valid);
}

void test_GPSInvalidFix(void)
{
    courier_state_update_gps(0, 0, false);

    gps_fix_t fix;
    courier_state_get_gps(&fix);
    TEST_ASSERT_FALSE(fix.fix_valid);
}

/* ---------------------------------------------------------------------------
 * Payload format tests
 * ------------------------------------------------------------------------- */

void test_TrackingPayload_ContainsLatLon(void)
{
    char buf[256];
    build_tracking_payload(buf, sizeof(buf), -31413000, -64183000, "T+1234");

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"lat\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"lon\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "T+1234"));
}

void test_TrackingPayload_LatIsNegative(void)
{
    char buf[256];
    build_tracking_payload(buf, sizeof(buf), -31413000, -64183000, "T+0");
    /* Should contain a minus sign for the latitude */
    TEST_ASSERT_NOT_NULL(strstr(buf, "-31."));
}

void test_SOSPayload_SameFormatAsTracking(void)
{
    char tracking[256], sos[256];
    build_tracking_payload(tracking, sizeof(tracking), -31000000, -64000000, "ts1");
    build_sos_payload(sos, sizeof(sos), -31000000, -64000000, "ts1");
    TEST_ASSERT_EQUAL_STRING(tracking, sos);
}

void test_DeliveredPayload_ContainsStopAndStatus(void)
{
    char buf[256];
    build_delivered_payload(buf, sizeof(buf), "Mercado Sur");

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"stop\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Mercado Sur"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"status\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"done\""));
}

void test_DeliveredPayload_StatusIsDone(void)
{
    char buf[256];
    build_delivered_payload(buf, sizeof(buf), "Hub A");
    /* Exact required format from the spec */
    TEST_ASSERT_EQUAL_STRING("{\"stop\":\"Hub A\",\"status\":\"done\"}", buf);
}

void test_TrackingPayload_ZeroCoordinates(void)
{
    char buf[256];
    int rc = build_tracking_payload(buf, sizeof(buf), 0, 0, "T+0");
    TEST_ASSERT_GREATER_THAN(0, rc);
    TEST_ASSERT_NOT_NULL(strstr(buf, "0.000000"));
}

/* ---------------------------------------------------------------------------
 * Route parser test (mirrors parse_and_store_route logic)
 * ------------------------------------------------------------------------- */

/* Minimal local reimplementation of the parser for host-side testing */
static int parse_route_to_array(const char* payload, char stops[][MAX_STOP_NAME], int max_stops)
{
    const char* p = payload;
    while (*p && *p != '[')
        p++;
    if (!*p)
        return 0;
    p++;

    int count = 0;
    while (*p && count < max_stops)
    {
        while (*p == ' ' || *p == ',' || *p == '\n')
            p++;
        if (*p == ']' || !*p)
            break;
        if (*p != '"')
        {
            p++;
            continue;
        }
        p++;
        const char* start = p;
        while (*p && *p != '"')
            p++;
        size_t len = (size_t)(p - start);
        if (len >= MAX_STOP_NAME)
            len = MAX_STOP_NAME - 1;
        memcpy(stops[count], start, len);
        stops[count][len] = '\0';
        count++;
        if (*p)
            p++;
    }
    return count;
}

void test_RouteParser_TwoStops(void)
{
    const char* payload = "[\"Mercado Sur\",\"Mercado Norte\"]";
    char stops[MAX_STOPS][MAX_STOP_NAME];
    int count = parse_route_to_array(payload, stops, MAX_STOPS);

    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("Mercado Sur", stops[0]);
    TEST_ASSERT_EQUAL_STRING("Mercado Norte", stops[1]);
}

void test_RouteParser_ThreeStops(void)
{
    const char* payload = "[\"A\", \"B\", \"C\"]";
    char stops[MAX_STOPS][MAX_STOP_NAME];
    int count = parse_route_to_array(payload, stops, MAX_STOPS);

    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_STRING("A", stops[0]);
    TEST_ASSERT_EQUAL_STRING("B", stops[1]);
    TEST_ASSERT_EQUAL_STRING("C", stops[2]);
}

void test_RouteParser_EmptyArray(void)
{
    const char* payload = "[]";
    char stops[MAX_STOPS][MAX_STOP_NAME];
    int count = parse_route_to_array(payload, stops, MAX_STOPS);
    TEST_ASSERT_EQUAL_INT(0, count);
}

void test_RouteParser_SingleStop(void)
{
    const char* payload = "[\"Only Stop\"]";
    char stops[MAX_STOPS][MAX_STOP_NAME];
    int count = parse_route_to_array(payload, stops, MAX_STOPS);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("Only Stop", stops[0]);
}

/* ---------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    /* courier_state */
    RUN_TEST(test_InitialStopCountIsZero);
    RUN_TEST(test_LoadStops_SetsStopCount);
    RUN_TEST(test_GetNextStop_ReturnsSecondStop);
    RUN_TEST(test_AdvanceStop_IncrementsIndex);
    RUN_TEST(test_AdvanceStop_ReturnsFalseAtLastStop);
    RUN_TEST(test_GetNextStop_ReturnsTripleDashAtLastStop);
    RUN_TEST(test_UpdateAndGetGPS);
    RUN_TEST(test_GPSInvalidFix);

    /* payload format */
    RUN_TEST(test_TrackingPayload_ContainsLatLon);
    RUN_TEST(test_TrackingPayload_LatIsNegative);
    RUN_TEST(test_SOSPayload_SameFormatAsTracking);
    RUN_TEST(test_DeliveredPayload_ContainsStopAndStatus);
    RUN_TEST(test_DeliveredPayload_StatusIsDone);
    RUN_TEST(test_TrackingPayload_ZeroCoordinates);

    /* route parser */
    RUN_TEST(test_RouteParser_TwoStops);
    RUN_TEST(test_RouteParser_ThreeStops);
    RUN_TEST(test_RouteParser_EmptyArray);
    RUN_TEST(test_RouteParser_SingleStop);

    return UNITY_END();
}

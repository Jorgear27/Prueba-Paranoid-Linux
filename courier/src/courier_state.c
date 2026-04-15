#include "courier_state.h"

#include <string.h>

#if defined(__ZEPHYR__)
#include <zephyr/logging/log.h>
#else
#include <stdio.h>

#define LOG_MODULE_REGISTER(name, level)
#define LOG_INF(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        fprintf(stderr, __VA_ARGS__);                                                                                  \
        fputc('\n', stderr);                                                                                           \
    } while (0)
#define LOG_DBG(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#define LOG_WRN(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        fprintf(stderr, __VA_ARGS__);                                                                                  \
        fputc('\n', stderr);                                                                                           \
    } while (0)
#define LOG_ERR(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        fprintf(stderr, __VA_ARGS__);                                                                                  \
        fputc('\n', stderr);                                                                                           \
    } while (0)

int k_mutex_init(struct k_mutex* mu)
{
    return pthread_mutex_init(&mu->m, NULL);
}

int k_mutex_lock(struct k_mutex* mu, int timeout)
{
    (void)timeout;
    return pthread_mutex_lock(&mu->m);
}

int k_mutex_unlock(struct k_mutex* mu)
{
    return pthread_mutex_unlock(&mu->m);
}
#endif

LOG_MODULE_REGISTER(courier_state, LOG_LEVEL_DBG);

courier_state_t g_courier;

void courier_state_init(const char* employee_id)
{
    k_mutex_init(&g_courier.lock);

    k_mutex_lock(&g_courier.lock, K_FOREVER);
    memset(&g_courier, 0, sizeof(g_courier));
    k_mutex_init(&g_courier.lock); /* re-init after memset */
    strncpy(g_courier.employee_id, employee_id, MAX_EMPLOYEE_ID - 1);
    g_courier.employee_id[MAX_EMPLOYEE_ID - 1] = '\0';
    g_courier.stop_count = 0;
    g_courier.current_stop_idx = 0;
    g_courier.gps.fix_valid = false;
    k_mutex_unlock(&g_courier.lock);

    LOG_INF("State initialised for employee: %s", employee_id);
}

bool courier_state_get_current_stop(char* buf, size_t buf_len)
{
    bool ok = false;

    k_mutex_lock(&g_courier.lock, K_FOREVER);
    if (g_courier.stop_count > 0 && g_courier.current_stop_idx < g_courier.stop_count)
    {
        strncpy(buf, g_courier.stops[g_courier.current_stop_idx], buf_len - 1);
        buf[buf_len - 1] = '\0';
        ok = true;
    }
    else
    {
        strncpy(buf, "---", buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
    k_mutex_unlock(&g_courier.lock);
    return ok;
}

bool courier_state_get_next_stop(char* buf, size_t buf_len)
{
    bool ok = false;

    k_mutex_lock(&g_courier.lock, K_FOREVER);
    int next = g_courier.current_stop_idx + 1;
    if (next < g_courier.stop_count)
    {
        strncpy(buf, g_courier.stops[next], buf_len - 1);
        buf[buf_len - 1] = '\0';
        ok = true;
    }
    else
    {
        strncpy(buf, "---", buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
    k_mutex_unlock(&g_courier.lock);
    return ok;
}

bool courier_state_advance_stop(void)
{
    bool more = false;

    k_mutex_lock(&g_courier.lock, K_FOREVER);
    if (g_courier.current_stop_idx + 1 < g_courier.stop_count)
    {
        g_courier.current_stop_idx++;
        more = true;
        LOG_INF("Advanced to stop %d: %s", g_courier.current_stop_idx, g_courier.stops[g_courier.current_stop_idx]);
    }
    else
    {
        LOG_INF("All stops completed.");
    }
    k_mutex_unlock(&g_courier.lock);
    return more;
}

void courier_state_update_gps(int32_t lat_e6, int32_t lon_e6, bool valid)
{
    k_mutex_lock(&g_courier.lock, K_FOREVER);
    g_courier.gps.lat_e6 = lat_e6;
    g_courier.gps.lon_e6 = lon_e6;
    g_courier.gps.fix_valid = valid;
    k_mutex_unlock(&g_courier.lock);
}

void courier_state_get_gps(gps_fix_t* out)
{
    k_mutex_lock(&g_courier.lock, K_FOREVER);
    *out = g_courier.gps;
    k_mutex_unlock(&g_courier.lock);
}

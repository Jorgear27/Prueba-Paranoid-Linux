/**
 * @file courier_state.h
 * @brief Shared courier state — stop list, current position, GPS fix.
 *
 * All fields are protected by a single mutex so any task can safely read or write.
 */

#ifndef COURIER_STATE_H
#define COURIER_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#else
#include <pthread.h>

/* Host-test compatibility path (no Zephyr SDK). */
#define ZEPHYR_STUB_TYPES 1

struct k_mutex
{
    pthread_mutex_t m;
};

int k_mutex_init(struct k_mutex* mu);
int k_mutex_lock(struct k_mutex* mu, int timeout);
int k_mutex_unlock(struct k_mutex* mu);

#ifndef K_FOREVER
#define K_FOREVER (-1)
#endif
#endif

/** Maximum number of stops in a single route. */
#define MAX_STOPS 32

/** Maximum length of a stop name, including null terminator. */
#define MAX_STOP_NAME 64

/** Employee ID string maximum length. */
#define MAX_EMPLOYEE_ID 32

/**
 * @brief GPS fix data.
 * lat and lon are in decimal degrees × 1e6 (integer fixed-point).
 * fix_valid is false when the GPS module has not yet acquired a fix.
 */
typedef struct
{
    int32_t lat_e6; /**< Latitude  × 1,000,000 (e.g. -31400000 = -31.4°) */
    int32_t lon_e6; /**< Longitude × 1,000,000 */
    bool fix_valid; /**< True once a valid fix has been received. */
} gps_fix_t;

/**
 * @brief Complete courier runtime state.
 */
typedef struct
{
    /** Protects all fields below. Acquire before any read or write. */
    struct k_mutex lock;

    /** Employee identifier string, e.g. "E042". */
    char employee_id[MAX_EMPLOYEE_ID];

    /** Ordered list of stop names received from routes/{employee_id}. */
    char stops[MAX_STOPS][MAX_STOP_NAME];

    /** Number of valid entries in stops[]. */
    int stop_count;

    /** Index of the stop the courier is currently heading to. */
    int current_stop_idx;

    /** Latest GPS fix. */
    gps_fix_t gps;
} courier_state_t;

/* -------------------------------------------------------------------------
 * Global instance — defined in courier_state.c
 * ------------------------------------------------------------------------- */
extern courier_state_t g_courier;

/**
 * @brief Initialise the global state and its mutex.
 * Must be called once from main() before any task starts.
 */
void courier_state_init(const char* employee_id);

/**
 * @brief Copy the current stop name into buf (null-terminated).
 * Returns false and writes "---" if no stops are loaded.
 */
bool courier_state_get_current_stop(char* buf, size_t buf_len);

/**
 * @brief Copy the next stop name into buf (null-terminated).
 * Returns false and writes "---" if this is the last stop.
 */
bool courier_state_get_next_stop(char* buf, size_t buf_len);

/**
 * @brief Advance to the next stop. Returns true if there are more stops.
 */
bool courier_state_advance_stop(void);

/**
 * @brief Update the GPS fix. Thread-safe.
 */
void courier_state_update_gps(int32_t lat_e6, int32_t lon_e6, bool valid);

/**
 * @brief Copy the current GPS fix. Thread-safe.
 */
void courier_state_get_gps(gps_fix_t* out);

#endif /* COURIER_STATE_H */

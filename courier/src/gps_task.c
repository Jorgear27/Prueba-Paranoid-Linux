#include "gps_task.h"
#include "courier_state.h"
#include "mqtt_handler.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gps_task, LOG_LEVEL_DBG);

/* ---------------------------------------------------------------------------
 * k_work_delayable — GPS publish work item.
 *
 * Using k_work_delayable (not a bare timer) means the GPS work runs on the
 * system workqueue thread, not in interrupt context. It can therefore safely
 * call mqtt_handler_publish_tracking() which acquires a mutex.
 *
 * The work handler reschedules itself — this creates a self-perpetuating
 * 5-second cycle without any dedicated GPS thread.
 * ------------------------------------------------------------------------- */

#define GPS_INTERVAL_MS 5000

static struct k_work_delayable s_gps_work;

/* ---------------------------------------------------------------------------
 * Hardware abstraction — replace with real UART/I2C GPS driver.
 *
 * In simulation (Qt build) this is replaced by gps_task_sim.c which feeds
 * scripted coordinates. On hardware, connect a NMEA GPS module to UART1 and
 * parse $GPRMC or $GPGGA sentences.
 * ------------------------------------------------------------------------- */

__attribute__((weak))
bool gps_hal_read(int32_t *lat_e6, int32_t *lon_e6)
{
    /* Default stub: no fix. Replace with real driver. */
    (void)lat_e6;
    (void)lon_e6;
    return false;
}

/* ---------------------------------------------------------------------------
 * Work handler — runs on system workqueue every GPS_INTERVAL_MS
 * ------------------------------------------------------------------------- */

static void gps_work_handler(struct k_work *work)
{
    int32_t lat_e6, lon_e6;
    bool    fix_valid = gps_hal_read(&lat_e6, &lon_e6);

    if (!fix_valid)
    {
        LOG_WRN("GPS: no fix — will retry in %d ms", GPS_INTERVAL_MS);
        /* Update state with invalid fix so UI can show "No GPS" if needed */
        courier_state_update_gps(0, 0, false);
    }
    else
    {
        LOG_DBG("GPS fix: lat=%d.%06d lon=%d.%06d",
                (int)(lat_e6 / 1000000), (int)abs(lat_e6 % 1000000),
                (int)(lon_e6 / 1000000), (int)abs(lon_e6 % 1000000));

        courier_state_update_gps(lat_e6, lon_e6, true);
        mqtt_handler_publish_tracking(lat_e6, lon_e6, true);
    }

    /* Reschedule — always, whether fix was valid or not */
    k_work_reschedule(&s_gps_work, K_MSEC(GPS_INTERVAL_MS));
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void gps_task_init(void)
{
    k_work_init_delayable(&s_gps_work, gps_work_handler);
    /* First execution after 1 s to let the network stack settle */
    k_work_schedule(&s_gps_work, K_SECONDS(1));
    LOG_INF("GPS task scheduled (interval=%d ms)", GPS_INTERVAL_MS);
}

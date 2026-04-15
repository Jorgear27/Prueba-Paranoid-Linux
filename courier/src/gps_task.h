#ifndef GPS_TASK_H
#define GPS_TASK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialise and start the GPS polling work item.
 * Safe to call from main() after WiFi and MQTT are up.
 */
void gps_task_init(void);

/**
 * @brief GPS hardware abstraction — implemented by the BSP or simulation.
 *
 * @param lat_e6  Output latitude  × 1e6.
 * @param lon_e6  Output longitude × 1e6.
 * @return true if a valid fix was obtained, false otherwise.
 */
bool gps_hal_read(int32_t *lat_e6, int32_t *lon_e6);

#endif /* GPS_TASK_H */

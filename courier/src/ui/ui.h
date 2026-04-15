/**
 * @file ui/ui.h
 * @brief UI abstraction layer — same API for LVGL (hardware) and Qt (simulation).
 *
 * The UI is intentionally separated from all MQTT and hardware logic so:
 *   a) The Zephyr build uses LVGL rendered on the SPI display.
 *   b) The Qt simulation build uses ui_sim.cpp which maps these calls to
 *      Qt widgets, allowing the full UI to run on a desktop without ESP32.
 *
 * All functions must be called from the LVGL task thread on hardware,
 * or from the Qt main thread in simulation. They are NOT thread-safe —
 * callers that run in other threads must dispatch via ui_post_event().
 */

#ifndef UI_H
#define UI_H

#include <stdbool.h>

/**
 * @brief Initialise the display and create all widgets.
 * Must be called once from the UI task before any other ui_* function.
 */
void ui_init(void);

/**
 * @brief Update the current-stop label on screen.
 * @param name  Null-terminated stop name string.
 */
void ui_set_current_stop(const char *name);

/**
 * @brief Update the next-stop label on screen.
 * @param name  Null-terminated stop name string, or "---" if none.
 */
void ui_set_next_stop(const char *name);

/**
 * @brief Show or hide the "No GPS" warning banner.
 */
void ui_set_gps_warning(bool visible);

/**
 * @brief Flash the SOS button red to give visual feedback on press.
 */
void ui_sos_feedback(void);

/**
 * @brief Called by the MQTT layer when a route is received and parsed.
 * Triggers a UI refresh from within the MQTT task via ui_post_event().
 * Thread-safe.
 */
void ui_notify_route_updated(void);

/**
 * @brief LVGL periodic tick task entry point.
 * Defined in ui.c (LVGL) or ui_sim.cpp (Qt) — not both.
 */
void ui_task(void *p1, void *p2, void *p3);

#endif /* UI_H */

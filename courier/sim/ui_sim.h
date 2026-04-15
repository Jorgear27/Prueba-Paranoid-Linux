/**
 * @file sim/ui_sim.h
 * @brief Declarations for symbols that main_sim.cpp needs from ui_sim.cpp.
 *
 * This header is only included by the Qt simulation build.
 */

#ifndef UI_SIM_H
#define UI_SIM_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

    /* The Qt button handles — used by main_sim.cpp to connect Qt signals. */
    struct QPushButton;
    struct QPushButton* ui_sim_get_sos_button(void);
    struct QPushButton* ui_sim_get_delivered_button(void);

    /*
     * Full ui.h API — implemented by ui_sim.cpp for Qt,
     * and by ui.c for LVGL/Zephyr.
     */
    void ui_init(void);
    void ui_set_current_stop(const char* name);
    void ui_set_next_stop(const char* name);
    void ui_set_gps_warning(bool visible);
    void ui_sos_feedback(void);
    void ui_notify_route_updated(void);
    void ui_task(void* p1, void* p2, void* p3);

#ifdef __cplusplus
}
#endif

#endif /* UI_SIM_H */

/**
 * @file ui/ui.c
 * @brief LVGL UI implementation for ESP32 + ILI9341 hardware.
 *
 * Layout (320×240):
 *
 *   ┌──────────────────────────────────────┐
 *   │  DHL Delivery                         │  ← title bar
 *   ├──────────────────────────────────────┤
 *   │  NOW:  Mercado Sur                   │  ← current stop (large)
 *   │  NEXT: Mercado Norte                 │  ← next stop (small, grey)
 *   ├──────────────────────────────────────┤
 *   │  [  SOS  ]        [ DELIVERED ]      │  ← buttons (bottom half)
 *   └──────────────────────────────────────┘
 */

#include "ui.h"
#include "courier_state.h"
#include "mqtt_handler.h"

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

LOG_MODULE_REGISTER(ui, LOG_LEVEL_DBG);

/* ---------------------------------------------------------------------------
 * Widget handles
 * ------------------------------------------------------------------------- */

static lv_obj_t *s_label_current;  /* Current stop name */
static lv_obj_t *s_label_next;     /* Next stop name    */
static lv_obj_t *s_label_gps_warn; /* "No GPS" warning  */
static lv_obj_t *s_btn_sos;
static lv_obj_t *s_btn_delivered;

/* Message queue for cross-thread UI updates (see ui_notify_route_updated) */
K_MSGQ_DEFINE(s_ui_msgq, sizeof(uint8_t), 8, 1);

typedef enum { UI_MSG_ROUTE_UPDATED = 1 } ui_msg_t;

/* ---------------------------------------------------------------------------
 * Button callbacks
 * ------------------------------------------------------------------------- */

static void sos_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        gps_fix_t fix;
        courier_state_get_gps(&fix);
        mqtt_handler_publish_sos(fix.lat_e6, fix.lon_e6);
        ui_sos_feedback();
        LOG_WRN("SOS button pressed");
    }
}

static void delivered_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        char stop_name[MAX_STOP_NAME];
        courier_state_get_current_stop(stop_name, sizeof(stop_name));

        mqtt_handler_publish_delivered(stop_name);
        courier_state_advance_stop();

        /* Refresh labels */
        char current[MAX_STOP_NAME], next[MAX_STOP_NAME];
        courier_state_get_current_stop(current, sizeof(current));
        courier_state_get_next_stop(next, sizeof(next));
        lv_label_set_text(s_label_current, current);
        lv_label_set_text(s_label_next, next);

        LOG_INF("Delivered: %s", stop_name);
    }
}

/* ---------------------------------------------------------------------------
 * Public API — ui_init
 * ------------------------------------------------------------------------- */

void ui_init(void)
{
    /* Title bar */
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "DHL Delivery");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    /* Current stop label (large) */
    s_label_current = lv_label_create(lv_scr_act());
    lv_label_set_text(s_label_current, "NOW:  ---");
    lv_obj_set_style_text_font(s_label_current, &lv_font_montserrat_14, 0);
    lv_obj_align(s_label_current, LV_ALIGN_TOP_LEFT, 10, 35);

    /* Next stop label (smaller, grey) */
    s_label_next = lv_label_create(lv_scr_act());
    lv_label_set_text(s_label_next, "NEXT: ---");
    lv_obj_set_style_text_color(s_label_next,
                                lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_label_next, LV_ALIGN_TOP_LEFT, 10, 60);

    /* GPS warning (hidden by default) */
    s_label_gps_warn = lv_label_create(lv_scr_act());
    lv_label_set_text(s_label_gps_warn, "No GPS fix");
    lv_obj_set_style_text_color(s_label_gps_warn,
                                lv_color_hex(0xFF8800), LV_PART_MAIN);
    lv_obj_align(s_label_gps_warn, LV_ALIGN_TOP_RIGHT, -5, 35);
    lv_obj_add_flag(s_label_gps_warn, LV_OBJ_FLAG_HIDDEN);

    /* SOS button — red, left side */
    s_btn_sos = lv_btn_create(lv_scr_act());
    lv_obj_set_size(s_btn_sos, 130, 80);
    lv_obj_align(s_btn_sos, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(s_btn_sos, lv_color_hex(0xCC0000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_sos, lv_color_hex(0xFF3333),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_btn_sos, sos_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sos_lbl = lv_label_create(s_btn_sos);
    lv_label_set_text(sos_lbl, "SOS");
    lv_obj_center(sos_lbl);

    /* DELIVERED button — green, right side */
    s_btn_delivered = lv_btn_create(lv_scr_act());
    lv_obj_set_size(s_btn_delivered, 130, 80);
    lv_obj_align(s_btn_delivered, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(s_btn_delivered, lv_color_hex(0x007700),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_btn_delivered, lv_color_hex(0x00BB00),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_btn_delivered, delivered_btn_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *del_lbl = lv_label_create(s_btn_delivered);
    lv_label_set_text(del_lbl, "DELIVERED");
    lv_obj_center(del_lbl);

    LOG_INF("UI initialised");
}

void ui_set_current_stop(const char *name)
{
    char buf[MAX_STOP_NAME + 6];
    snprintf(buf, sizeof(buf), "NOW:  %s", name);
    lv_label_set_text(s_label_current, buf);
}

void ui_set_next_stop(const char *name)
{
    char buf[MAX_STOP_NAME + 6];
    snprintf(buf, sizeof(buf), "NEXT: %s", name);
    lv_label_set_text(s_label_next, buf);
}

void ui_set_gps_warning(bool visible)
{
    if (visible)
        lv_obj_clear_flag(s_label_gps_warn, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_label_gps_warn, LV_OBJ_FLAG_HIDDEN);
}

void ui_sos_feedback(void)
{
    /* Brief colour flash to acknowledge the press */
    lv_obj_set_style_bg_color(s_btn_sos, lv_color_hex(0xFFFFFF),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Restore after 200 ms — handled in ui_task timer callback */
}

void ui_notify_route_updated(void)
{
    uint8_t msg = UI_MSG_ROUTE_UPDATED;
    k_msgq_put(&s_ui_msgq, &msg, K_NO_WAIT);
}

/* ---------------------------------------------------------------------------
 * UI task — runs the LVGL tick and drains the message queue
 * ------------------------------------------------------------------------- */

#define LVGL_TICK_MS 10

void ui_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev))
    {
        LOG_ERR("Display device not ready");
        return;
    }

    display_blanking_off(display_dev);
    ui_init();

    while (1)
    {
        /* Drain the message queue — process UI update requests from other tasks */
        uint8_t msg;
        while (k_msgq_get(&s_ui_msgq, &msg, K_NO_WAIT) == 0)
        {
            if (msg == UI_MSG_ROUTE_UPDATED)
            {
                char current[MAX_STOP_NAME], next[MAX_STOP_NAME];
                courier_state_get_current_stop(current, sizeof(current));
                courier_state_get_next_stop(next, sizeof(next));
                ui_set_current_stop(current);
                ui_set_next_stop(next);
            }
        }

        /* Update GPS warning based on current fix state */
        gps_fix_t fix;
        courier_state_get_gps(&fix);
        ui_set_gps_warning(!fix.fix_valid);

        /* Drive LVGL */
        lv_tick_inc(LVGL_TICK_MS);
        lv_task_handler();

        k_sleep(K_MSEC(LVGL_TICK_MS));
    }
}

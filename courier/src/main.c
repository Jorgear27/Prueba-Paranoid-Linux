/**
 * @file main.c
 * @brief Courier Rugged System — Zephyr RTOS entry point.
 *
 * Thread layout and priorities (lower number = higher priority):
 *
 *   Priority 5  SOS ISR thread        — gpio callback → k_sem → publish
 *   Priority 6  Delivered ISR thread  — gpio callback → k_sem → publish
 *   Priority 7  MQTT handler thread   — receive loop, keepalive
 *   Priority 7  GPS work (workqueue)  — k_work_delayable, 5-second interval
 *   Priority 10 UI task               — LVGL tick, 10 ms
 *
 */

#include "courier_state.h"
#include "gps_task.h"
#include "mqtt_handler.h"
#include "ui/ui.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * Thread stacks and definitions
 * ------------------------------------------------------------------------- */

#define MQTT_TASK_STACK 4096
#define SOS_TASK_STACK 2048
#define DELIVERED_STACK 2048
#define UI_TASK_STACK 4096

K_THREAD_STACK_DEFINE(mqtt_stack, MQTT_TASK_STACK);
K_THREAD_STACK_DEFINE(sos_stack, SOS_TASK_STACK);
K_THREAD_STACK_DEFINE(delivered_stack, DELIVERED_STACK);
K_THREAD_STACK_DEFINE(ui_stack, UI_TASK_STACK);

static struct k_thread mqtt_thread;
static struct k_thread sos_thread;
static struct k_thread delivered_thread;
static struct k_thread ui_thread;

/* ---------------------------------------------------------------------------
 * Button GPIO
 * ------------------------------------------------------------------------- */

static const struct gpio_dt_spec s_sos_btn = GPIO_DT_SPEC_GET(DT_ALIAS(sos_btn), gpios);
static const struct gpio_dt_spec s_delivered_btn = GPIO_DT_SPEC_GET(DT_ALIAS(delivered_btn), gpios);

static struct gpio_callback s_sos_cb_data;
static struct gpio_callback s_delivered_cb_data;

/* Semaphores — ISR signals, task waits */
static K_SEM_DEFINE(s_sos_sem, 0, 1);
static K_SEM_DEFINE(s_delivered_sem, 0, 1);

static void sos_isr(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    k_sem_give(&s_sos_sem);
}

static void delivered_isr(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    k_sem_give(&s_delivered_sem);
}

/* ---------------------------------------------------------------------------
 * SOS thread — priority 5 (highest in the system)
 * Waits on semaphore given by GPIO ISR. No other work happens here.
 * ------------------------------------------------------------------------- */

static void sos_task(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    LOG_INF("SOS task ready");

    while (1)
    {
        k_sem_take(&s_sos_sem, K_FOREVER);

        LOG_WRN("SOS activated!");

        gps_fix_t fix;
        courier_state_get_gps(&fix);
        mqtt_handler_publish_sos(fix.lat_e6, fix.lon_e6);

        /* Debounce: ignore repeated presses for 500 ms */
        k_sleep(K_MSEC(500));
    }
}

/* ---------------------------------------------------------------------------
 * Delivered thread — priority 6
 * ------------------------------------------------------------------------- */

static void delivered_task(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    LOG_INF("Delivered task ready");

    while (1)
    {
        k_sem_take(&s_delivered_sem, K_FOREVER);

        char stop_name[MAX_STOP_NAME];
        courier_state_get_current_stop(stop_name, sizeof(stop_name));
        mqtt_handler_publish_delivered(stop_name);
        courier_state_advance_stop();
        ui_notify_route_updated();

        k_sleep(K_MSEC(300));
    }
}

/* ---------------------------------------------------------------------------
 * WiFi — connect and wait for IP address
 * ------------------------------------------------------------------------- */

static struct net_mgmt_event_callback s_wifi_cb;
static K_SEM_DEFINE(s_wifi_sem, 0, 1);

static void wifi_event_handler(struct net_mgmt_event_callback* cb, uint32_t mgmt_event, struct net_if* iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);

    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD)
    {
        LOG_INF("WiFi: IP address assigned");
        k_sem_give(&s_wifi_sem);
    }
}

static int wifi_connect(void)
{
    struct net_if* iface = net_if_get_default();

    net_mgmt_init_event_callback(&s_wifi_cb, wifi_event_handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&s_wifi_cb);

    struct wifi_connect_req_params params = {
        .ssid = (uint8_t*)WIFI_SSID,
        .ssid_length = strlen(WIFI_SSID),
        .psk = (uint8_t*)WIFI_PSK,
        .psk_length = strlen(WIFI_PSK),
        .security = WIFI_SECURITY_TYPE_PSK,
        .channel = WIFI_CHANNEL_ANY,
    };

    int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (rc != 0)
    {
        LOG_ERR("WiFi connect request failed: %d", rc);
        return rc;
    }

    LOG_INF("WiFi: connecting to %s ...", WIFI_SSID);

    /* Wait up to 30 seconds for IP assignment */
    if (k_sem_take(&s_wifi_sem, K_SECONDS(30)) != 0)
    {
        LOG_ERR("WiFi: timed out waiting for IP");
        return -ETIMEDOUT;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * GPIO button initialisation
 * ------------------------------------------------------------------------- */

static int buttons_init(void)
{
    int rc;

    if (!device_is_ready(s_sos_btn.port) || !device_is_ready(s_delivered_btn.port))
    {
        LOG_ERR("Button GPIO devices not ready");
        return -ENODEV;
    }

    rc = gpio_pin_configure_dt(&s_sos_btn, GPIO_INPUT);
    if (rc != 0)
    {
        LOG_ERR("SOS pin config failed: %d", rc);
        return rc;
    }

    rc = gpio_pin_configure_dt(&s_delivered_btn, GPIO_INPUT);
    if (rc != 0)
    {
        LOG_ERR("Delivered pin config failed: %d", rc);
        return rc;
    }

    gpio_init_callback(&s_sos_cb_data, sos_isr, BIT(s_sos_btn.pin));
    gpio_init_callback(&s_delivered_cb_data, delivered_isr, BIT(s_delivered_btn.pin));

    gpio_add_callback(s_sos_btn.port, &s_sos_cb_data);
    gpio_add_callback(s_delivered_btn.port, &s_delivered_cb_data);

    gpio_pin_interrupt_configure_dt(&s_sos_btn, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_pin_interrupt_configure_dt(&s_delivered_btn, GPIO_INT_EDGE_TO_ACTIVE);

    LOG_INF("Buttons initialised (SOS=GPIO%d, Delivered=GPIO%d)", s_sos_btn.pin, s_delivered_btn.pin);
    return 0;
}

/* ---------------------------------------------------------------------------
 * main()
 * ------------------------------------------------------------------------- */

int main(void)
{
    LOG_INF("=== DHL Courier Rugged System v1.0 ===");
    LOG_INF("Employee: %s", EMPLOYEE_ID);

    /* 1. Shared state */
    courier_state_init(EMPLOYEE_ID);

    /* 2. GPIO buttons (interrupts ready before network) */
    if (buttons_init() != 0)
    {
        LOG_ERR("Button init failed — SOS unavailable");
        /* Non-fatal: continue without hardware buttons */
    }

    /* 3. Start UI task early so the screen shows a "Connecting..." state
     *    while WiFi and MQTT negotiate. */
    k_thread_create(&ui_thread, ui_stack, UI_TASK_STACK, ui_task, NULL, NULL, NULL, 10, 0, K_NO_WAIT);
    k_thread_name_set(&ui_thread, "ui");

    /* 4. Connect to WiFi */
    if (wifi_connect() != 0)
    {
        LOG_ERR("WiFi connection failed — retrying in 10 s");
        k_sleep(K_SECONDS(10));
        /* In production: persist state to NVS and reset */
    }

    /* 5. Connect to MQTT broker (RabbitMQ MQTT plugin on port 1883) */
    int rc = mqtt_handler_init(MQTT_BROKER_ADDR, MQTT_BROKER_PORT, EMPLOYEE_ID);
    if (rc != MQTT_OK)
    {
        LOG_ERR("MQTT init failed: %d", rc);
        /* The MQTT task will retry automatically */
    }

    /* 6. Subscribe to routes/{employee_id} */
    mqtt_handler_subscribe_routes();

    /* 7. Start the MQTT receive/keepalive task */
    k_thread_create(&mqtt_thread, mqtt_stack, MQTT_TASK_STACK, mqtt_handler_task, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
    k_thread_name_set(&mqtt_thread, "mqtt");

    /* 8. Start SOS thread (highest priority) */
    k_thread_create(&sos_thread, sos_stack, SOS_TASK_STACK, sos_task, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_name_set(&sos_thread, "sos");

    /* 9. Start Delivered thread */
    k_thread_create(&delivered_thread, delivered_stack, DELIVERED_STACK, delivered_task, NULL, NULL, NULL, 6, 0,
                    K_NO_WAIT);
    k_thread_name_set(&delivered_thread, "delivered");

    /* 10. Start GPS polling (k_work_delayable on system workqueue) */
    gps_task_init();

    LOG_INF("All tasks started — device operational");
    return 0;
}

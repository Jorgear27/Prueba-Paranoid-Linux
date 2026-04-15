#include "mqtt_handler.h"
#include "courier_state.h"
#include "ui/ui.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(mqtt_handler, LOG_LEVEL_DBG);

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define MQTT_RX_BUF_SIZE 1024
#define MQTT_TX_BUF_SIZE 512
#define TOPIC_BUF_SIZE 64
#define PAYLOAD_BUF_SIZE 256
#define CONNECT_TIMEOUT_MS 10000
#define KEEPALIVE_TIMEOUT K_SECONDS(30)

/* ---------------------------------------------------------------------------
 * Static state
 * ------------------------------------------------------------------------- */

static struct mqtt_client s_client;
static struct sockaddr_in s_broker;
static uint8_t s_rx_buf[MQTT_RX_BUF_SIZE];
static uint8_t s_tx_buf[MQTT_TX_BUF_SIZE];
static char s_employee_id[MAX_EMPLOYEE_ID];

/* Mutex protecting s_client from concurrent publishes. */
static struct k_mutex s_mqtt_mutex;

/* Semaphore signalled when CONNACK is received. */
static struct k_sem s_connack_sem;

/* ---------------------------------------------------------------------------
 * Topic helpers
 * ------------------------------------------------------------------------- */

static void make_topic(char* buf, size_t len, const char* prefix)
{
    snprintf(buf, len, "%s/%s", prefix, s_employee_id);
}

static void make_topic_sos(char* buf, size_t len)
{
    snprintf(buf, len, "alerts/sos/%s", s_employee_id);
}

/* ---------------------------------------------------------------------------
 * Timestamp helper (Zephyr uptime in ISO-like format). With the esp32 we could use the
 * built-in RTC for real timestamps, but for simplicity we just use uptime here.
 * ------------------------------------------------------------------------- */

static void get_timestamp(char* buf, size_t len)
{
    int64_t ms = k_uptime_get();
    snprintf(buf, len, "T+%lld", (long long)ms);
}

/* ---------------------------------------------------------------------------
 * Route JSON parser
 *
 * Expected payload: ["Stop A", "Stop B", "Stop C"]
 * ------------------------------------------------------------------------- */

static void parse_and_store_route(const char* payload, size_t len)
{
    /* The payload is a JSON array of strings. Walk it manually. */
    k_mutex_lock(&g_courier.lock, K_FOREVER);
    g_courier.stop_count = 0;
    g_courier.current_stop_idx = 0;
    k_mutex_unlock(&g_courier.lock);

    /* Find opening bracket */
    const char* p = payload;
    const char* end = payload + len;
    while (p < end && *p != '[')
        p++;
    if (p >= end)
    {
        LOG_ERR("Route: no opening bracket");
        return;
    }
    p++; /* skip '[' */

    int count = 0;
    while (p < end && count < MAX_STOPS)
    {
        /* Skip whitespace and commas */
        while (p < end && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r'))
            p++;
        if (p >= end || *p == ']')
            break;

        if (*p != '"')
        {
            p++;
            continue;
        }
        p++; /* skip opening quote */

        /* Extract string until closing quote */
        const char* start = p;
        while (p < end && *p != '"')
            p++;

        size_t slen = (size_t)(p - start);
        if (slen >= MAX_STOP_NAME)
            slen = MAX_STOP_NAME - 1;

        k_mutex_lock(&g_courier.lock, K_FOREVER);
        memcpy(g_courier.stops[count], start, slen);
        g_courier.stops[count][slen] = '\0';
        count++;
        g_courier.stop_count = count;
        k_mutex_unlock(&g_courier.lock);

        if (p < end)
            p++; /* skip closing quote */
    }

    LOG_INF("Route loaded: %d stops", count);

    /* Notify UI so it can refresh immediately */
    ui_notify_route_updated();
}

/* ---------------------------------------------------------------------------
 * MQTT event callback — called by the Zephyr MQTT stack from mqtt_input()
 * ------------------------------------------------------------------------- */

static void mqtt_event_handler(struct mqtt_client* client, const struct mqtt_evt* evt)
{
    switch (evt->type)
    {
    case MQTT_EVT_CONNACK:
        if (evt->result == 0)
        {
            LOG_INF("MQTT connected (session_present=%d)", evt->param.connack.session_present_flag);
            k_sem_give(&s_connack_sem);
        }
        else
        {
            LOG_ERR("MQTT CONNACK error: %d", evt->result);
        }
        break;

    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected: %d", evt->result);
        break;

    case MQTT_EVT_PUBLISH: {
        const struct mqtt_publish_param* p = &evt->param.publish;
        LOG_INF("MQTT message on topic: %.*s  len=%d", p->message.topic.topic.size,
                (const char*)p->message.topic.topic.utf8, p->message.payload.len);

        /* Read the payload. mqtt_read_publish_payload returns the number
         * of bytes read; the message may be fragmented so we loop. */
        static char payload[MQTT_RX_BUF_SIZE];
        int remaining = p->message.payload.len;
        int offset = 0;

        while (remaining > 0 && offset < (int)sizeof(payload) - 1)
        {
            int rc = mqtt_read_publish_payload(client, payload + offset, remaining);
            if (rc < 0)
            {
                LOG_ERR("Failed to read payload: %d", rc);
                break;
            }
            offset += rc;
            remaining -= rc;
        }
        payload[offset] = '\0';

        /* Route the message by topic prefix */
        char routes_topic[TOPIC_BUF_SIZE];
        make_topic(routes_topic, sizeof(routes_topic), "routes");

        if (strncmp((const char*)p->message.topic.topic.utf8, routes_topic, p->message.topic.topic.size) == 0)
        {
            parse_and_store_route(payload, (size_t)offset);
        }

        /* Send PUBACK for QoS 1 */
        if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE)
        {
            struct mqtt_puback_param puback = {.message_id = p->message_id};
            mqtt_publish_qos1_ack(client, &puback);
        }
        break;
    }

    case MQTT_EVT_SUBACK:
        LOG_INF("MQTT SUBACK received (message_id=%d)", evt->param.suback.message_id);
        break;

    case MQTT_EVT_PUBACK:
        LOG_DBG("MQTT PUBACK received (message_id=%d)", evt->param.puback.message_id);
        break;

    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

mqtt_result_t mqtt_handler_init(const char* broker_addr, uint16_t broker_port, const char* employee_id)
{
    k_mutex_init(&s_mqtt_mutex);
    k_sem_init(&s_connack_sem, 0, 1);
    strncpy(s_employee_id, employee_id, MAX_EMPLOYEE_ID - 1);
    s_employee_id[MAX_EMPLOYEE_ID - 1] = '\0';

    /* Build broker address */
    memset(&s_broker, 0, sizeof(s_broker));
    s_broker.sin_family = AF_INET;
    s_broker.sin_port = htons(broker_port);
    net_addr_pton(AF_INET, broker_addr, &s_broker.sin_addr);

    /* Initialise MQTT client */
    mqtt_client_init(&s_client);

    s_client.broker = (struct sockaddr*)&s_broker;
    s_client.evt_cb = mqtt_event_handler;
    s_client.client_id.utf8 = (uint8_t*)s_employee_id;
    s_client.client_id.size = strlen(s_employee_id);
    s_client.protocol_version = MQTT_VERSION_3_1_1;
    s_client.clean_session = 1;
    s_client.keepalive = 60;

    /* RabbitMQ MQTT plugin requires basic auth if the vhost is not "/":
     * set username to "guest:/" (user:vhost) or configure accordingly. */
    s_client.user_name = NULL;
    s_client.password = NULL;

    s_client.rx_buf = s_rx_buf;
    s_client.rx_buf_size = sizeof(s_rx_buf);
    s_client.tx_buf = s_tx_buf;
    s_client.tx_buf_size = sizeof(s_tx_buf);

    int rc = mqtt_connect(&s_client);
    if (rc != 0)
    {
        LOG_ERR("mqtt_connect failed: %d", rc);
        return MQTT_ERR_CONNECT;
    }

    /* Wait for CONNACK (up to CONNECT_TIMEOUT_MS) */
    if (k_sem_take(&s_connack_sem, K_MSEC(CONNECT_TIMEOUT_MS)) != 0)
    {
        LOG_ERR("MQTT CONNACK timeout");
        return MQTT_ERR_CONNECT;
    }

    LOG_INF("MQTT initialised. Broker: %s:%d", broker_addr, broker_port);
    return MQTT_OK;
}

mqtt_result_t mqtt_handler_subscribe_routes(void)
{
    char topic_buf[TOPIC_BUF_SIZE];
    make_topic(topic_buf, sizeof(topic_buf), "routes");

    struct mqtt_topic topics[] = {
        {
            .topic = {.utf8 = (uint8_t*)topic_buf, .size = strlen(topic_buf)},
            .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        },
    };

    struct mqtt_subscription_list sub = {
        .list = topics,
        .list_count = ARRAY_SIZE(topics),
        .message_id = 1,
    };

    k_mutex_lock(&s_mqtt_mutex, K_FOREVER);
    int rc = mqtt_subscribe(&s_client, &sub);
    k_mutex_unlock(&s_mqtt_mutex);

    if (rc != 0)
    {
        LOG_ERR("mqtt_subscribe failed: %d", rc);
        return MQTT_ERR_SUBSCRIBE;
    }

    LOG_INF("Subscribed to: %s", topic_buf);
    return MQTT_OK;
}

mqtt_result_t mqtt_handler_publish_tracking(int32_t lat_e6, int32_t lon_e6, bool fix_valid)
{
    if (!fix_valid)
    {
        LOG_WRN("GPS: no fix — skipping tracking publish");
        return MQTT_OK; /* Not an error; will retry on next tick */
    }

    char ts[32];
    get_timestamp(ts, sizeof(ts));

    char payload[PAYLOAD_BUF_SIZE];
    /* lat/lon in decimal degrees from fixed-point */
    snprintf(payload, sizeof(payload), "{\"lat\":%d.%06d,\"lon\":%d.%06d,\"ts\":\"%s\"}", (int)(lat_e6 / 1000000),
             (int)abs(lat_e6 % 1000000), (int)(lon_e6 / 1000000), (int)abs(lon_e6 % 1000000), ts);

    char topic[TOPIC_BUF_SIZE];
    make_topic(topic, sizeof(topic), "tracking");

    struct mqtt_publish_param msg = {
        .message =
            {
                .topic =
                    {
                        .topic = {.utf8 = (uint8_t*)topic, .size = strlen(topic)},
                        .qos = MQTT_QOS_0_AT_MOST_ONCE,
                    },
                .payload = {.data = (uint8_t*)payload, .len = strlen(payload)},
            },
        .message_id = k_uptime_get_32() & 0xFFFF,
        .dup_flag = 0,
        .retain_flag = 0,
    };

    k_mutex_lock(&s_mqtt_mutex, K_FOREVER);
    int rc = mqtt_publish(&s_client, &msg);
    k_mutex_unlock(&s_mqtt_mutex);

    if (rc != 0)
    {
        LOG_ERR("Tracking publish failed: %d", rc);
        return MQTT_ERR_PUBLISH;
    }
    LOG_DBG("Tracking published: %s", payload);
    return MQTT_OK;
}

mqtt_result_t mqtt_handler_publish_sos(int32_t lat_e6, int32_t lon_e6)
{
    char ts[32];
    get_timestamp(ts, sizeof(ts));

    char payload[PAYLOAD_BUF_SIZE];
    snprintf(payload, sizeof(payload), "{\"lat\":%d.%06d,\"lon\":%d.%06d,\"ts\":\"%s\"}", (int)(lat_e6 / 1000000),
             (int)abs(lat_e6 % 1000000), (int)(lon_e6 / 1000000), (int)abs(lon_e6 % 1000000), ts);

    char topic[TOPIC_BUF_SIZE];
    make_topic_sos(topic, sizeof(topic));

    /* QoS 1 — at-least-once: SOS must not be silently dropped */
    struct mqtt_publish_param msg = {
        .message =
            {
                .topic =
                    {
                        .topic = {.utf8 = (uint8_t*)topic, .size = strlen(topic)},
                        .qos = MQTT_QOS_1_AT_LEAST_ONCE,
                    },
                .payload = {.data = (uint8_t*)payload, .len = strlen(payload)},
            },
        .message_id = k_uptime_get_32() & 0xFFFF,
        .dup_flag = 0,
        .retain_flag = 0,
    };

    k_mutex_lock(&s_mqtt_mutex, K_FOREVER);
    int rc = mqtt_publish(&s_client, &msg);
    k_mutex_unlock(&s_mqtt_mutex);

    if (rc != 0)
    {
        LOG_ERR("SOS publish failed: %d", rc);
        return MQTT_ERR_PUBLISH;
    }
    LOG_WRN("SOS PUBLISHED: %s", payload);
    return MQTT_OK;
}

mqtt_result_t mqtt_handler_publish_delivered(const char* stop_name)
{
    char payload[PAYLOAD_BUF_SIZE];
    snprintf(payload, sizeof(payload), "{\"stop\":\"%s\",\"status\":\"done\"}", stop_name);

    char topic[TOPIC_BUF_SIZE];
    make_topic(topic, sizeof(topic), "delivered");

    struct mqtt_publish_param msg = {
        .message =
            {
                .topic =
                    {
                        .topic = {.utf8 = (uint8_t*)topic, .size = strlen(topic)},
                        .qos = MQTT_QOS_1_AT_LEAST_ONCE,
                    },
                .payload = {.data = (uint8_t*)payload, .len = strlen(payload)},
            },
        .message_id = k_uptime_get_32() & 0xFFFF,
        .dup_flag = 0,
        .retain_flag = 0,
    };

    k_mutex_lock(&s_mqtt_mutex, K_FOREVER);
    int rc = mqtt_publish(&s_client, &msg);
    k_mutex_unlock(&s_mqtt_mutex);

    if (rc != 0)
    {
        LOG_ERR("Delivered publish failed: %d", rc);
        return MQTT_ERR_PUBLISH;
    }
    LOG_INF("Delivered published: stop=%s", stop_name);
    return MQTT_OK;
}

void mqtt_handler_task(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("MQTT handler task started");

    while (1)
    {
        k_mutex_lock(&s_mqtt_mutex, K_FOREVER);
        int rc = mqtt_input(&s_client);
        k_mutex_unlock(&s_mqtt_mutex);

        if (rc != 0 && rc != -EAGAIN)
        {
            LOG_ERR("mqtt_input error: %d — reconnecting in 5s", rc);
            k_sleep(K_SECONDS(5));
            /* Re-connect */
            mqtt_handler_init(MQTT_BROKER_ADDR, MQTT_BROKER_PORT, s_employee_id);
            mqtt_handler_subscribe_routes();
            continue;
        }

        k_mutex_lock(&s_mqtt_mutex, K_FOREVER);
        mqtt_live(&s_client);
        k_mutex_unlock(&s_mqtt_mutex);

        k_sleep(K_MSEC(100));
    }
}

/**
 * @file mqtt_handler.h
 * @brief MQTT connection, subscription, and typed publish helpers.
 *
 * The internal mqtt_client is accessed under a mutex so
 * concurrent publishes do not corrupt the broker connection.
 *
 * Topic mapping (MQTT slash → AMQP dot via RabbitMQ MQTT plugin):
 *   routes/{id}         → routes.{id}
 *   tracking/{id}       → tracking.{id}
 *   alerts/sos/{id}     → alerts.sos.{id}
 *   delivered/{id}      → delivered.{id}
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/net/mqtt.h>

/** Result codes for MQTT operations. */
typedef enum
{
    MQTT_OK = 0,
    MQTT_ERR_CONNECT = -1,
    MQTT_ERR_PUBLISH = -2,
    MQTT_ERR_SUBSCRIBE = -3,
    MQTT_ERR_PAYLOAD = -4,
} mqtt_result_t;

/**
 * @brief Initialise the MQTT subsystem and connect to the broker.
 *
 * Blocks until the CONNACK is received or the timeout expires.
 * Must be called after the network interface is up (WiFi connected).
 *
 * @param broker_addr  IPv4 address string, e.g. "192.168.1.100".
 * @param broker_port  TCP port (default 1883 for RabbitMQ MQTT plugin).
 * @param employee_id  Client identifier — also used in topic names.
 * @return MQTT_OK on success, negative error code otherwise.
 */
mqtt_result_t mqtt_handler_init(const char* broker_addr, uint16_t broker_port, const char* employee_id);

/**
 * @brief Subscribe to routes/{employee_id}.
 * Must be called after mqtt_handler_init() succeeds.
 */
mqtt_result_t mqtt_handler_subscribe_routes(void);

/**
 * @brief Publish current GPS coordinates to tracking/{employee_id}.
 *
 * Payload: {"lat": <decimal>, "lon": <decimal>, "ts": "<ISO8601>"}
 *
 * @param lat_e6  Latitude  × 1e6 (integer fixed-point).
 * @param lon_e6  Longitude × 1e6.
 * @param fix_valid  False → logs "no fix" and skips publish.
 */
mqtt_result_t mqtt_handler_publish_tracking(int32_t lat_e6, int32_t lon_e6, bool fix_valid);

/**
 * @brief Publish SOS alert to alerts/sos/{employee_id}.
 *
 * Payload: {"lat": <decimal>, "lon": <decimal>, "ts": "<ISO8601>"}
 * QoS 1 — at-least-once delivery to ensure the alert is not lost.
 */
mqtt_result_t mqtt_handler_publish_sos(int32_t lat_e6, int32_t lon_e6);

/**
 * @brief Publish delivery confirmation to delivered/{employee_id}.
 *
 * Payload: {"stop": "<name>", "status": "done"}
 *
 * @param stop_name  Name of the stop that was just delivered.
 */
mqtt_result_t mqtt_handler_publish_delivered(const char* stop_name);

/**
 * @brief MQTT receive/keepalive task entry point.
 *
 * Must run in a dedicated thread. Calls mqtt_input() and mqtt_live()
 * in a loop so incoming messages (route updates) are processed and
 * the broker connection is kept alive.
 */
void mqtt_handler_task(void* p1, void* p2, void* p3);

#endif /* MQTT_HANDLER_H */

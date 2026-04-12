/**
 * @file orders.h
 * @brief Header file for the Orders module.
 * @version 0.1
 * @date 2025-04-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef ORDERS_H
#define ORDERS_H

#include "authentication.h"
#include "hub.h"
#include <cjson/cJSON.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Socket buffer size for receiving messages.
 */
#define SOCKET_BUFFER_SIZE 1024
/**
 * @brief Maximum length for the order ID.
 */
#define MAX_ORDER_ID_LENGTH 50

/**
 * @brief Handle the creation of an order.
 *
 * @param sock
 * @param hub_id
 */
void handle_create_order(int sock, const char* hub_id);

/**
 * @brief Handle the cancellation of an order.
 *
 * @param sock
 * @param hub_id
 */
void handle_cancel_order(int sock, const char* hub_id);

/**
 * @brief Handle the query of an order status.
 *
 * @param sock
 * @param hub_id
 */
void handle_query_order_status(int sock, const char* hub_id);

/**
 * @brief Create a JSON string for an order request.
 *
 * @param order_id
 * @param hub_id
 * @param timestamp
 * @param items_needed
 * @return order_request json string
 */
char* create_order_request(const char* order_id, const char* hub_id, const char* timestamp, const char* items_needed);

/**
 * @brief Send a delivery update to the server.
 *
 * Prompts the operator for an order ID and new status (Delivered or Canceled) and sends a delivery_update
 * message to the server. The server handler OrderManager::deliveryUpdate validates and persists the status.
 *
 * @param sock    Connected socket to the server.
 * @param hub_id  Identifier of the hub sending the update.
 */
void handle_delivery_update(int sock, const char* hub_id);

#endif // ORDERS_H

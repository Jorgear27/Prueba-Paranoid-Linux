/**
 * @file orders.h
 * @brief Header file for the Orders module.
 * @version 0.1
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef ORDERS_H
#define ORDERS_H

#include "authentication.h"
#include <cjson/cJSON.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Maximum length for the timestamp.
 */
#define MAX_TIMESTAMP_LENGTH 50

/**
 * @brief Send a dispatched order notification to the server.
 *
 * @param sock
 * @param order_id
 * @param items_shipped
 *
 * @return 0 on success, -1 on failure.
 */
int send_orderdispatch_to_server(int sock, const char* order_id, const char* items_shipped);

#endif // ORDERS_H

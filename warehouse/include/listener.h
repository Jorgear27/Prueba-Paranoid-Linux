/**
 * @file listener.h
 * @brief Header file for the Listener module.
 * @version 0.1
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef LISTENER_H
#define LISTENER_H

#include "authentication.h"
#include "inventory.h"
#include "orders.h"
#include <cjson/cJSON.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * @brief Socket buffer size for receiving messages.
 */
#define SOCKET_BUFFER_SIZE 1024

/**
 * @brief Listener thread function to receive updates from the server.
 *
 * @param arg Thread arguments containing socket and warehouse ID.
 * @return void*
 */
void* listen_for_request(void* arg);

#endif // LISTENER_H

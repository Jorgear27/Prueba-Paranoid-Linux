/**
 * @file warehouse.h
 * @brief Header file for the Warehouse module.
 * @version 0.1
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef WAREHOUSE_H
#define WAREHOUSE_H

#include "authentication.h"
#include "inventory.h"
#include <arpa/inet.h>
#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Server IP address default value.
 *
 */
#define SERVER_IP_DEFAULT "127.0.0.1"

/**
 * @brief Server port default value.
 *
 */
#define SERVER_PORT_DEFAULT 8080

/**
 * @brief Connect to a server using TCP/IP.
 *
 * @return int
 */
int connect_wh_to_server(void);

/**
 * @brief Disconnect from the server.
 *
 * @param sock
 * @param warehouse_id
 */
void disconnect_wh_from_server(int sock, const char* warehouse_id);

/**
 * @brief Send a message to the server.
 *
 * @param sock
 * @param message
 * @return int
 */
int send_message_from_wh(int sock, const char* message);

/**
 * @brief Receive a message from the server.
 *
 * @param sock
 * @param buffer
 * @param buffer_size
 * @return int
 */
int receive_message_wh(int sock, char* buffer, size_t buffer_size);

#endif

/**
 * @file hub.h
 * @brief Header file for the Hub module.
 * @version 0.1
 * @date 2025-04-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef HUB_H
#define HUB_H

#include "authentication.h"
#include "orders.h"
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
 * @brief Server port number default value.
 *
 */
#define SERVER_PORT_DEFAULT 8080

/**
 * @brief Connect to a server using TCP/IP.
 *
 * @return int
 */
int connect_hub_to_server(void);

/**
 * @brief Disconnect from the server.
 *
 * @param sock
 * @param hub_id
 */
void disconnect_hub_from_server(int sock, const char* hub_id);

/**
 * @brief Send a message to the server.
 *
 * @param sock
 * @param message
 * @return number of bytes sent
 */
int send_message_from_hub(int sock, const char* message);

/**
 * @brief Receive a message from the server.
 *
 * @param sock
 * @param buffer
 * @param buffer_size
 * @return number of bytes received
 */
int receive_message_hub(int sock, char* buffer, size_t buffer_size);

#endif // HUB_H

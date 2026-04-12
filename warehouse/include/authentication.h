/**
 * @file authentication.h
 * @brief Header file for the Authentication module.
 * @version 0.2
 * @date 2025-07-15
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef AUTHENTICATION_H
#define AUTHENTICATION_H

#include "warehouse.h"
#include <cjson/cJSON.h>
#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Maximum length for the timestamp.
 */
#define MAX_TIMESTAMP_LENGTH 50

/**
 * @brief Maximum number of transport connections a node can declare.
 */
#define MAX_CONNECTIONS 32

/**
 * @brief Maximum length for a node ID string (e.g. "W001", "H042").
 */
#define MAX_NODE_ID_LENGTH 32

/**
 * @brief Maximum length for a connection type string (e.g. "rail", "road").
 */
#define MAX_CONN_TYPE_LENGTH 16

/**
 * @brief Represents a single transport connection to a neighbouring node.
 *
 * Declared by the warehouse at authentication time. The server stores these
 * in the connections table and uses them to build the live routing graph.
 */
typedef struct
{
    char target_node_id[MAX_NODE_ID_LENGTH];    ///< ID of the connected node.
    double base_weight;                         ///< Raw transport cost (> 0).
    char connection_type[MAX_CONN_TYPE_LENGTH]; ///< E.g. "road", "rail".
    char connection_conditions[256];            ///< JSON array string, e.g. '["foggy"]'.
} Connection;

/**
 * @brief Generate a timestamp in ISO 8601 format.
 *
 */
void generate_timestamp_wh(char* buffer, size_t size);

/**
 * @brief Create a JSON string for client information.
 *
 * Serialises the warehouse identity, location, security flag, and transport connections
 * into the client_info JSON format expected by the server's Authentication::processClientInfo().
 *
 * @param wh_id        Warehouse identifier (must start with 'W').
 * @param latitude     Geographic latitude.
 * @param longitude    Geographic longitude.
 * @param is_secure    True if the node is in a secure zone.
 * @param connections  Array of Connection structs (may be NULL).
 * @param conn_count   Number of entries in connections[].
 * @return Heap-allocated JSON string; caller must free().
 */
char* create_client_info_warehouse(const char* wh_id, int latitude, int longitude, bool is_secure,
                                   const Connection* connections, int conn_count);

/**
 * @brief Validate the warehouse ID format.
 *
 * @param wh_id
 * @return true if valid, false otherwise
 */
bool isValidWhId(const char* wh_id);

#endif // AUTHENTICATION_H

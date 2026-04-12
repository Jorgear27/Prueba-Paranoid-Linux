/**
 * @file inventory.h
 * @brief Header file for the Inventory module.
 * @version 0.1
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef INVENTORY_H
#define INVENTORY_H

#include "authentication.h"
#include "warehouse.h"
#include <cjson/cJSON.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Thread arguments structure.
 *
 */
typedef struct
{
    /**
     * @brief Socket descriptor for communication.
     *
     */
    int sock;
    /**
     * @brief Warehouse ID.
     *
     */
    const char* wh_id;
} ThreadArgs;

/**
 * @brief Inventory item structure.
 *
 */
typedef struct
{
    /**
     * @brief Item type (e.g., item ID).
     *
     */
    int item_type;
    /**
     * @brief Threshold level for restocking.
     *
     */
    int threshold;
    /**
     * @brief Stock level of the item.
     *
     */
    int stock_level;
} InventoryItem;

/**
 * @brief Inventory structure.
 *
 */
typedef struct
{
    /**
     * @brief Inventory items.
     *
     */
    InventoryItem* items;
    /**
     * @brief Number of items in the inventory.
     *
     */
    int item_count;
} Inventory;

/**
 * @brief Socket buffer size for receiving messages.
 */
#define SOCKET_BUFFER_SIZE 1024

/**
 * @brief Item types in the inventory.
 *
 */
#define MAX_ITEM_TYPES 3

/**
 * @brief Maximum quantity for each item type.
 *
 */
#define MAX_ITEM_QUANTITY 1000

/**
 * @brief Threshold for item restocking.
 *
 */
#define ITEM_THRESHOLD 100

/**
 * @brief Size of the buffer for converting integers to strings.
 *
 */
#define INT_TO_STRING_BUFFER_SIZE 12

/**
 * @brief Sleep time in seconds for periodic inventory updates.
 *
 */
#define SLEEP_TIME 60

/**
 * @brief Path to the key file for the semaphore.
 */
#define SEM_PATH "/tmp"

/**
 * @brief Semaphore project ID for the inventory.
 *
 */
#define SEM_PROJ_ID 'A' // Arbitrary character

/**
 * @brief Value for binary semaphore initialization.
 *
 */
#define SEM_INITIAL_VALUE 1

/**
 * @brief Initialize the inventory with a given number of items.
 *
 * @param item_count Number of items in the inventory.
 */
void initialize_inventory(int item_count);

/**
 * @brief Update the inventory with an added quantity of an item.
 *
 * @param item_type Type of the item to update.
 * @param quantity Quantity of the item to update.
 */
void update_inventory(int item_type, int quantity);

/**
 * @brief Set the inventory for a specific item type quantity.
 *
 * @param item_type Type of the item to set.
 * @param stock_level Quantity of the item to set.
 */
void set_inventory(int item_type, int stock_level);

/**
 * @brief Restock an item if its quantity is below the threshold.
 *
 * @param item_type Type of the item to check.
 * @return int 1 if restock is triggered, 0 otherwise.
 */
int check_restock_item(int item_type);

/**
 * @brief Get the current inventory.
 *
 * @return Inventory*
 */
Inventory* get_inventory(void);

/**
 * @brief Print the current inventory.
 *
 */
void print_inventory(void);

/**
 * @brief Free the inventory resources.
 *
 */
void free_inventory(void);

/**
 * @brief Send the inventory to the server.
 *
 * @param sock
 * @param wh_id
 */
void send_inventory_to_server(int sock, const char* wh_id);

/**
 * @brief Handle the restock of inventory.
 *
 * @param sock
 * @param wh_id
 * @param item_type
 */
void send_restock_to_server(int sock, const char* wh_id, int item_type);

/**
 * @brief Periodically update the inventory.
 *
 * @param args
 * @return void*
 */
void* periodic_inventory_update(void* args);

/**
 * @brief Initialize the semaphore for inventory management.
 *
 * @return -1 on failure, semaphore ID on success.
 */
int init_semaphore(void);

/**
 * @brief Lock the inventory for thread-safe operations.
 *
 */
void lock_inventory(void);

/**
 * @brief Unlock the inventory after thread-safe operations.
 *
 */
void unlock_inventory(void);

#endif // INVENTORY_H

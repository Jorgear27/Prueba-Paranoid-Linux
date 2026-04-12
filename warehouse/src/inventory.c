#include "inventory.h"

static Inventory inventory; // Global inventory instance
static int semid;           // Semaphore ID for synchronization

#ifdef TESTING
int test_status = 0; // Global variable to track status during testing
#endif

/**
 * @brief Union to hold semaphore information.
 *
 * This union is used for configuring and managing semaphores.
 * It contains various fields for semaphore operations.
 */
union semun {
    int val;               /**< Value for SETVAL operation. */
    struct semid_ds* buf;  /**< Buffer for IPC_STAT and IPC_SET operations. */
    unsigned short* array; /**< Array for GETALL and SETALL operations. */
    struct seminfo* __buf; /**< Buffer for IPC_INFO (Linux-specific). */
};

void initialize_inventory(int item_count)
{
    inventory.items = (InventoryItem*)malloc(sizeof(InventoryItem) * item_count);
    inventory.item_count = item_count;

    // Initialize semaphore
    semid = init_semaphore();
    if (semid == -1)
    {
        fprintf(stderr, "[ERROR] Failed to initialize semaphore\n");
        exit(EXIT_FAILURE);
    }

    // Initialize all items with default values
    for (int i = 0; i < item_count; i++)
    {
        inventory.items[i].item_type = i;
        inventory.items[i].threshold = ITEM_THRESHOLD;
        inventory.items[i].stock_level = MAX_ITEM_QUANTITY;
    }
}

void update_inventory(int item_type, int quantity)
{
    lock_inventory();

    for (int i = 0; i < inventory.item_count; i++)
    {
        if (inventory.items[i].item_type == item_type)
        {
            inventory.items[i].stock_level += quantity;
            printf("[INFO] Updated inventory: Item %d, New Stock Level: %d\n", item_type,
                   inventory.items[i].stock_level);
            break;
        }
    }

    unlock_inventory();
}

void set_inventory(int item_type, int stock_level)
{
    lock_inventory();

    for (int i = 0; i < inventory.item_count; i++)
    {
        if (inventory.items[i].item_type == item_type)
        {
            inventory.items[i].stock_level = stock_level;
            printf("[INFO] Updated inventory: Item %d, New Stock Level: %d\n", item_type,
                   inventory.items[i].stock_level);
            break;
        }
    }

    unlock_inventory();
}

int check_restock_item(int item_type)
{
    lock_inventory();

    for (int i = 0; i < inventory.item_count; i++)
    {
        if (inventory.items[i].item_type == item_type)
        {
            if (inventory.items[i].stock_level < inventory.items[i].threshold)
            {
                // Trigger restock if below threshold
                printf("[WARNING] Item %d below threshold! Current stock_level: %d, Threshold: %d\n", item_type,
                       inventory.items[i].stock_level, inventory.items[i].threshold);
                // Manually restock
                inventory.items[i].stock_level = MAX_ITEM_QUANTITY;
                printf("[INFO] Restocked item %d. New Stock Level: %d\n", item_type, inventory.items[i].stock_level);
                unlock_inventory();
                return 1; // Restock triggered
            }
            break;
        }
    }

    unlock_inventory();
    return 0; // No restock needed
}

Inventory* get_inventory()
{
    return &inventory;
}

void print_inventory()
{
    lock_inventory();

    printf("[INFO] Current Inventory:\n");
    for (int i = 0; i < inventory.item_count; i++)
    {
        printf("Item Type: %d, Stock Level: %d\n", inventory.items[i].item_type, inventory.items[i].stock_level);
    }

    unlock_inventory();
}

void free_inventory()
{
    free(inventory.items);
    if (semctl(semid, 0, IPC_RMID) == -1)
    {
        perror("[ERROR] semctl IPC_RMID failed");
    }
}

void send_inventory_to_server(int sock, const char* wh_id)
{
    Inventory* inventory = get_inventory();
    if (wh_id == NULL)
    {
        printf("[ERROR] Warehouse ID is NULL. Cannot send inventory to server.\n");
        return;
    }
    // Create the root JSON object
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "inventory_update");
    cJSON_AddStringToObject(root, "user_id", wh_id);

    // Generate a timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_wh(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    // Create the inventory array
    cJSON* inventory_array = cJSON_CreateArray();

    lock_inventory(); // Lock the inventory for thread-safe access

    for (int i = 0; i < inventory->item_count; i++)
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "item_type", inventory->items[i].item_type);
        cJSON_AddNumberToObject(item, "stock_level", inventory->items[i].stock_level);
        cJSON_AddNumberToObject(item, "threshold", inventory->items[i].threshold);
        cJSON_AddItemToArray(inventory_array, item);
    }

    unlock_inventory();

    cJSON_AddItemToObject(root, "inventory", inventory_array);

    // Convert the JSON object to a string
    char* json_string = cJSON_PrintUnformatted(root);

    // Send the JSON string to the server
    if (send_message_from_wh(sock, json_string) < 0)
    {
        perror("[ERROR] Failed to send inventory to server");
#ifdef TESTING
        test_status = -2; // Set status for failure
#endif
    }
    else
    {
        printf("[INFO] Inventory sent to server: %s\n", json_string);
    }

    // Clean up
    free(json_string);
    cJSON_Delete(root);
}

void send_restock_to_server(int sock, const char* wh_id, int item_type)
{
    // Create the root JSON object
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "restock_notice");
    cJSON_AddStringToObject(root, "user_id", wh_id);

    // Generate a timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_wh(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    // Add item type and stock_level to the JSON object as an int
    cJSON_AddNumberToObject(root, "item_type", item_type);
    cJSON_AddNumberToObject(root, "stock_level", MAX_ITEM_QUANTITY);

    // Convert the JSON object to a string
    char* json_string = cJSON_PrintUnformatted(root);

    // Send the JSON string to the server
    if (send_message_from_wh(sock, json_string) < 0)
    {
        perror("[ERROR] Failed to send restock notice to server");
#ifdef TESTING
        test_status = -3; // Set status for failure
#endif
    }
    else
    {
        printf("[INFO] Restock notice sent to server: %s\n", json_string);
    }

    // Clean up
    free(json_string);
    cJSON_Delete(root);
}

void* periodic_inventory_update(void* args)
{
    // Cast the argument to ThreadArgs*
    ThreadArgs* thread_args = (ThreadArgs*)args;

    // Extract the socket and warehouse ID
    int sock = thread_args->sock;
    const char* wh_id = thread_args->wh_id;

    while (true)
    {
        // Wait before the next update
        sleep(SLEEP_TIME);

        // Send the inventory to the server
        send_inventory_to_server(sock, wh_id);
    }

    return NULL;
}

int init_semaphore()
{
    key_t key = ftok(SEM_PATH, SEM_PROJ_ID);
    if (key == -1)
    {
        perror("[ERROR] ftok failed");
        return -1;
    }

    // Create a semaphore set with one semaphore
    // Flags: IPC_CREAT: create the semaphore if it doesn't exist
    // IPC_EXCL: fail if the semaphore already exists
    int semid = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semid == -1)
    {
        if (errno == EEXIST)
        {
            // Semaphore already exists, get its ID
            semid = semget(key, 1, 0);
            if (semid == -1)
            {
                perror("[ERROR] semget failed");
                return -1;
            }
        }
        else
        {
            perror("[ERROR] semget failed");
            return -1;
        }
    }
    else
    {
        // Semaphore created successfully, initialize it to 1
        union semun sem_opts;
        sem_opts.val = SEM_INITIAL_VALUE;

        // Initialize the semaphore
        if (semctl(semid, 0, SETVAL, sem_opts) == -1)
        {
            perror("[ERROR] semctl failed");
            return -1;
        }
    }

    return semid;
}

void lock_inventory()
{
    struct sembuf sem_op = {0, -1, 0}; // sem_op = -1 (access)
    if (semop(semid, &sem_op, 1) == -1)
    {
        perror("[ERROR] semop lock failed");
        exit(EXIT_FAILURE);
    }
}

void unlock_inventory()
{
    struct sembuf sb = {0, 1, 0}; // sem_op = 1 (release)
    if (semop(semid, &sb, 1) == -1)
    {
        perror("[ERROR] semop unlock failed");
        exit(EXIT_FAILURE);
    }
}

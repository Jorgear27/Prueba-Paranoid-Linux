#include "inventory.h"
#include "unity.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <setjmp.h>
#include <sys/socket.h>
#include <sys/types.h>

#define TEST_ITEM_COUNT 3
#define TEST_ITEM_TYPE 2
#define TEST_QUANTITY_POSITIVE 10
#define TEST_QUANTITY_NEGATIVE -10
#define TEST_STOCK_LEVEL 50
#define TEST_WAREHOUSE_ID "W005"
#define TEST_PORT 8080
#define TEST_IP "127.0.0.1"
#define TEST_SOCKET 42
#define KEY_RESULT_SUCCESS 1234
#define MOCK_ERROR -1
#define MOCK_SUCCESS 0

static int mock_send_message_result = 0;    // Global variable to simulate send_message_from_wh result
extern int test_status;                     // Declare the global variable from listener.c
static int key_result = KEY_RESULT_SUCCESS; // Global variable to simulate ftok result
static int semget_result = MOCK_SUCCESS;    // Global variable to simulate semget result
static int semctl_result = MOCK_SUCCESS;    // Global variable to simulate semctl result
static int semop_result = MOCK_SUCCESS;     // Global variable to simulate semop result
static char mock_message_buffer[1024];      // Buffer to store the mock message
static char last_perror_msg[256];           // Buffer to store the last error message
static int exit_called = 0;                 // Variable to check if exit was called
jmp_buf test_exit_env;                      // Buffer for longjmp

void setUp(void);
void tearDown(void);
void test_initialize_inventory(void);
void test_update_inventory_addition(void);
void test_update_inventory_remove(void);
void test_set_inventory(void);
void test_check_restock_item_no_restock(void);
void test_check_restock_item_with_restock(void);
void test_send_inventory_to_server(void);
void test_send_inventory_null_parameter(void);
void test_send_inventory_error_send(void);
void test_send_restock_to_server(void);
void test_send_restock_error_send(void);
void test_print_inventory(void);
void test_init_semaphore(void);
void test_init_semaphore_error_key(void);
void test_init_semaphore_error_semget(void);
void test_init_semaphore_error_semctl(void);
void test_lock_inventory(void);
void test_lock_inventory_error(void);
void test_unlock_inventory(void);
void test_unlock_inventory_error(void);
int main(void);

// Mock definitions
#ifdef TESTING
// Mock prototypes
int send_message_from_wh(int sock, const char* message);
void generate_timestamp_wh(char* buffer, size_t size);
key_t ftok(const char* pathname, int proj_id);
int semget(key_t key, int nsems, int semflg);
int semctl(int semid, int semnum, int cmd, ...);
int semop(int semid, struct sembuf* sops, size_t nsops);
void perror(const char* msg);
void exit(int code);

int send_message_from_wh(int sock, const char* message)
{
    (void)sock;
    strncpy(mock_message_buffer, message, sizeof(mock_message_buffer) - 1);
    printf("[MOCK] Sending message: %s\n", message);
    return mock_send_message_result; // Simulate success or failure
}

void generate_timestamp_wh(char* buffer, size_t size)
{
    // Simulate a fixed timestamp for testing
    const char* mock_timestamp = "2025-01-01T00:00:00Z";
    strncpy(buffer, mock_timestamp, size - 1);
    buffer[size - 1] = '\0';
}

key_t ftok(const char* pathname, int proj_id)
{
    // Simulate a fixed key for testing
    (void)pathname;
    (void)proj_id;
    return key_result;
}

int semget(key_t key, int nsems, int semflg)
{
    // Simulate a fixed semaphore ID for testing
    (void)key;
    (void)nsems;
    (void)semflg;
    return semget_result;
}

int semctl(int semid, int semnum, int cmd, ...)
{
    // Simulate semaphore control operations
    (void)semid;
    (void)semnum;
    (void)cmd;
    return semctl_result;
}

int semop(int semid, struct sembuf* sops, size_t nsops)
{
    // Simulate semaphore operations
    (void)semid;
    (void)sops;
    (void)nsops;
    return semop_result;
}

void perror(const char* msg)
{
    strncpy(last_perror_msg, msg, sizeof(last_perror_msg) - 1);
    last_perror_msg[sizeof(last_perror_msg) - 1] = '\0';
}

void exit(int code)
{
    exit_called = code;
    longjmp(test_exit_env, 1);
}

#endif

void setUp(void)
{
    initialize_inventory(TEST_ITEM_COUNT);
}

void tearDown(void)
{
    free_inventory();
}

// Test initialization of inventory
void test_initialize_inventory(void)
{
    printf("Testing initialize_inventory...\n");
    Inventory* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv->items);
    TEST_ASSERT_EQUAL_INT(MAX_ITEM_TYPES, inv->item_count);

    for (int i = 0; i < MAX_ITEM_TYPES; i++)
    {
        TEST_ASSERT_EQUAL_INT(i, inv->items[i].item_type);
        TEST_ASSERT_EQUAL_INT(ITEM_THRESHOLD, inv->items[i].threshold);
        TEST_ASSERT_EQUAL_INT(MAX_ITEM_QUANTITY, inv->items[i].stock_level);
    }
}

// Test updating inventory by adding items
void test_update_inventory_addition(void)
{
    printf("Testing update_inventory to add items...\n");
    update_inventory(TEST_ITEM_TYPE, TEST_QUANTITY_POSITIVE);

    Inventory* inv = get_inventory();
    TEST_ASSERT_EQUAL_INT(MAX_ITEM_QUANTITY + TEST_QUANTITY_POSITIVE, inv->items[TEST_ITEM_TYPE].stock_level);
}

// Test updating inventory by removing items
void test_update_inventory_remove(void)
{
    printf("Testing update_inventory to remove items...\n");
    update_inventory(TEST_ITEM_TYPE, TEST_QUANTITY_NEGATIVE);

    Inventory* inv = get_inventory();
    TEST_ASSERT_EQUAL_INT(MAX_ITEM_QUANTITY + TEST_QUANTITY_NEGATIVE, inv->items[TEST_ITEM_TYPE].stock_level);
}

// Test setting inventory directly
void test_set_inventory(void)
{
    printf("Testing set_inventory forced...\n");
    set_inventory(TEST_ITEM_TYPE, TEST_STOCK_LEVEL);

    Inventory* inv = get_inventory();
    TEST_ASSERT_EQUAL_INT(TEST_STOCK_LEVEL, inv->items[TEST_ITEM_TYPE].stock_level);
}

// Test checking if restock is needed, without triggering restock
void test_check_restock_item_no_restock(void)
{
    printf("Testing check_restock_item with no restock needed...\n");
    set_inventory(TEST_ITEM_TYPE, ITEM_THRESHOLD + 1);
    int restock_triggered = check_restock_item(TEST_ITEM_TYPE);
    TEST_ASSERT_EQUAL_INT(0, restock_triggered);

    Inventory* inv = get_inventory();
    TEST_ASSERT_EQUAL_INT((ITEM_THRESHOLD + 1), inv->items[TEST_ITEM_TYPE].stock_level);
}

// Test checking if restock is needed, triggering restock
void test_check_restock_item_with_restock(void)
{
    printf("Testing check_restock_item with restock needed...\n");
    set_inventory(TEST_ITEM_TYPE, ITEM_THRESHOLD - 1);
    int restock_triggered = check_restock_item(TEST_ITEM_TYPE);
    TEST_ASSERT_EQUAL_INT(1, restock_triggered);

    Inventory* inv = get_inventory();
    TEST_ASSERT_EQUAL_INT(MAX_ITEM_QUANTITY, inv->items[TEST_ITEM_TYPE].stock_level);
}

// Test sending inventory to server
void test_send_inventory_to_server(void)
{
    printf("Testing send_inventory_to_server...\n");
    // Set up the inventory
    set_inventory(TEST_ITEM_TYPE, TEST_STOCK_LEVEL);
    send_inventory_to_server(TEST_SOCKET, TEST_WAREHOUSE_ID);

    // Check if the message was sent
    TEST_ASSERT_NOT_EQUAL(0, strlen(mock_message_buffer));
    cJSON* root = cJSON_Parse(mock_message_buffer);
    TEST_ASSERT_NOT_NULL(root);

    // Validate JSON structure
    cJSON* type = cJSON_GetObjectItem(root, "type");
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_STRING("inventory_update", type->valuestring);

    cJSON* user_id = cJSON_GetObjectItem(root, "user_id");
    TEST_ASSERT_NOT_NULL(user_id);
    TEST_ASSERT_EQUAL_STRING(TEST_WAREHOUSE_ID, user_id->valuestring);

    // Validate inventory array
    cJSON* inventory_array = cJSON_GetObjectItem(root, "inventory");
    TEST_ASSERT_NOT_NULL(inventory_array);
    TEST_ASSERT(cJSON_IsArray(inventory_array));

    // Ensure TEST_ITEM_TYPE is within bounds
    int inventory_size = cJSON_GetArraySize(inventory_array);
    TEST_ASSERT(TEST_ITEM_TYPE >= 0 && TEST_ITEM_TYPE < inventory_size);

    // Access the item at the position corresponding to TEST_ITEM_TYPE
    cJSON* item = cJSON_GetArrayItem(inventory_array, TEST_ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(item);

    // Validate the item_type and quantity
    cJSON* item_type = cJSON_GetObjectItem(item, "item_type");
    TEST_ASSERT_NOT_NULL(item_type);
    TEST_ASSERT_EQUAL_INT(TEST_ITEM_TYPE, item_type->valueint);

    cJSON* quantity = cJSON_GetObjectItem(item, "stock_level");
    TEST_ASSERT_NOT_NULL(quantity);
    TEST_ASSERT_EQUAL_INT(TEST_STOCK_LEVEL, quantity->valueint);

    // Clean up
    cJSON_Delete(root);
}

// Test sending inventory with null parameter
void test_send_inventory_null_parameter(void)
{
    // Clean mock message buffer
    memset(mock_message_buffer, 0, sizeof(mock_message_buffer));

    printf("Testing test_send_inventory_null_parameter...\n");
    set_inventory(TEST_ITEM_TYPE, TEST_STOCK_LEVEL);
    send_inventory_to_server(TEST_SOCKET, NULL);

    // Check if the message was not sent
    TEST_ASSERT_EQUAL(0, strlen(mock_message_buffer));
}

// Test sending inventory with failure in send_message_from_wh
void test_send_inventory_error_send(void)
{
    // Clean mock message buffer
    memset(mock_message_buffer, 0, sizeof(mock_message_buffer));

    mock_send_message_result = -1; // Simulate failure in other module
    printf("Testing test_send_inventory_error_send...\n");
    send_inventory_to_server(TEST_SOCKET, TEST_WAREHOUSE_ID);

    // Check if the message was not sent
    TEST_ASSERT_EQUAL(-2, test_status);
}

// Test sending restock notice to server
void test_send_restock_to_server(void)
{
    printf("Testing send_restock_to_server...\n");

    // Send a restock notice
    send_restock_to_server(TEST_SOCKET, TEST_WAREHOUSE_ID, TEST_ITEM_TYPE);

    // Check if the message was sent
    TEST_ASSERT_NOT_EQUAL(0, strlen(mock_message_buffer));
    cJSON* root = cJSON_Parse(mock_message_buffer);
    TEST_ASSERT_NOT_NULL(root);

    // Validate JSON structure
    cJSON* type = cJSON_GetObjectItem(root, "type");
    TEST_ASSERT_NOT_NULL(type);
    TEST_ASSERT_EQUAL_STRING("restock_notice", type->valuestring);

    cJSON* item_type = cJSON_GetObjectItem(root, "item_type");
    TEST_ASSERT_NOT_NULL(item_type);
    TEST_ASSERT_EQUAL_INT(TEST_ITEM_TYPE, item_type->valueint);

    cJSON* quantity = cJSON_GetObjectItem(root, "stock_level");
    TEST_ASSERT_NOT_NULL(quantity);
    TEST_ASSERT_EQUAL_INT(MAX_ITEM_QUANTITY, quantity->valueint);

    // Clean up
    cJSON_Delete(root);
}

// Test sending restock with failure in send_message_from_wh
void test_send_restock_error_send(void)
{
    // Clean mock message buffer
    memset(mock_message_buffer, 0, sizeof(mock_message_buffer));

    mock_send_message_result = -1; // Simulate failure in other module
    printf("Testing test_send_restock_error_send...\n");
    send_restock_to_server(TEST_SOCKET, TEST_WAREHOUSE_ID, TEST_ITEM_TYPE);

    // Check if the message was not sent
    TEST_ASSERT_EQUAL(-3, test_status);
}

// Test printing inventory
void test_print_inventory(void)
{
    printf("Testing print_inventory...\n");
    print_inventory();
    Inventory* inv = get_inventory();
    for (int i = 0; i < MAX_ITEM_TYPES; i++)
    {
        printf("Item Type: %d, Stock Level: %d\n", inv->items[i].item_type, inv->items[i].stock_level);
    }
    // Check if the inventory is printed correctly
    for (int i = 0; i < MAX_ITEM_TYPES; i++)
    {
        TEST_ASSERT_EQUAL_INT(i, inv->items[i].item_type);
        TEST_ASSERT_EQUAL_INT(MAX_ITEM_QUANTITY, inv->items[i].stock_level);
    }
    // Check if the inventory is printed correctly
}

// Test initializing semaphore successfully
void test_init_semaphore(void)
{
    printf("Testing init_semaphore...\n");
    key_result = KEY_RESULT_SUCCESS;
    semget_result = MOCK_SUCCESS;
    semctl_result = MOCK_SUCCESS;
    semop_result = MOCK_SUCCESS;

    int semid = init_semaphore();
    TEST_ASSERT_NOT_EQUAL(-1, semid);

    // Clean up
    if (semctl(semid, 0, IPC_RMID) == -1)
    {
        perror("[ERROR] semctl IPC_RMID failed");
    }
}

// Test initializing semaphore with error in key
void test_init_semaphore_error_key(void)
{
    printf("Testing init_semaphore_error...\n");
    key_result = MOCK_ERROR;

    int semid = init_semaphore();
    TEST_ASSERT_EQUAL(-1, semid);

    key_result = KEY_RESULT_SUCCESS; // Reset key result
}

// Test initializing semaphore with error in semget
void test_init_semaphore_error_semget(void)
{
    printf("Testing init_semaphore_error_semget...\n");
    semget_result = MOCK_ERROR;

    int semid = init_semaphore();
    TEST_ASSERT_EQUAL(-1, semid);

    semget_result = MOCK_SUCCESS; // Reset semaphore get result
}

// Test initializing semaphore with error in semctl
void test_init_semaphore_error_semctl(void)
{
    printf("Testing init_semaphore_error_semctl...\n");
    semctl_result = MOCK_ERROR;

    int semid = init_semaphore();
    TEST_ASSERT_EQUAL(-1, semid);

    semctl_result = MOCK_SUCCESS; // Reset semaphore control result
}

// Test locking inventory
void test_lock_inventory(void)
{
    printf("Testing lock_inventory...\n");
    semop_result = MOCK_SUCCESS; // Simulate successful semaphore operation
    lock_inventory();
    TEST_ASSERT_EQUAL_INT(0, exit_called);

    exit_called = 0; // Reset exit_called for next test
}

// Test locking inventory with error
void test_lock_inventory_error(void)
{
    printf("Testing lock_inventory_error...\n");
    semop_result = MOCK_ERROR; // Simulate error in semaphore operation

    if (setjmp(test_exit_env) == 0)
    {
        lock_inventory();
        TEST_FAIL_MESSAGE("lock_inventory did not call exit as expected");
    }
    else
    {
        // Check if exit was called
        TEST_ASSERT_EQUAL_STRING("[ERROR] semop lock failed", last_perror_msg);
        TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, exit_called);
    }

    exit_called = 0;             // Reset exit_called for next test
    semop_result = MOCK_SUCCESS; // Reset semaphore operation result
}

// Test unlocking inventory
void test_unlock_inventory(void)
{
    printf("Testing unlock_inventory...\n");
    semop_result = MOCK_SUCCESS; // Simulate successful semaphore operation
    unlock_inventory();
    TEST_ASSERT_EQUAL_INT(0, exit_called);

    exit_called = 0; // Reset exit_called for next test
}

// Test unlocking inventory with error
void test_unlock_inventory_error(void)
{
    printf("Testing unlock_inventory_error...\n");
    semop_result = MOCK_ERROR; // Simulate error in semaphore operation

    if (setjmp(test_exit_env) == 0)
    {
        unlock_inventory();
        TEST_FAIL_MESSAGE("lock_inventory did not call exit as expected");
    }
    else
    {
        // Check that exit was called with the correct code
        TEST_ASSERT_EQUAL_STRING("[ERROR] semop unlock failed", last_perror_msg);
        TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, exit_called);
    }
    exit_called = 0;             // Reset exit_called for next test
    semop_result = MOCK_SUCCESS; // Reset semaphore operation result
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initialize_inventory);
    RUN_TEST(test_update_inventory_addition);
    RUN_TEST(test_update_inventory_remove);
    RUN_TEST(test_set_inventory);
    RUN_TEST(test_check_restock_item_no_restock);
    RUN_TEST(test_check_restock_item_with_restock);
    RUN_TEST(test_send_inventory_to_server);
    RUN_TEST(test_send_inventory_null_parameter);
    RUN_TEST(test_send_inventory_error_send);
    RUN_TEST(test_send_restock_to_server);
    RUN_TEST(test_send_restock_error_send);
    RUN_TEST(test_print_inventory);
    RUN_TEST(test_init_semaphore);
    RUN_TEST(test_init_semaphore_error_key);
    RUN_TEST(test_init_semaphore_error_semget);
    RUN_TEST(test_init_semaphore_error_semctl);
    RUN_TEST(test_lock_inventory);
    RUN_TEST(test_lock_inventory_error);
    RUN_TEST(test_unlock_inventory);
    RUN_TEST(test_unlock_inventory_error);
    return UNITY_END();
}

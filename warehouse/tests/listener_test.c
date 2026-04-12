#include "listener.h"
#include "unity.h"
#include <cjson/cJSON.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define MOCK_SOCKET_DESCRIPTOR 42 // Mock socket descriptor for testing

// Prototypes
void setUp(void);
void tearDown(void);
void test_listen_for_request_valid_message(void);
void test_listen_for_request_invalid_json(void);
void test_listen_for_request_no_data(void);
void test_listen_for_request_socket_error(void);
void test_listen_for_request_invalid_order_format(void);
int main(void);

// Mock definitions
#ifdef TESTING
extern int test_status; // Declare the global variable from listener.c
static int simulate_recv_failure = 0;
static const char* mock_recv_message = NULL;

int receive_message_wh(int sock, char* buffer, size_t buffer_size);
void update_inventory(int item_type, int quantity);
int check_restock_item(int item_type);
void send_restock_to_server(int sock, const char* wh_id, int item_type);
int send_orderdispatch_to_server(int sock, const char* order_id, const char* items_needed);
void send_inventory_to_server(int sock, const char* wh_id);

int receive_message_wh(int sock, char* buffer, size_t buffer_size)
{
    (void)sock;

    if (simulate_recv_failure)
    {
        errno = EBADF; // Simulate a socket error
        return -1;
    }

    if (mock_recv_message == NULL)
    {
        fprintf(stderr, "Error: mock_recv_message is NULL\n");
        errno = 0; // SImulate deconnection
        return 0;
    }

    size_t message_length = strlen(mock_recv_message);
    if (message_length >= buffer_size)
    {
        fprintf(stderr, "Error: buffer size is too small for the message\n");
        return -1;
    }

    strncpy(buffer, mock_recv_message, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return message_length;
}

// Mock implementations for inventory and order functions
void update_inventory(int item_type, int quantity)
{
    printf("[MOCK] update_inventory called with item_type=%d, quantity=%d\n", item_type, quantity);
}

int check_restock_item(int item_type)
{
    printf("[MOCK] check_restock_item called with item_type=%d\n", item_type);
    return 0; // Simulate that restocking is not needed
}

void send_restock_to_server(int sock, const char* wh_id, int item_type)
{
    printf("[MOCK] send_restock_to_server called with sock=%d, wh_id=%s, item_type=%d\n", sock, wh_id, item_type);
}

int send_orderdispatch_to_server(int sock, const char* order_id, const char* items_needed)
{
    printf("[MOCK] send_orderdispatch_to_server called with sock=%d, order_id=%s, items_needed=%s\n", sock, order_id,
           items_needed);
    return 0; // Simulate success
}

void send_inventory_to_server(int sock, const char* wh_id)
{
    (void)sock;
    (void)wh_id;
    printf("[MOCK] send_inventory_to_server called");
}
#endif

void setUp()
{
    // Setup code before each test
    simulate_recv_failure = 0;
    mock_recv_message = NULL;
}

void tearDown()
{
    // Cleanup code after each test
}

// Test receiving valid JSON messages
void test_listen_for_request_valid_message()
{
    printf("Testing listen_for_request with a valid message...\n");
    mock_recv_message = "{\"type\":\"supply_request\",\"timestamp\":\"2025-05-02T12:00:00Z\",\"order_id\":\"H002_3\","
                        "\"items_needed\":[{\"item_type\":1,\"quantity\":10}]}";
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_request(&sock);
    TEST_ASSERT_EQUAL(0, test_status); // Expect success
}

// Test receiving invalid JSON messages
void test_listen_for_request_invalid_json()
{
    printf("Testing listen_for_request with a invalid json...\n");
    mock_recv_message = "invalid_json";
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_request(&sock);
    TEST_ASSERT_EQUAL(-2, test_status); // Expect invalid JSON error
}

// Test receiving no data
void test_listen_for_request_no_data()
{
    printf("Testing listen_for_request with null data...\n");
    mock_recv_message = NULL; // Simulate disconnection from server
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_request(&sock);
    TEST_ASSERT_EQUAL(-1, test_status); // Expect failure
}

// Test socket error
void test_listen_for_request_socket_error()
{
    printf("Testing listen_for_request with a socket error...\n");
    simulate_recv_failure = 1; // Simulate socket error
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_request(&sock);
    TEST_ASSERT_EQUAL(-1, test_status); // Expect failure
}

// Test receiving invalid order format
void test_listen_for_request_invalid_order_format()
{
    printf("Testing listen_for_request with a invalid format json...\n");
    mock_recv_message = "{\"type\":\"supply_request\",\"timestamp\":\"2025-04-01T12:10:00Z\",\"order_id\":\"H002_3\","
                        "\"item_type\": 1, \"quantity\": 50}";
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_request(&sock);
    TEST_ASSERT_EQUAL(-3, test_status);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_listen_for_request_valid_message);
    RUN_TEST(test_listen_for_request_invalid_json);
    RUN_TEST(test_listen_for_request_no_data);
    RUN_TEST(test_listen_for_request_socket_error);
    RUN_TEST(test_listen_for_request_invalid_order_format);

    return UNITY_END();
}

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
void test_listen_for_updates_valid_message(void);
void test_listen_for_updates_invalid_json(void);
void test_listen_for_updates_no_data(void);
void test_listen_for_updates_socket_error(void);
void test_listen_for_updates_invalid_order_format(void);
int main(void);

// Mock definitions
#ifdef TESTING
extern int test_status; // Declare the global variable from listener.c
static int simulate_recv_failure = 0;
static const char* mock_recv_message = NULL;

ssize_t recv(int sockfd, void* buf, size_t len, int flags)
{
    (void)sockfd;
    (void)flags;

    if (simulate_recv_failure)
    {
        errno = EBADF; // Set errno to indicate a bad file descriptor
        return -1;     // Simulate failure
    }

    if (mock_recv_message == NULL)
    {
        // Simulate server disconnection
        errno = 0; // Clear errno for normal disconnection
        return 0;  // Simulate server disconnection
    }
    else
    {
        // Simulate receiving a message
        strncpy(buf, mock_recv_message, len);
        return strlen(mock_recv_message); // Return the number of bytes "received"
    }
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
void test_listen_for_updates_valid_message()
{
    mock_recv_message =
        "{\"type\":\"order_for_distribution\",\"timestamp\":\"2025-05-02T12:00:00Z\",\"order_id\":\"12345\","
        "\"items_shipped\":[{\"item_type\":1,\"quantity\":10}],\"status\":\"shipped\"}";
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_updates(&sock);
    TEST_ASSERT_EQUAL(0, test_status); // Expect success
}

// Test receiving invalid JSON messages
void test_listen_for_updates_invalid_json()
{
    mock_recv_message = "invalid_json";
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_updates(&sock);
    TEST_ASSERT_EQUAL(-2, test_status); // Expect invalid JSON error
}

// Test receiving no data
void test_listen_for_updates_no_data()
{
    mock_recv_message = NULL; // Simulate server disconnection
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_updates(&sock);
    TEST_ASSERT_EQUAL(-1, test_status); // Expect failure
}

// Test socket error
void test_listen_for_updates_socket_error()
{
    simulate_recv_failure = 1; // Simulate socket error
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_updates(&sock);
    TEST_ASSERT_EQUAL(-1, test_status); // Expect failure
}

void test_listen_for_updates_invalid_order_format()
{
    // Simulate receiving an invalid order format
    mock_recv_message =
        "{\"type\":\"order_for_distribution\",\"timestamp\":\"2025-05-02T12:00:00Z\",\"order_id\":\"12345\","
        "\"items_shipped\":[{\"item_type\":\"invalid_type\",\"quantity\":10}]}";
    int sock = MOCK_SOCKET_DESCRIPTOR;
    listen_for_updates(&sock);
    TEST_ASSERT_EQUAL(-3, test_status); // Expect success
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_listen_for_updates_valid_message);
    RUN_TEST(test_listen_for_updates_invalid_json);
    RUN_TEST(test_listen_for_updates_no_data);
    RUN_TEST(test_listen_for_updates_socket_error);
    RUN_TEST(test_listen_for_updates_invalid_order_format);

    return UNITY_END();
}

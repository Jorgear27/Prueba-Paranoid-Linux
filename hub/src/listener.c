#include "listener.h"

#ifdef TESTING
static int test_iteration_count = 0; // Counter for testing
int test_status = 0;                 // Global variable to track status during testing
#endif

void* listen_for_updates(void* arg)
{
    int sock = *(int*)arg;
    char buffer[SOCKET_BUFFER_SIZE];

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));

        // Receive messages from the server
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No data available, continue the loop
                sleep(1);
                continue;
            }
#ifdef TESTING
            test_status = -1; // Set status for failure
#endif
            perror("[ERROR] Failed to receive message from server");
            break;
            if (bytes_received == 0)
            {
                printf("[INFO] Server closed the connection.\n");
            }
            else
            {
                perror("[ERROR] Failed to receive message from server");
            }
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate the received message
        printf("\nServer response: %s\n", buffer);

        // Parse the JSON message
        cJSON* json = cJSON_Parse(buffer);
        if (json == NULL)
        {
#ifdef TESTING
            test_status = -2; // Set status for invalid JSON
#endif
            printf("[ERROR] Failed to parse JSON message.\n");
#ifdef TESTING
            if (++test_iteration_count >= 3)
            {
                test_iteration_count = 0; // Reset for next test
                break;
            }
#endif
            continue;
        }

        // Check if the message type is "order_update"
        cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "order_for_distribution") == 0)
        {
#ifdef TESTING
            test_status = 0; // Set status for success
#endif
            printf("[ORDER UPDATE]\n");

            // Extract fields from the JSON object
            cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
            cJSON* order_id = cJSON_GetObjectItemCaseSensitive(json, "order_id");
            cJSON* items_shipped = cJSON_GetObjectItemCaseSensitive(json, "items_shipped");
            cJSON* status = cJSON_GetObjectItemCaseSensitive(json, "status");

            if (cJSON_IsString(order_id) && cJSON_IsString(status) && cJSON_IsArray(items_shipped))
            {
                printf("[INFO] Order ID: %s\n", order_id->valuestring);
                printf("[INFO] Timestamp: %s\n", timestamp->valuestring);
                printf("[INFO] Status: %s\n", status->valuestring);

                // Iterate through the "items_shipped" array
                printf("[INFO] Items Needed:\n");
                cJSON* item;
                cJSON_ArrayForEach(item, items_shipped)
                {
                    cJSON* item_type = cJSON_GetObjectItemCaseSensitive(item, "item_type");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");

                    if (cJSON_IsNumber(item_type) && cJSON_IsNumber(quantity))
                    {
                        printf("  - Item Type: %d, Quantity: %d\n", item_type->valueint, quantity->valueint);
                    }
                    else
                    {
                        printf("[ERROR] Invalid item format in items_needed.\n");
                    }
                }
            }
            else
            {
                printf("[ERROR] Invalid order for distribution format.\n");
#ifdef TESTING
                test_status = -3; // Set status for invalid order for distribution format
#endif
            }
        }

        // Clean up the JSON object
        cJSON_Delete(json);
#ifdef TESTING
        // Exit the loop after a certain number of iterations during testing
        if (++test_iteration_count >= 3)
        {
            test_iteration_count = 0; // Reset for next test
            break;
        }
#endif
    }

    return NULL;
}

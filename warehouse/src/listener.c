#include "listener.h"

#ifdef TESTING
int test_status = 0;                 // Global variable to track test status
static int test_iteration_count = 0; // Counter for test iterations
#endif

void* listen_for_request(void* arg)
{
    ThreadArgs* args = (ThreadArgs*)arg;
    int sock = args->sock;
    const char* wh_id = args->wh_id;
    char buffer[SOCKET_BUFFER_SIZE];

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));

        // Receive messages from the server
        int bytes_received = receive_message_wh(sock, buffer, sizeof(buffer));
        if (bytes_received <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No data available, continue the loop
                sleep(1);
                continue;
            }

#ifdef TESTING
            test_status = -1; // Set test status to indicate failure
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

        // Parse the JSON message
        cJSON* json = cJSON_Parse(buffer);
        if (json == NULL)
        {
#ifdef TESTING
            test_status = -2; // Set test status to indicate failure
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

        // Check if the message type is "supply_request"
        cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "supply_request") == 0)
        {
#ifdef TESTING
            test_status = 0; // Set test status to indicate success
#endif

            printf("[SUPPLY REQUEST]\n");

            // Extract fields from the JSON object
            cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
            cJSON* order_id = cJSON_GetObjectItemCaseSensitive(json, "order_id");
            cJSON* items_needed = cJSON_GetObjectItemCaseSensitive(json, "items_needed");

            if (cJSON_IsArray(items_needed))
            {
                printf("[INFO] Timestamp: %s\n", timestamp->valuestring);

                // Iterate through the "items_needed" array
                printf("[INFO] Items Needed:\n");
                cJSON* item;
                cJSON_ArrayForEach(item, items_needed)
                {
                    cJSON* item_type = cJSON_GetObjectItemCaseSensitive(item, "item_type");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");

                    if (cJSON_IsNumber(item_type) && cJSON_IsNumber(quantity))
                    {
                        printf("  - Item Type: %d, Quantity: %d\n", item_type->valueint, quantity->valueint);

                        // Update variables
                        update_inventory(item_type->valueint, -(quantity->valueint));
                        if (check_restock_item(item_type->valueint))
                        {
                            // Send a restock to the server
                            send_restock_to_server(sock, wh_id, item_type->valueint);
                        }
                    }
                    else
                    {
                        printf("[ERROR] Invalid item format in items_needed.\n");
                    }
                }

                // Serialize items_needed to a JSON string
                char* items_needed_string = cJSON_PrintUnformatted(items_needed);
                if (items_needed_string == NULL)
                {
                    printf("[ERROR] Failed to serialize items_needed to JSON string.\n");
                }
                else
                {
                    // Send a response back to the server
                    if (send_orderdispatch_to_server(sock, order_id->valuestring, items_needed_string) < 0)
                    {
                        printf("[ERROR] Failed to send order dispatch to server.\n");
                    }
                    else
                    {
                        printf("[INFO] Order dispatch sent to server: %s\n", items_needed_string);
                    }

                    free(items_needed_string); // Free the serialized string after use
                }

                // Send the updated inventory to the server
                send_inventory_to_server(sock, wh_id);
            }
            else
            {
                printf("[ERROR] Invalid order update format.\n");
#ifdef TESTING
                test_status = -3; // Set test status to indicate failure
#endif
            }
        }
        else
        {
            printf("\nServer response: %s\n", buffer);
        }
        // Clean up the JSON object
        cJSON_Delete(json);
#ifdef TESTING
        // Exit the loop after 3 iterations for testing
        if (++test_iteration_count >= 3)
        {
            test_iteration_count = 0; // Reset for next test
            break;
        }
#endif
    }

    return NULL;
}

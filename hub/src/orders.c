#include "orders.h"

// Static variable to keep track of the number of orders created by the hub
static int order_counter = 0;

void handle_create_order(int sock, const char* hub_id)
{
    char order_id[MAX_ORDER_ID_LENGTH];
    char timestamp[MAX_TIMESTAMP_LENGTH];
    cJSON* items_array = cJSON_CreateArray();

    // Generate the order ID automatically
    snprintf(order_id, sizeof(order_id), "%s_%d", hub_id, ++order_counter);
    printf("Generated Order ID: %s\n", order_id);

    // Generate the current timestamp
    generate_timestamp_hub(timestamp, sizeof(timestamp));

    // Menu for selecting items
    printf("\n--- Add Items to Order ---\n");
    while (1)
    {
        int item_type, quantity;
        printf("Enter item type (0:Medicines, 1:Food, 2:Clothing, -1 to finish): ");
        scanf("%d", &item_type);

        if (item_type == -1)
        {
            break; // Exit the loop when the user is done adding items
        }

        if (item_type < 0 || item_type > 2)
        {
            printf("Invalid item type. Please try again.\n");
            continue;
        }

        printf("Enter quantity for item type %d: ", item_type);
        scanf("%d", &quantity);

        if (quantity <= 0)
        {
            printf("Quantity must be greater than 0. Please try again.\n");
            continue;
        }

        // Add the item to the JSON array
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "item_type", item_type);
        cJSON_AddNumberToObject(item, "quantity", quantity);
        cJSON_AddItemToArray(items_array, item);
    }

    // Convert the items array to a JSON string
    char* items_needed = cJSON_PrintUnformatted(items_array);

    // Create the order request
    char* order_request = create_order_request(order_id, hub_id, timestamp, items_needed);
    if (send_message_from_hub(sock, order_request) < 0)
    {
        perror("Failed to send order request");
    }
    else
    {
        printf("Order request sent: %s\n", order_request);
    }

    // Clean up
    free(order_request);
    free(items_needed);
    cJSON_Delete(items_array); // Free the JSON array
}

void handle_cancel_order(int sock, const char* hub_id)
{
    char order_id[MAX_ORDER_ID_LENGTH];
    printf("Enter order ID to cancel: ");
    scanf("%s", order_id);

    // Generate the current timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_hub(timestamp, sizeof(timestamp));

    // Create a cancel message
    cJSON* cancel = cJSON_CreateObject();
    cJSON_AddStringToObject(cancel, "type", "cancel_order");
    cJSON_AddStringToObject(cancel, "hub_id", hub_id);
    cJSON_AddStringToObject(cancel, "timestamp", timestamp);
    cJSON_AddStringToObject(cancel, "order_id", order_id);

    char* cancel_message = cJSON_PrintUnformatted(cancel);

    if (send_message_from_hub(sock, cancel_message) < 0)
    {
        perror("Failed to send cancel request");
    }
    else
    {
        printf("Cancel request sent: %s\n", cancel_message);
    }

    // Clean up
    free(cancel_message);
    cJSON_Delete(cancel);
}

void handle_query_order_status(int sock, const char* hub_id)
{
    char order_id[MAX_ORDER_ID_LENGTH];
    printf("Enter order ID to query: ");
    scanf("%s", order_id);

    // Generate the current timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_hub(timestamp, sizeof(timestamp));

    // Create a query message
    cJSON* query = cJSON_CreateObject();
    cJSON_AddStringToObject(query, "type", "order_status");
    cJSON_AddStringToObject(query, "hub_id", hub_id);
    cJSON_AddStringToObject(query, "timestamp", timestamp);
    cJSON_AddStringToObject(query, "order_id", order_id);

    char* query_message = cJSON_PrintUnformatted(query);

    if (send_message_from_hub(sock, query_message) < 0)
    {
        perror("Failed to send query");
    }
    else
    {
        printf("Query sent: %s\n", query_message);

        // Receive the response
        char buffer[SOCKET_BUFFER_SIZE];
        int bytes_received = receive_message_hub(sock, buffer, sizeof(buffer));
        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            printf("Response from server: %s\n", buffer);
        }
        else
        {
            perror("Failed to receive response");
        }
    }

    // Clean up
    free(query_message);
    cJSON_Delete(query);
}

char* create_order_request(const char* order_id, const char* hub_id, const char* timestamp, const char* items_needed)
{
    cJSON* json_obj = cJSON_CreateObject();

    cJSON_AddStringToObject(json_obj, "type", "order_request");
    cJSON_AddStringToObject(json_obj, "hub_id", hub_id);
    cJSON_AddStringToObject(json_obj, "timestamp", timestamp);
    cJSON_AddStringToObject(json_obj, "order_id", order_id);

    cJSON* items_array = cJSON_Parse(items_needed); // Parse items_needed JSON string
    cJSON_AddItemToObject(json_obj, "items_needed", items_array);

    char* result = cJSON_PrintUnformatted(json_obj);

    cJSON_Delete(json_obj); // Free JSON object
    return result;
}

void handle_delivery_update(int sock, const char* hub_id)
{
    char order_id[MAX_ORDER_ID_LENGTH];
    char status[32];

    printf("Enter order ID to update: ");
    scanf("%49s", order_id);

    printf("Enter new status (Delivered / Canceled): ");
    scanf("%31s", status);

    // Generate the current timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_hub(timestamp, sizeof(timestamp));

    // Build the delivery_update message
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "delivery_update");
    cJSON_AddStringToObject(msg, "hub_id", hub_id);
    cJSON_AddStringToObject(msg, "order_id", order_id);
    cJSON_AddStringToObject(msg, "status", status);
    cJSON_AddStringToObject(msg, "timestamp", timestamp);

    char* json_string = cJSON_PrintUnformatted(msg);

    if (send_message_from_hub(sock, json_string) < 0)
    {
        perror("Failed to send delivery update");
    }
    else
    {
        printf("Delivery update sent: %s\n", json_string);
    }

    free(json_string);
    cJSON_Delete(msg);
}

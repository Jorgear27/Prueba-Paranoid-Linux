#include "orders.h"
#include "inventory.h"

#include "inventory.h"
#include "orders.h"

int send_orderdispatch_to_server(int sock, const char* order_id, const char* items_needed)
{
    // Create the JSON object for the response
    cJSON* response = cJSON_CreateObject();

    // Add fields to the JSON object
    cJSON_AddStringToObject(response, "type", "order_dispatch");
    cJSON_AddStringToObject(response, "order_id", order_id);
    cJSON_AddStringToObject(response, "status", "Shipped");

    // Generate a timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_wh(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(response, "timestamp", timestamp);

    // Parse the items_needed string into a cJSON array
    cJSON* itemsArray = cJSON_Parse(items_needed);
    if (itemsArray == NULL || !cJSON_IsArray(itemsArray))
    {
        printf("[ERROR] Invalid items_needed format. Failed to parse JSON.\n");
        cJSON_Delete(response);
        return -1;
    }

    // Add the items_needed array to the JSON object
    cJSON_AddItemToObject(response, "items_shipped", itemsArray);

    // Serialize the JSON object to a string
    char* json_string = cJSON_PrintUnformatted(response);

    // Send the JSON string to the server
    if (send_message_from_wh(sock, json_string) < 0)
    {
        perror("[ERROR] Failed to send inventory to server");
        free(json_string);
        cJSON_Delete(response);
        return -1;
    }
    else
    {
        printf("[INFO] Inventory sent to server: %s\n", json_string);
    }

    // Clean up
    free(json_string);
    cJSON_Delete(response);

    return 0;
}

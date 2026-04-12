#include "authentication.h"

void generate_timestamp_wh(char* buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm* t = gmtime(&now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", t);
}

char* create_client_info_warehouse(const char* wh_id, int latitude, int longitude, bool is_secure,
                                   const Connection* connections, int conn_count)
{
    // Validate the format of wh_id
    if (wh_id == NULL || wh_id[0] != 'W')
    {
        printf("Invalid warehouse ID format\n");
        return NULL;
    }

    // Create a JSON object for client information
    cJSON* json_obj = cJSON_CreateObject();

    // Generate the current timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_wh(timestamp, sizeof(timestamp));

    cJSON_AddStringToObject(json_obj, "type", "client_info");
    cJSON_AddStringToObject(json_obj, "timestamp", timestamp);
    cJSON_AddStringToObject(json_obj, "warehouse_id", wh_id);
    cJSON_AddBoolToObject(json_obj, "is_secure", is_secure);

    cJSON* location = cJSON_CreateObject();
    cJSON_AddNumberToObject(location, "latitude", latitude);
    cJSON_AddNumberToObject(location, "longitude", longitude);
    cJSON_AddItemToObject(json_obj, "location", location);

    // Serialise the connections array into a cJSON array.
    cJSON* conn_array = cJSON_CreateArray();
    if (connections != NULL && conn_count > 0)
    {
        for (int i = 0; i < conn_count; i++)
        {
            cJSON* conn = cJSON_CreateObject();
            cJSON_AddStringToObject(conn, "target_node_id", connections[i].target_node_id);
            cJSON_AddNumberToObject(conn, "base_weight", connections[i].base_weight);
            cJSON_AddStringToObject(conn, "connection_type", connections[i].connection_type);

            // connection_conditions is stored as a JSON array string — parse it
            // back to a cJSON array so it embeds correctly in the message.
            cJSON* conds = cJSON_Parse(connections[i].connection_conditions);
            if (conds == NULL)
            {
                conds = cJSON_CreateArray(); // Fallback to empty array on parse error.
            }
            cJSON_AddItemToObject(conn, "connection_conditions", conds);

            cJSON_AddItemToArray(conn_array, conn);
        }
    }
    cJSON_AddItemToObject(json_obj, "connections", conn_array);

    char* result = cJSON_PrintUnformatted(json_obj);

    // Clean up the JSON object
    cJSON_Delete(json_obj);

    return result;
}

bool isValidWhId(const char* wh_id)
{
    // Verify that the warehouse ID is not NULL
    if (strlen(wh_id) < 2)
    {
        return false;
    }

    // Verify that the first character is 'W'
    if (wh_id[0] != 'W')
    {
        return false;
    }

    // Verify that the rest of the string contains only digits
    for (size_t i = 1; i < strlen(wh_id); ++i)
    {
        if (!isdigit(wh_id[i]))
        {
            return false;
        }
    }

    return true;
}

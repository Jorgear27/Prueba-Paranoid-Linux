#include "authentication.h"
#include "unity.h"
#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WH_ID "W009"
#define WH_FAIL "M001"
#define LATITUDE 37
#define LONGITUDE -122

static const Connection TEST_CONNECTION = {
    .target_node_id = "W101",
    .base_weight = 12.5,
    .connection_type = "road",
    .connection_conditions = "[\"foggy\",\"night\"]",
};

void setUp(void);
void tearDown(void);
void test_create_client_info(void);
void test_create_client_info_fail(void);
void test_validate_warehouse_id(void);
int main(void);

void setUp(void)
{
    // Setup code before each test
}

void tearDown(void)
{
    // Cleanup code after each test
}

// Test case for create_client_info_warehouse
void test_create_client_info()
{
    printf("Testing create_client_info_warehouse...\n");
    char* client_info = create_client_info_warehouse(WH_ID, LATITUDE, LONGITUDE, true, &TEST_CONNECTION, 1);

    // Parse the JSON result
    cJSON* json_obj = cJSON_Parse(client_info);
    TEST_ASSERT_NOT_NULL(json_obj);

    // Validate the JSON fields
    cJSON* type = cJSON_GetObjectItem(json_obj, "type");
    TEST_ASSERT_TRUE(cJSON_IsString(type));
    TEST_ASSERT_EQUAL_STRING("client_info", type->valuestring);

    cJSON* timestamp = cJSON_GetObjectItem(json_obj, "timestamp");
    TEST_ASSERT_TRUE(cJSON_IsString(timestamp));
    TEST_ASSERT_TRUE(strlen(timestamp->valuestring) > 0);

    cJSON* warehouse_id_json = cJSON_GetObjectItem(json_obj, "warehouse_id");
    TEST_ASSERT_TRUE(cJSON_IsString(warehouse_id_json));
    TEST_ASSERT_EQUAL_STRING(WH_ID, warehouse_id_json->valuestring);

    cJSON* location = cJSON_GetObjectItem(json_obj, "location");
    TEST_ASSERT_TRUE(cJSON_IsObject(location));

    cJSON* latitude_json = cJSON_GetObjectItem(location, "latitude");
    TEST_ASSERT_TRUE(cJSON_IsNumber(latitude_json));
    TEST_ASSERT_EQUAL_INT(LATITUDE, latitude_json->valueint);

    cJSON* longitude_json = cJSON_GetObjectItem(location, "longitude");
    TEST_ASSERT_TRUE(cJSON_IsNumber(longitude_json));
    TEST_ASSERT_EQUAL_INT(LONGITUDE, longitude_json->valueint);

    cJSON* secure = cJSON_GetObjectItem(json_obj, "is_secure");
    TEST_ASSERT_TRUE(cJSON_IsBool(secure));
    TEST_ASSERT_TRUE(cJSON_IsTrue(secure));

    cJSON* connections = cJSON_GetObjectItem(json_obj, "connections");
    TEST_ASSERT_TRUE(cJSON_IsArray(connections));
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(connections));

    cJSON* connection = cJSON_GetArrayItem(connections, 0);
    TEST_ASSERT_TRUE(cJSON_IsObject(connection));

    cJSON* target_node_id = cJSON_GetObjectItem(connection, "target_node_id");
    TEST_ASSERT_TRUE(cJSON_IsString(target_node_id));
    TEST_ASSERT_EQUAL_STRING(TEST_CONNECTION.target_node_id, target_node_id->valuestring);

    cJSON* base_weight = cJSON_GetObjectItem(connection, "base_weight");
    TEST_ASSERT_TRUE(cJSON_IsNumber(base_weight));
    TEST_ASSERT_TRUE(fabs(TEST_CONNECTION.base_weight - base_weight->valuedouble) < 0.0001);

    cJSON* connection_type = cJSON_GetObjectItem(connection, "connection_type");
    TEST_ASSERT_TRUE(cJSON_IsString(connection_type));
    TEST_ASSERT_EQUAL_STRING(TEST_CONNECTION.connection_type, connection_type->valuestring);

    cJSON* connection_conditions = cJSON_GetObjectItem(connection, "connection_conditions");
    TEST_ASSERT_TRUE(cJSON_IsArray(connection_conditions));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(connection_conditions));
    TEST_ASSERT_EQUAL_STRING("foggy", cJSON_GetArrayItem(connection_conditions, 0)->valuestring);
    TEST_ASSERT_EQUAL_STRING("night", cJSON_GetArrayItem(connection_conditions, 1)->valuestring);

    printf("Client Info: %s\n", client_info);

    cJSON_Delete(json_obj);
    free(client_info);
}

// Test case for create_client_info_warehouse with invalid warehouse ID
void test_create_client_info_fail()
{
    printf("Testing create_client_info_warehouse with invalid warehouse ID...\n");
    char* client_info = create_client_info_warehouse(WH_FAIL, LATITUDE, LONGITUDE, true, NULL, 0);

    // Parse the JSON result
    cJSON* json_obj = cJSON_Parse(client_info);
    TEST_ASSERT_NULL(json_obj); // Expecting NULL for invalid warehouse ID
    printf("Client info JSON for invalid ID: %s\n", client_info);

    // Clean up
    cJSON_Delete(json_obj);
    free(client_info);
}

// Test cases for validate_warehouse_id
void test_validate_warehouse_id()
{
    printf("Testing validate_warehouse_id...\n");
    TEST_ASSERT_TRUE(isValidWhId(WH_ID));
    TEST_ASSERT_FALSE(isValidWhId(WH_FAIL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_client_info);
    RUN_TEST(test_create_client_info_fail);
    RUN_TEST(test_validate_warehouse_id);
    return UNITY_END();
}

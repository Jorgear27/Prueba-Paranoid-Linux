#include "warehouse.h"

int connect_wh_to_server()
{
    int sock;
    struct sockaddr_in server_addr;

    // Get the server IP and port from environment variables
    const char* server_ip = getenv("SERVER_IP");
    const char* server_port_env = getenv("SERVER_PORT");

    if (server_ip == NULL)
    {
        printf("Environment variable SERVER_IP not set. Using default: %s\n", SERVER_IP_DEFAULT);
        server_ip = SERVER_IP_DEFAULT;
    }

    int port = SERVER_PORT_DEFAULT; // Default port value
    if (server_port_env == NULL)
    {
        printf("Environment variable SERVER_PORT not set. Using default: %d\n", SERVER_PORT_DEFAULT);
        port = SERVER_PORT_DEFAULT;
    }
    else
    {
        // Convert server_port to integer
        port = atoi(server_port_env);
        if (port <= 0 || port > 65535)
        {
            printf("Invalid port number. Using default: %d\n", SERVER_PORT_DEFAULT);
            port = SERVER_PORT_DEFAULT;
        }
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        return -1;
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        close(sock);
        return -1;
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to server failed");
        close(sock);
        return -1;
    }
    printf("Connected to server at %s:%d\n", server_ip, port);

    int flags = fcntl(sock, F_GETFL, 0);

    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    return sock;
}

void disconnect_wh_from_server(int sock, const char* warehouse_id)
{
    // Create a JSON object for disconnect request
    cJSON* json = cJSON_CreateObject();

    // Generate a timestamp
    char timestamp[MAX_TIMESTAMP_LENGTH];
    generate_timestamp_wh(timestamp, sizeof(timestamp));

    // Add disconnect request type to JSON object
    cJSON_AddStringToObject(json, "type", "disconnect_request");
    cJSON_AddStringToObject(json, "user_id", warehouse_id);
    cJSON_AddStringToObject(json, "timestamp", timestamp);

    // Convert JSON object to string
    char* disconnect_message = cJSON_PrintUnformatted(json);

    // Send disconnect message to server
    send_message_from_wh(sock, disconnect_message);

    // Free the JSON string and object
    free(disconnect_message);
    cJSON_Delete(json);

    // Close the socket
    close(sock);
    printf("Disconnected from server\n");
}

int send_message_from_wh(int sock, const char* message)
{
    if (sock < 0 || message == NULL)
    {
        errno = EBADF;
        return -1;
    }

    return send(sock, message, strlen(message), 0);
}

int receive_message_wh(int sock, char* buffer, size_t buffer_size)
{
    if (sock < 0 || buffer == NULL || buffer_size == 0)
    {
        errno = EBADF;
        return -1;
    }

    return recv(sock, buffer, buffer_size - 1, 0);
}

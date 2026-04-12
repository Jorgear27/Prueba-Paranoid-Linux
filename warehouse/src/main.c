#include "authentication.h"
#include "listener.h"
#include "warehouse.h"
#include <pthread.h>
#include <stdbool.h>

// Function prototypes
void print_usage(const char* program_name);
void print_invalid_id(const char* program_name);
void display_menu(void);

#define ARGC_REQUESTED 4

enum MenuOptions
{
    MANUAL_SYNC = 1,
    UPDATE_INVENTORY,
    FORCE_RESTOCK,
    VIEW_INVENTORY,
    DISCONNECT
};

void print_usage(const char* program_name)
{
    printf("Usage: %s <warehouse_id> <latitude> <longitude>\n", program_name);
}

void print_invalid_id(const char* program_name)
{
    printf("warehouse_id format invalid. Usage: Wxxx where xxx are digits.\n");
    printf("Example: %s W001 40.7128 -74.0060\n", program_name);
}

void display_menu()
{
    printf("\n--- Warehouse Menu ---\n");
    printf("1. Manually synchronize inventory\n");
    printf("2. Update Inventory\n");
    printf("3. Force restock\n");
    printf("4. View current inventory\n");
    printf("5. Disconnect\n");
    printf("Enter your choice: \n");
}

int main(int argc, char* argv[])
{
    // Check if the correct number of arguments is provided
    if (argc != ARGC_REQUESTED)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Parse command-line arguments
    const char* wh_id = argv[1];
    int latitude = atoi(argv[2]);
    int longitude = atoi(argv[3]);

    // Validate warehouse ID format
    if (!isValidWhId(wh_id))
    {
        print_invalid_id(argv[0]);
        return EXIT_FAILURE;
    }

    // Initialize inventory with 3 item types
    initialize_inventory(MAX_ITEM_TYPES);

    // Step 1: Connect to the server
    int sock = connect_wh_to_server();

    // Step 2: Authenticate warehouse
    // Declare transport connections to neighbouring nodes. These would be loaded from a
    // config file. Here we define one example connection so the server can build a graph.
    Connection connections[1];
    strncpy(connections[0].target_node_id, "H001", MAX_NODE_ID_LENGTH - 1);
    connections[0].target_node_id[MAX_NODE_ID_LENGTH - 1] = '\0';
    connections[0].base_weight = 10.0;
    strncpy(connections[0].connection_type, "road", MAX_CONN_TYPE_LENGTH - 1);
    connections[0].connection_type[MAX_CONN_TYPE_LENGTH - 1] = '\0';
    strncpy(connections[0].connection_conditions, "[]", sizeof(connections[0].connection_conditions) - 1);
    connections[0].connection_conditions[sizeof(connections[0].connection_conditions) - 1] = '\0';

    char* client_info = create_client_info_warehouse(wh_id, latitude, longitude, true, connections, 1);
    if (client_info == NULL)
    {
        perror("Invalid warehouse ID format\n");
        free(client_info);
        disconnect_wh_from_server(sock, wh_id);
        free_inventory();
        return EXIT_FAILURE;
    }

    // Send client information to server if the client_info is valid
    if (send_message_from_wh(sock, client_info) < 0)
    {
        perror("Failed to send client info");
        free(client_info);
        disconnect_wh_from_server(sock, wh_id);
        free_inventory();
        return EXIT_FAILURE;
    }
    free(client_info);

    // Send initial inventory to server
    usleep(100000); // Sleep for a short duration to allow server to process
    send_inventory_to_server(sock, wh_id);

    // Step 3: Start the listener thread
    pthread_t listener_thread;
    ThreadArgs thread_args = {sock, wh_id};
    if (pthread_create(&listener_thread, NULL, listen_for_request, &thread_args) != 0)
    {
        perror("Failed to create listener thread");
        disconnect_wh_from_server(sock, wh_id);
        return EXIT_FAILURE;
    }

    // Step 4: Start the periodic inventory update thread
    pthread_t update_thread;
    ThreadArgs thread_args_inventory = {sock, wh_id}; // Initialize the struct with socket and warehouse ID
    if (pthread_create(&update_thread, NULL, periodic_inventory_update, &thread_args_inventory) != 0)
    {
        perror("Failed to create periodic update thread");
        disconnect_wh_from_server(sock, wh_id);
        pthread_cancel(listener_thread);
        pthread_join(listener_thread, NULL);
        free_inventory();
        return EXIT_FAILURE;
    }

    // User interface loop
    int choice, item_type, quantity;
    while (true)
    {
        display_menu();
        scanf("%d", &choice);

        switch (choice)
        {
        case MANUAL_SYNC:
            send_inventory_to_server(sock, wh_id);
            break;
        case UPDATE_INVENTORY:
            printf("Enter item type and quantity to add (e.g., 1 10): ");
            scanf("%d %d", &item_type, &quantity);
            update_inventory(item_type, quantity);
            if (check_restock_item(item_type))
            {
                send_restock_to_server(sock, wh_id, item_type); // Send restock notice to server
            }
            send_inventory_to_server(sock, wh_id);
            break;
        case FORCE_RESTOCK:
            printf("Enter item type to restock: ");
            scanf("%d", &item_type);
            // Force restock the item
            set_inventory(item_type, MAX_ITEM_QUANTITY);
            send_restock_to_server(sock, wh_id, item_type); // Send restock notice to server
            send_inventory_to_server(sock, wh_id);          // Send updated inventory
            break;
        case VIEW_INVENTORY:
            print_inventory();
            break;
        case DISCONNECT:
            disconnect_wh_from_server(sock, wh_id);

            pthread_cancel(listener_thread);     // Cancel the listener thread
            pthread_join(listener_thread, NULL); // Wait for the listener thread to finish
            pthread_cancel(update_thread);       // Cancel the periodic update thread
            pthread_join(update_thread, NULL);   // Wait for the update thread to finish
            free_inventory();                    // Free inventory resources
            return EXIT_SUCCESS;
        default:
            printf("Invalid choice. Please try again.\n");
        }
        usleep(100000); // Sleep for a short duration to allow printing to complete
    }

    free_inventory(); // Free inventory resources
    return EXIT_SUCCESS;
}

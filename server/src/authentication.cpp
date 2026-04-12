#include "authentication.hpp"
#include "graph_router.hpp"

using json = nlohmann::json;

std::string Authentication::processClientInfo(const std::string& jsonData, int sock)
{
    try
    {
        // Parse the JSON data
        json clientInfo = json::parse(jsonData);

        // Validate the client information
        if (!validateClientInfo(clientInfo))
        {
            Logger::getInstance().log("Authentication", "Invalid client information");
            std::cerr << "[ERROR] Invalid client information.\n";
            return "{\"status\":\"error\",\"message\":\"Invalid client information\"}";
        }

        std::string id;
        // Extract parameters for client info
        if (clientInfo.contains("hub_id"))
        {
            id = clientInfo.at("hub_id").get<std::string>();
        }
        else if (clientInfo.contains("warehouse_id"))
        {
            id = clientInfo.at("warehouse_id").get<std::string>();
        }
        double latitude = clientInfo.at("location").at("latitude");
        double longitude = clientInfo.at("location").at("longitude");

        // Add the client connection to the registry
        sender.addConnection(id, sock);
        Logger::getInstance().log("Authentication",
                                  "[INFO] Added new client connection: " + id + " at socket " + std::to_string(sock));
        std::cout << "[INFO] Added new client connection: " << id << " at socket " << sock << "\n";

        // Retry mechanism
        int retryCount = 0;
        bool success = false;

        while (retryCount < MAX_RETRIES && !success)
        {
            // Use Database to insert or update user info
            success = database.insertOrUpdateUser(id, latitude, longitude);

            if (success)
            {
                Logger::getInstance().log("Authentication", "[INFO] User info stored in DB for ID = " + id);
                std::cout << "[INFO] User info stored in DB for ID = " << id << std::endl;
            }
            else
            {
                Logger::getInstance().log("Authentication", "[ERROR] Failed to process new client.");
                std::cerr << "[ERROR] Failed to process new client.\n";
                retryCount++;
                std::this_thread::sleep_for(RETRY_DELAY); // Wait before retrying
            }
        }

        if (!success)
        {
            return "{\"status\":\"error\",\"message\":\"Failed to process new client after retries\"}";
        }

        // ------------------------------------------------------------------
        // Store transport connections declared by the client.
        // Clients that omit "connections" are accepted without edges (isolated nodes).
        // ------------------------------------------------------------------
        if (clientInfo.contains("connections") && clientInfo.at("connections").is_array() &&
            !clientInfo.at("connections").empty())
        {
            if (!database.replaceConnections(id, clientInfo.at("connections")))
            {
                Logger::getInstance().log("Authentication", "[WARN] Failed to store connections for: " + id +
                                                                ". Node stored without edges.");
                std::cerr << "[WARN] Failed to store connections for: " << id << "\n";
            }
            else
            {
                Logger::getInstance().log("Authentication", "[INFO] Connections stored for: " + id + " (" +
                                                                std::to_string(clientInfo.at("connections").size()) +
                                                                " edges).");
            }
        }

        // ------------------------------------------------------------------
        // Trigger a live graph refresh so algorithms reflect the newly connected node
        // ------------------------------------------------------------------
        GraphRouter::getInstance().refreshFromDatabase(database);

        Logger::getInstance().log("Authentication", "[INFO] Client info processed successfully");
        std::cout << "[INFO] Client info processed successfully\n";

        // Respond to the client
        return "{\"status\":\"success\",\"message\":\"Client information received and stored\"}";
    }
    catch (const std::exception& e)
    {
        Logger::getInstance().log("Authentication",
                                  "[ERROR] Exception occurred while processing client info: " + std::string(e.what()));
        return "{\"status\":\"error\",\"message\":\"Invalid JSON format or missing fields:" + std::string(e.what()) +
               "\"}";
    }
}

void Authentication::handleClientDisconnection(const std::string& jsonData, int sock)
{
    // Extract the hub ID from the JSON data
    json message = json::parse(jsonData);
    std::string user_id = message.at("user_id").get<std::string>();
    std::string timestamp = message.at("timestamp").get<std::string>();
    // Log the disconnection
    Logger::getInstance().log("Authentication", "[INFO] Disconnect request received from: " + user_id);
    std::cout << "[INFO] Disconnect request received from: " << user_id << "\n";

    // Use Database to update user online status
    if (!database.updateUserOnlineStatus(user_id, false))
    {
        Logger::getInstance().log("Authentication", "[ERROR] Failed to update user status in DB.");
        std::cerr << "[ERROR] Failed to update user status in DB.\n";
    }
    else
    {
        Logger::getInstance().log("Authentication", "[INFO] User status updated to offline in DB for ID = " + user_id);
        std::cout << "[INFO] User status updated to offline in DB for ID = " << user_id << "\n";
    }

    // Remove the client connection from the registry
    sender.removeConnection(user_id);
    Logger::getInstance().log("Authentication", "[INFO] Removed client connection: " + user_id);
    std::cout << "[INFO] Removed client connection: " << user_id << "\n";

    // Refresh the graph so offline node and its edges are excluded immediately.
    GraphRouter::getInstance().refreshFromDatabase(database);
}

bool Authentication::validateClientInfo(const json& message)
{
    // Check if "timestamp" is present and not null
    if (!message.contains("timestamp") || message.at("timestamp").is_null())
    {
        std::cerr << "[ERROR] Missing or null timestamp.\n";
        return false;
    }

    // Check if "location" is present and valid
    if (!message.contains("location") || message.at("location").is_null())
    {
        std::cerr << "[ERROR] Missing or null location.\n";
        return false;
    }

    auto location = message.at("location");
    if (!location.contains("latitude") || !location.contains("longitude") || location.at("latitude").is_null() ||
        location.at("longitude").is_null())
    {
        std::cerr << "[ERROR] Missing or null latitude or longitude in location.\n";
        return false;
    }

    double latitude = location.at("latitude");
    double longitude = location.at("longitude");
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)
    {
        std::cerr << "[ERROR] Latitude or longitude out of range: latitude=" << latitude << ", longitude=" << longitude
                  << "\n";
        return false;
    }

    // Check if the message contains either "warehouse_id" or "hub_id"
    if (message.contains("warehouse_id") && !message.at("warehouse_id").is_null())
    {
        // Validate "warehouse_id"
        if (message.at("warehouse_id").empty())
        {
            std::cerr << "[ERROR] Warehouse ID is empty.\n";
            return false;
        }
    }
    else if (message.contains("hub_id") && !message.at("hub_id").is_null())
    {
        // Validate "hub_id"
        if (message.at("hub_id").empty())
        {
            std::cerr << "[ERROR] Hub ID is empty.\n";
            return false;
        }
    }
    else
    {
        std::cerr << "[ERROR] Missing warehouse_id or hub_id.\n";
        return false;
    }
    return true; // All checks passed
}

#include "request_router.hpp"

using json = nlohmann::json;

std::string RequestRouter::routeRequest(const std::string& jsonStr, int sock)
{
    try
    {
        // Parse the JSON to determine the type of request
        json req = json::parse(jsonStr);
        std::string typeStr = req["type"];
        RequestType type = requestTypeMap.count(typeStr) ? requestTypeMap.at(typeStr) : RequestType::Unknown;

        switch (type)
        {
        case RequestType::ClientInfo: {
            // Delegate to Authentication
            return auth.processClientInfo(jsonStr, sock);
        }
        case RequestType::InventoryUpdate: {
            // Delegate to InventoryManager for inventory updates
            inventoryManager.handleInventoryUpdate(jsonStr);
            return json{{"status", "success"}, {"message", "Inventory updated"}}.dump();
        }
        case RequestType::RestockNotice: {
            // Delegate to InventoryManager for restock notices
            inventoryManager.handleRestockNotice(jsonStr);
            return json{{"status", "success"}, {"message", "Restock notice processed"}}.dump();
        }
        case RequestType::OrderRequest: {
            // Delegate to OrderManager
            orderManager.handleNewOrder(jsonStr);
            return json{{"status", "success"}, {"message", "New order created"}}.dump();
        }
        case RequestType::OrderDispatch: {
            // Delegate to OrderManager for order dispatch
            orderManager.handleOrderDispatch(jsonStr);
            return json{{"status", "success"}, {"message", "Order dispatched"}}.dump();
        }
        case RequestType::DeliveryUpdate: {
            // Delegate to OrderManager for delivery updates
            orderManager.deliveryUpdate(jsonStr);
            return json{{"status", "success"}, {"message", "Delivery updated"}}.dump();
        }
        case RequestType::DisconnectRequest: {
            // Handle client disconnection
            auth.handleClientDisconnection(jsonStr, sock);
            // Send back the server a json so it kills the current process
            return json{{"order", "disconnect"}, {"status", "success"}, {"message", "Disconnect request processed"}}
                .dump();
        }
        case RequestType::OrderStatusQuery: {
            // Handle order status query
            return orderManager.handleOrderStatusQuery(jsonStr);
        }
        case RequestType::CancelOrder: {
            // Handle order cancellation
            return orderManager.handleCancelation(jsonStr);
        }
        default:
            return json{{"status", "unknown request"}}.dump();
        }
    }
    catch (const std::exception& e)
    {
        // Handle JSON parsing or missing field errors
        return json{{"status", "error"}, {"message", e.what()}}.dump();
    }
}

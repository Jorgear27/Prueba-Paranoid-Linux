#include "orders.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

std::string OrderManager::handleNewOrder(const std::string& jsonData)
{
    try
    {
        // Parse the JSON data
        json message = json::parse(jsonData);

        // Validate the order details
        if (!validateOrderDetails(message))
        {
            std::cerr << "[ERROR] Invalid order details.\n";
            return json{{"status", "error"}, {"message", "Invalid order details"}}.dump();
        }

        // Extract fields
        std::string hubId = message.at("hub_id");
        std::string orderId = message.at("order_id");
        auto orderItemsList = message.at("items_needed");

        // Add the new order to the database
        for (const auto& item : orderItemsList)
        {
            int itemType = item.at("item_type");
            int quantityRequest = item.at("quantity");

            if (!database.insertOrUpdateOrder(orderId, hubId, itemType, quantityRequest))
            {
                logger.log("OrderManager", "[ERROR] Failed to insert/update order in DB.");
                std::cerr << "[ERROR] Failed to insert/update order.\n";
                return json{{"status", "error"}, {"message", "Failed to insert order in database"}}.dump();
            }
        }

        // Compute delivery stops (warehouses) for each item
        json stopsArray = json::array();
        std::unordered_map<std::string, bool> uniqueStops; // Deduplicate stops

        for (const auto& item : orderItemsList)
        {
            int itemType = item.at("item_type");
            int quantityNeeded = item.at("quantity");

            // Find warehouse for this item
            std::string warehouseId = inventoryManager.findWarehouseForItem(itemType, quantityNeeded);
            if (!warehouseId.empty() && uniqueStops.find(warehouseId) == uniqueStops.end())
            {
                stopsArray.push_back(warehouseId);
                uniqueStops[warehouseId] = true;
                logger.log("OrderManager", "[ROUTE] Added warehouse stop: " + warehouseId +
                                               " for item_type: " + std::to_string(itemType));
            }
            else if (warehouseId.empty())
            {
                logger.log("OrderManager", "[WARN] No warehouse found for item_type: " + std::to_string(itemType));
            }
        }

        // Launch a separate thread to handle the status update
        std::thread([this, orderId]() {
            try
            {
                // Sleep for 30 seconds (cancellation window)
                std::this_thread::sleep_for(std::chrono::seconds(30));
                std::string status = database.getOrderStatus(orderId);

                if (status == "Pending")
                {
                    updateOrderStatus(orderId, "Approved");
                    std::cout << "[INFO] Order " << orderId << " status updated to 'Approved'.\n";
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "[ERROR] Exception in thread: " << e.what() << "\n";
            }
        }).detach(); // Detach the thread to allow it to run independently

        logger.log("OrderManager", "[INFO] New order added for user: " + hubId + " with " +
                                       std::to_string(stopsArray.size()) + " delivery stops");

        // Return success response with the computed stops for courier routing
        json response = json::object();
        response["status"] = "success";
        response["order_id"] = orderId;
        response["stops"] = stopsArray;

        return response.dump();
    }
    catch (const std::exception& e)
    {
        logger.log("OrderManager", "[ERROR] Exception occurred while handling new order: " + std::string(e.what()));
        std::cerr << "[ERROR] Failed to handle new order: " << e.what() << "\n";
        return json{{"status", "error"}, {"message", e.what()}}.dump();
    }
}

void OrderManager::processApprovedOrders()
{
    while (true)
    {
        try
        {
            // Query the database for orders with status "Approved"
            std::vector<std::string> approvedOrders = database.getApprovedOrders();

            for (const auto& orderId : approvedOrders)
            {
                // Fetch order details
                nlohmann::json orderDetails = getOrderDetails(orderId);

                // Extract items needed
                std::vector<std::pair<int, int>> itemsNeeded;
                for (const auto& item : orderDetails["items_needed"])
                {
                    int itemType = item["item_type"];
                    int quantity = item["quantity"];
                    itemsNeeded.emplace_back(itemType, quantity);
                }

                // Call supplyRequest to fulfill the order
                std::string supplyResponse = supplyRequest(orderId, itemsNeeded);

                logger.log("OrderManager", "[INFO] Order " + orderId + " processed and supply request sent.");
            }
        }
        catch (const std::exception& e)
        {
            logger.log("OrderManager", "[ERROR] Exception in processApprovedOrders: " + std::string(e.what()));
        }

        // Wait for 10 seconds before checking for new approved orders
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

std::string OrderManager::supplyRequest(const std::string orderId, const std::vector<std::pair<int, int>>& itemsNeeded)
{
    try
    {
        // Create a base supply request JSON that will also be returned as a summary.
        nlohmann::json supplyRequest;
        supplyRequest["type"] = "supply_request";

        // Generate a real UTC timestamp.
        std::string timestamp;
        {
            const std::time_t now = std::time(nullptr);
            std::tm utc{};
            gmtime_r(&now, &utc);
            std::ostringstream oss;
            oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            timestamp = oss.str();
            supplyRequest["timestamp"] = timestamp;
        }

        supplyRequest["order_id"] = orderId;
        supplyRequest["items_needed"] = nlohmann::json::array();

        for (const auto& item : itemsNeeded)
        {
            int itemType = item.first;
            int quantityNeeded = item.second;

            // Ask InventoryManager for a warehouse that can fulfill the request
            std::string warehouseId = inventoryManager.findWarehouseForItem(itemType, quantityNeeded);
            if (!warehouseId.empty())
            {
                nlohmann::json itemPayload = {
                    {"item_type", itemType},
                    {"quantity", quantityNeeded},
                    {"fulfilled_by", warehouseId},
                };

                nlohmann::json perWarehouseRequest;
                perWarehouseRequest["type"] = "supply_request";
                perWarehouseRequest["timestamp"] = timestamp;
                perWarehouseRequest["order_id"] = orderId;
                perWarehouseRequest["items_needed"] = nlohmann::json::array({itemPayload});

                if (sender.sendMessageToClient(warehouseId, perWarehouseRequest.dump()) != -1)
                {
                    printf("[INFO] Supply request sent to warehouse: %s\n", warehouseId.c_str());
                    // Log the operation
                    logger.log("OrderManager", "[ORDER] Fulfilled request for item_type: " + std::to_string(itemType) +
                                                   ", quantity: " + std::to_string(quantityNeeded) +
                                                   " from warehouse: " + warehouseId);
                    std::cout << "[Order] Fulfilled request for item_type: " << itemType
                              << ", quantity: " << quantityNeeded << " from warehouse: " << warehouseId << "\n";
                }
                else
                {
                    logger.log("OrderManager", "[ERROR] Failed to send supply request to warehouse: " + warehouseId);
                }

                supplyRequest["items_needed"].push_back(itemPayload);
            }
            else
            {
                logger.log("OrderManager", "[ERROR] No warehouse found for item_type: " + std::to_string(itemType) +
                                               ", quantity: " + std::to_string(quantityNeeded));
                // Add the unfulfilled item to the supply request JSON
                supplyRequest["items_needed"].push_back(
                    {{"item_type", itemType}, {"quantity", quantityNeeded}, {"fulfilled_by", "none"}});
            }
        }
        return supplyRequest.dump();
    }
    catch (const std::exception& e)
    {
        logger.log("OrderManager", " [ERROR] Exception in supplyRequest: " + std::string(e.what()));
        return "{\"status\":\"error\",\"message\":\"Failed to process supply request\"}";
    }
}

void OrderManager::handleOrderDispatch(const std::string& jsonData)
{
    try
    {
        // Parse the JSON data
        json message = json::parse(jsonData);

        // Extract fields
        std::string orderId = message.at("order_id");
        std::string status = message.at("status"); // shipped or canceled
        auto itemsShipped = message.at("items_shipped");

        nlohmann::json existingOrder = getOrderDetails(orderId);
        std::string currentStatus = existingOrder.value("status", "");

        // Prevent illegal transitions such as Canceled -> Shipped.
        if (currentStatus == "Canceled" || currentStatus == "Delivered")
        {
            logger.log("OrderManager",
                       "[WARN] Ignoring dispatch for order " + orderId + " with terminal status: " + currentStatus);
            return;
        }

        if (currentStatus == "Shipped" && status == "Shipped")
        {
            // Idempotent duplicate dispatch from warehouse; nothing to do.
            return;
        }

        // JSON object to send to the hub for notification
        nlohmann::json orderDistribution;
        orderDistribution["type"] = "order_for_distribution";
        orderDistribution["order_id"] = orderId;
        orderDistribution["status"] = status;
        orderDistribution["items_shipped"] = itemsShipped;

        // Generate a real UTC timestamp (same fix applied to supplyRequest in TP2).
        {
            const std::time_t now = std::time(nullptr);
            std::tm utc{};
            gmtime_r(&now, &utc);
            std::ostringstream oss;
            oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            orderDistribution["timestamp"] = oss.str();
        }

        nlohmann::json orderDetails = existingOrder;
        std::string hubId = orderDetails.at("user_id");

        // Notify the hub about the order dispatch Sender sender;
        if (sender.sendMessageToClient(hubId, orderDistribution.dump()) != -1)
        {
            // Log the operation
            logger.log("OrderManager", "[ORDER] Order dispatched: " + orderId + ", new status: " + status);
            std::cout << "[ORDER] Order dispatched: " << orderId << ", new status: " << status << "\n";
        }
        else
        {
            // Log the error
            logger.log("OrderManager", "[ERROR] Failed to send order dispatch notification to hub: " + hubId);
            std::cerr << "[ERROR] Failed to send order dispatch notification to hub.\n";
            return; // Exit early if sending fails
        }

        // Update the order status in the database
        updateOrderStatus(orderId, status);

        return;
    }
    catch (const std::exception& e)
    {
        logger.log("OrderManager", "[ERROR] Exception in handleOrderDispatch: " + std::string(e.what()));
        std::cerr << "[ORDER] Failed to process order dispatch: " << e.what() << "\n";
        return;
    }
}

std::string OrderManager::handleCancelation(const std::string& jsonData)
{
    try
    {
        // Parse the JSON data
        json message = json::parse(jsonData);

        // Extract fields
        std::string orderId = message.at("order_id");

        std::string details = getOrderDetails(orderId).dump();

        json response_details = json::parse(details);
        std::string status = response_details.at("status");

        if (status == "Pending")
        {
            std::string status = "Canceled";
            updateOrderStatus(orderId, status);
        }
        else
        {
            std::cout << "[ERROR] The order was already approved, it cannot be canceled. \n";
            logger.log("OrderManager", "[ERROR] The order was already approved, it cannot be canceled.");
            return "{\"status\":\"error\",\"message\":\"The order was already approved, it cannot be canceled\"}";
        }

        // Log the operation
        logger.log("OrderManager", "[ORDER] Order canceled: " + orderId);
        std::cout << "[ORDER] Order canceled: " << orderId << "\n";
        return "{\"status\":\"success\",\"message\":\"The order was canceled\"}";
    }
    catch (const std::exception& e)
    {
        logger.log("OrderManager", "[ERROR] Exception in handleCancelation: " + std::string(e.what()));
        std::cerr << "[ORDER] Failed to process order cancelation: " << e.what() << "\n";
        return "{\"status\":\"error\",\"message\":\"Failed to process order cancelation\"}";
    }
}

std::string OrderManager::handleOrderStatusQuery(const std::string& jsonData)
{
    try
    {
        // Parse the JSON data
        json message = json::parse(jsonData);

        // Validate the JSON structure
        if (!message.contains("type") || !message.at("type").is_string())
        {
            throw std::runtime_error("[ERROR] 'type' field must be a string.");
        }
        if (!message.contains("hub_id") || !message.at("hub_id").is_string())
        {
            throw std::runtime_error("[ERROR] 'hub_id' field must be a string.");
        }
        if (!message.contains("timestamp") || !message.at("timestamp").is_string())
        {
            throw std::runtime_error("[ERROR] 'timestamp' field must be a string.");
        }
        if (!message.contains("order_id") || !message.at("order_id").is_string())
        {
            throw std::runtime_error("[ERROR] 'order_id' field must be a string.");
        }

        // Extract fields
        std::string orderId = message.at("order_id");

        // Fetch order details
        json orderDetails = getOrderDetails(orderId);

        // Return the order details
        return orderDetails.dump();
    }
    catch (const std::exception& e)
    {
        logger.log("OrderManager", "[ERROR] Exception in handleOrderStatusQuery: " + std::string(e.what()));
        std::cerr << "[ERROR] Failed to process order status query: " << e.what() << "\n";
        return json{{"status", "error"}, {"message", e.what()}}.dump();
    }
}

void OrderManager::deliveryUpdate(const std::string& jsonData)
{
    try
    {
        // Parse the JSON data
        json message = json::parse(jsonData);

        // Validate the order details
        if (!validateOrderUpdate(message))
        {
            std::cerr << "[ERROR] Invalid order details.\n";
            return; // Exit early if validation fails
        }

        // Extract fields
        std::string orderId = message.at("order_id");
        std::string status = message.at("status");

        // Update the order status in the database
        updateOrderStatus(orderId, status);

        // Log the operation
        logger.log("OrderManager",
                   "[ORDER] Delivery update received for order: " + orderId + ", new status: " + status);
        return;
    }
    catch (const std::exception& e)
    {
        logger.log("OrderManager", "[ERROR] Exception in deliveryUpdate: " + std::string(e.what()));
        return;
    }
}

nlohmann::json OrderManager::getOrderDetails(const std::string& orderId)
{
    return database.getOrderDetails(orderId);
}

void OrderManager::updateOrderStatus(const std::string& orderId, const std::string& newStatus)
{
    if (!database.updateOrderStatus(orderId, newStatus))
    {
        logger.log("OrderManager", "Failed to update order status for order: " + orderId);
        std::cerr << "[ERROR] Failed to update order status for order: " << orderId << "\n";
    }
}

bool OrderManager::validateOrderDetails(const json& message)
{
    // Check if "hub_id" is present and not null
    if (!message.contains("hub_id") || message.at("hub_id").is_null())
    {
        std::cerr << "[ERROR] Missing or null hub_id.\n";
        return false;
    }

    // Check if "order_id" is present and not null
    if (!message.contains("order_id") || message.at("order_id").is_null())
    {
        std::cerr << "[ERROR] Missing or null order_id.\n";
        return false;
    }

    // Check if "items_needed" is present and is an array
    if (!message.contains("items_needed") || message.at("items_needed").is_null() ||
        !message.at("items_needed").is_array())
    {
        std::cerr << "[ERROR] Missing or invalid items_needed array.\n";
        return false;
    }

    // Validate items array (each item should have "item_type" and "quantity")
    for (const auto& item : message.at("items_needed"))
    {
        if (!item.contains("item_type") || !item.contains("quantity") || item.at("item_type").is_null() ||
            item.at("quantity").is_null())
        {
            std::cerr << "[ERROR] Missing or null item_type or quantity in item.\n";
            return false;
        }
    }

    return true; // All checks passed, data is valid
}

bool OrderManager::validateOrderUpdate(const json& message)
{
    // Check if "timestamp" is present and not null
    if (!message.contains("timestamp") || message.at("timestamp").is_null())
    {
        std::cerr << "[ERROR] Missing or null timestamp.\n";
        return false;
    }

    // Check if "order_id" is present and not null
    if (!message.contains("hub_id") || message.at("hub_id").is_null())
    {
        std::cerr << "[ERROR] Missing or null hub_id.\n";
        return false;
    }

    // Check if "order_id" is present and not null
    if (!message.contains("order_id") || message.at("order_id").is_null())
    {
        std::cerr << "[ERROR] Missing or null order_id.\n";
        return false;
    }

    // Check if "status" is present and not null
    if (!message.contains("status") || message.at("status").is_null())
    {
        std::cerr << "[ERROR] Missing or null status.\n";
        return false;
    }

    return true; // All checks passed, data is valid
}

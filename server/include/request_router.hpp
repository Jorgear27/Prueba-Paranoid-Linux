/**
 * @file request_router.hpp
 * @brief This file contains the RequestRouter class which handles routing of incoming requests
 *        to the appropriate handlers based on the request type.
 * @version 0.1
 * @date 2025-04-06
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef REQUEST_ROUTER_H
#define REQUEST_ROUTER_H

#include "authentication.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "orders.hpp"
#include "sender.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

/**
 * @brief This class handles routing of incoming requests to the appropriate handlers
 *
 */
class RequestRouter
{
  public:
    /**
     * @brief Construct a new Request Router object with dependency injection
     *
     */
    RequestRouter(Authentication& auth, InventoryManager& inventoryManager, OrderManager& orderManager)
        : auth(auth), inventoryManager(inventoryManager), orderManager(orderManager)
    {
    }

    /**
     * @brief Route the incoming request based on the request type.
     *
     * @param jsonData JSON string containing the request data
     * @param sock Socket file descriptor for the client
     * @return std::string Response in JSON format
     */
    virtual std::string routeRequest(const std::string& jsonData, int sock);

  private:
    /**
     * @brief Enum to represent different request types
     *
     */
    enum class RequestType
    {
        ClientInfo,        // H/W -> server
        InventoryUpdate,   // Wh -> server
        RestockNotice,     // Wh -> server
        OrderRequest,      // Hub -> server
        OrderDispatch,     // Wh -> server
        DeliveryUpdate,    // unimplemented
        OrderStatusQuery,  // Hub -> server
        CancelOrder,       // Hub -> server
        DisconnectRequest, // H/W -> server
        Unknown
    };

    /**
     * @brief Map to associate request types with their string representations
     *
     */
    const std::unordered_map<std::string, RequestType> requestTypeMap = {
        {"client_info", RequestType::ClientInfo},
        {"inventory_update", RequestType::InventoryUpdate},
        {"restock_notice", RequestType::RestockNotice},
        {"order_request", RequestType::OrderRequest},
        {"order_dispatch", RequestType::OrderDispatch},
        {"delivery_update", RequestType::DeliveryUpdate},
        {"disconnect_request", RequestType::DisconnectRequest},
        {"order_status", RequestType::OrderStatusQuery},
        {"cancel_order", RequestType::CancelOrder}};

    Authentication& auth;               //!< Reference to the authentication instance
    InventoryManager& inventoryManager; //!< Reference to the inventory manager instance
    OrderManager& orderManager;         //!< Reference to the order manager instance
};

#endif

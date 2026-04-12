/**
 * @file orders.hpp
 * @brief This file contains the OrderManager class which handles creating, canceling,
 *        and managing orders.
 * @version 0.2
 * @date 2025-04-06
 *
 */

#ifndef ORDERS_H
#define ORDERS_H

#include "database.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "sender.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

/**
 * @brief Namespace for JSON library.
 *
 */
using json = nlohmann::json;

/**
 * @brief This class handles creating, canceling, and managing orders.
 *
 */
class OrderManager
{
  public:
    /**
     * @brief Constructor with dependency injection
     *
     */
    OrderManager(Database& db = Database::getInstance(), Logger& logger = Logger::getInstance(),
                 Sender& sender = Sender::getInstance(),
                 InventoryManager& inventoryManager = InventoryManager::getInstance())
        : database(db), logger(logger), sender(sender), inventoryManager(inventoryManager)
    {
    }

    /**
     * @brief Handle a new order request.
     *
     * @param jsonData JSON string containing the order request.
     */
    virtual void handleNewOrder(const std::string& jsonData);

    /**
     * @brief Process approved orders and create supply requests.
     *
     */
    virtual void processApprovedOrders();

    /**
     * @brief Send the order to the warehouse (supply request) and reduce stock from warehouses.
     *
     * @param orderId The ID of the order.
     * @param itemsNeeded A vector of item types and quantities needed.
     * @return std::string JSON string representing the supply request.
     */
    std::string supplyRequest(const std::string orderId, const std::vector<std::pair<int, int>>& itemsNeeded);

    /**
     *  @brief Handle order dispatch from the warehouse.
     *
     * @param jsonData JSON string containing the order dispatch details.
     */
    virtual void handleOrderDispatch(const std::string& jsonData);

    /**
     * @brief Handle order cancellation.
     *
     * @param jsonData JSON string containing the order cancellation details.
     *
     * @return order cancellation status.
     */
    virtual std::string handleCancelation(const std::string& jsonData);

    /**
     * @brief Handle order status query.
     *
     * @param jsonData JSON string containing the order status query details.
     * @return std::string JSON string with the order status.
     */
    virtual std::string handleOrderStatusQuery(const std::string& jsonData);

    /**
     * @brief Handle delivery updates from a hub.
     *
     * @param jsonData JSON string containing the delivery update details.
     */
    virtual void deliveryUpdate(const std::string& jsonData);

    /**
     * @brief Retrieve details of an order.
     *
     * @param orderId string with the order id.
     * @return nlohmann::json JSON object with order details.
     */
    nlohmann::json getOrderDetails(const std::string& orderId);

    /**
     * @brief Update the status of an order.
     *
     * @param orderId string with the order id.
     * @param newStatus string with the new status.
     *
     */
    void updateOrderStatus(const std::string& orderId, const std::string& newStatus);

    /**
     * @brief Validate the order details received in JSON format for a new order.
     *
     * @param message JSON object containing order details.
     */
    bool validateOrderDetails(const json& message);

    /**
     * @brief Validate the order details received in JSON format for an update order.
     *
     * @param message JSON object containing order details.
     */
    bool validateOrderUpdate(const json& message);

  private:
    Database& database;                 //!< Reference to the database instance
    Logger& logger;                     //!< Reference to the logger instance
    Sender& sender;                     //!< Reference to the sender instance
    InventoryManager& inventoryManager; //!< Reference to the inventory manager instance
};

#endif

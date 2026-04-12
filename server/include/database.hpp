/**
 * @file database.hpp
 * @brief Header file for managing PostgreSQL database connections.
 *
 */

#ifndef database_H
#define database_H

#include "log.hpp"
#include <chrono>
#include <iostream>
#include <libpq-fe.h>
#include <nlohmann/json.hpp>
#include <thread>

/**
 * @brief Connection string for PostgreSQL database.
 *
 */
#define DB_INFO "host=localhost dbname=paranoid_db user=server password=server123 keepalives=1 keepalives_idle=10"

/**
 * @class Database
 * @brief A class to manage PostgreSQL database connections.
 * encapsulates the functionality required to connect to a PostgreSQL database using the `libpq` library.
 */
class Database
{
  public:
    /**
     * @brief Construct a new Database object
     *
     * @param connectionString
     */
    Database(const std::string& connectionString = DB_INFO);

    /**
     * @brief Destroys the Database object and disconnects from the database.
     *
     */
    virtual ~Database();

    /**
     * @brief Checks if the database connection is active.
     * @return `true` if the connection is active, `false` otherwise.
     */
    bool is_connected();

    /**
     * @brief Disconnects from the PostgreSQL database.
     */
    void disconnect();

    /**
     * @brief Get the singleton instance of the Database class.
     *
     * This method provides access to a single instance of the Database class for use across all layers.
     *
     * @param connectionString The connection string for the PostgreSQL database.
     *
     * @return Database& Reference to the singleton Database instance.
     */
    static Database& getInstance(const std::string& connectionString = DB_INFO);

    /**
     * @brief Get the PostgreSQL connection object.
     *
     * This method provides access to the underlying `PGconn*` object used for
     * interacting with the PostgreSQL database.
     *
     * @param connectionString The connection string for the PostgreSQL database.
     *
     * @return PGconn* Pointer to the PostgreSQL connection object.
     */
    PGconn* getConnection(const std::string& connectionString = DB_INFO);

    /**
     * @brief Insert or update an order in the database.
     *
     * @param orderId The ID of the order.
     * @param hubId The ID of the hub.
     * @param itemType The type of the item.
     * @param quantity The quantity of the item.
     * @return true if the operation was successful, false otherwise.
     */
    virtual bool insertOrUpdateOrder(const std::string& orderId, const std::string& hubId, int itemType, int quantity);

    /**
     * @brief Get the status of an order.
     *
     * @param orderId The ID of the order.
     * @return std::string The status of the order.
     */
    virtual std::string getOrderStatus(const std::string& orderId);

    /**
     * @brief Update the status of an order.
     *
     * @param orderId The ID of the order.
     * @param newStatus The new status of the order.
     * @return true if the operation was successful, false otherwise.
     */
    virtual bool updateOrderStatus(const std::string& orderId, const std::string& newStatus);

    /**
     * @brief Retrieve details of an order.
     *
     * @param orderId The ID of the order.
     * @return nlohmann::json JSON object with order details.
     */
    virtual nlohmann::json getOrderDetails(const std::string& orderId);

    /**
     * @brief Get a list of approved orders.
     *
     * @return std::vector<std::string> List of approved order IDs.
     */
    virtual std::vector<std::string> getApprovedOrders();

    /**
     * @brief Insert or update user information in the database.
     *
     * @param userId The ID of the user.
     * @param latitude The latitude of the user's location.
     * @param longitude The longitude of the user's location.
     * @return true if the operation was successful, false otherwise.
     */
    virtual bool insertOrUpdateUser(const std::string& userId, double latitude, double longitude);

    /**
     * @brief Update the user's online status in the database.
     *
     * @param userId The ID of the user.
     * @param isOnline The new online status of the user.
     * @return true if the operation was successful, false otherwise.
     */
    virtual bool updateUserOnlineStatus(const std::string& userId, bool isOnline);

    /**
     * @brief Insert or update inventory for a user.
     *
     * @param userId The ID of the user.
     * @param itemType The type of the item.
     * @param stockLevel The stock level of the item.
     * @param stockThreshold The stock threshold of the item.
     * @return true if the operation was successful, false otherwise.
     */
    virtual bool insertOrUpdateInventory(const std::string& userId, int itemType, int stockLevel, int stockThreshold);

    /**
     * @brief Find a warehouse that can fulfill a request for an item.
     *
     * @param itemType The type of the item.
     * @param quantityNeeded The quantity of the item needed.
     * @return std::string The ID of the warehouse, or an empty string if none is found.
     */
    virtual std::string findWarehouseForItem(int itemType, int quantityNeeded);

    /**
     * @brief Replace all connections declared by a node.
     *
     * Deletes existing rows for fromNodeId, then inserts the new list. Called by
     * Authentication::processClientInfo() when a client declares its transport connections.
     *
     * @param fromNodeId   The declaring existent node's user_id.
     * @param connections  JSON array of connection objects.
     * @return true if all inserts succeeded, false on any failure.
     */
    virtual bool replaceConnections(const std::string& fromNodeId, const nlohmann::json& connections);

    /**
     * @brief Update the stock level for a single inventory item.
     *
     * Used by InventoryManager::handleRestockNotice() to persist the new
     * stock level without touching the threshold.
     *
     * @param userId     The warehouse's user_id.
     * @param itemType   The item type index.
     * @param stockLevel The new stock level to set.
     * @return true if the update succeeded, false otherwise.
     */
    virtual bool updateInventoryStockLevel(const std::string& userId, int itemType, int stockLevel);

    /**
     * @brief Build a JSON snapshot of the live graph from PostgreSQL.
     *
     * Joins users (active, online nodes), inventory (warehouse stock), andconnections
     * (transport topology) into a single JSON array compatible with Graph::build().
     *
     * @return nlohmann::json  JSON array of node objects or an empty array on error.
     */
    virtual nlohmann::json buildGraphSnapshot();

  private:
    /**
     * @brief A pointer to the PostgreSQL connection object.
     *
     * This member variable holds the connection object (`PGconn`) used by
     * the `libpq` library to manage the database connection.
     */
    PGconn* conn;
};

#endif

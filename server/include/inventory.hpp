/**
 * @file inventory.hpp
 * @brief This file contains the InventoryManager class which handles inventory management
 *        for different clients, including updates, restock notices, and supply requests.
 * @version 0.2
 * @date 2025-04-06
 *
 */

#ifndef INVENTORY_MANAGER_H
#define INVENTORY_MANAGER_H

#include "database.hpp"
#include "log.hpp"
#include "sender.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Namespace for JSON library.
 *
 */
using json = nlohmann::json;

/**
 * @brief This struct represents an inventory entry for a client.
 *
 */
struct InventoryEntry
{
    /**
     * @brief The type of item in the inventory.
     *
     */
    int stockLevel;

    /**
     * @brief The type of location where the item is stored.
     *
     */
    std::string locationType;
};

/**
 * @brief This class handles inventory management for different clients.
 *
 */
class InventoryManager
{
  public:
    /**
     * @brief Construct a new Inventory Manager object with dependency injection.
     *
     * @param logger
     * @param database
     */
    InventoryManager(Logger& logger = Logger::getInstance(), Database& database = Database::getInstance())
        : logger(logger), database(database)
    {
    }

    /**
     * @brief Handle an inventory update message from the warehouse.
     *
     * @param jsonData JSON string containing the inventory update message.
     */
    virtual void handleInventoryUpdate(const std::string& jsonData);

    /**
     * @brief Handle a restock notice from the warehouse.
     *
     * @param jsonData JSON string containing the restock notice.
     */
    virtual void handleRestockNotice(const std::string& jsonData);

    /**
     * @brief Find a warehouse that can fulfill the requested items.
     *
     * @param itemType The type of item requested.
     * @param quantityNeeded The quantity of the item needed.
     * @return std::string The ID of the warehouse that can fulfill the request, or an empty string if none is
     * available.
     */
    virtual std::string findWarehouseForItem(int itemType, int quantityNeeded);

    /**
     * @brief Singleton instance of the InventoryManager class.
     *
     * @return InventoryManager& Reference to the singleton instance.
     */
    static InventoryManager& getInstance();

  private:
    std::unordered_map<std::string, std::unordered_map<int, InventoryEntry>> inventory; // user_id -> item_type ->
    // InventoryEntry

    Logger& logger;     // Reference to the Logger instance
    Database& database; // Reference to the Database instance
};

#endif

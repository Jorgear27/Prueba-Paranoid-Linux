/**
 * @file authentication.hpp
 * @brief This file contains the Authentication class which handles receiving and validating
 *        client information and preparing it for database storage.
 * @version 0.2
 * @date 2025-04-06
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef AUTHENTICATION_H
#define AUTHENTICATION_H

#include "database.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "sender.hpp"
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

/**
 * @brief Namespace for JSON library.
 *
 */
using json = nlohmann::json;

#define MAX_RETRIES 3                              ///< Maximum number of retries for database operations
#define RETRY_DELAY std::chrono::milliseconds(100) ///< Delay between retries for database operations

/**
 * @brief This class handles receiving and validating client information
 *        and preparing it for database storage.
 *
 */
class Authentication
{
  public:
    /**
     * @brief Constructor for the Authentication class.
     *
     * @param db Reference to the Database object for database operations.
     * @param sender Reference to the Sender object for managing client connections.
     */
    Authentication(Database& db, Sender& sender = Sender::getInstance()) : database(db), sender(sender)
    {
    }

    /**
     * @brief Process client information received in JSON format.
     *
     * @param clientInfo JSON string containing client information.
     * @param sock Socket file descriptor for the client.
     * @return std::string Response message to the client.
     */
    virtual std::string processClientInfo(const std::string& clientInfo, int sock);

    /**
     * @brief Handle the disconnection of a client.
     *
     * @param jsonData JSON string containing client information.
     * @param sock Socket file descriptor for the client.
     *
     */
    virtual void handleClientDisconnection(const std::string& jsonData, int sock);

  private:
    /**
     * @brief Validate the client information.
     *
     * @param message JSON object containing client information.
     * @return true if the information is valid, false otherwise.
     */
    bool validateClientInfo(const json& message);

    Database& database; //!< The database instance for database operations
    Sender& sender;     //!< The sender instance for managing client connections
};

#endif

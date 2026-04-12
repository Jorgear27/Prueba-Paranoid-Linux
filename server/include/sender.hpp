/**
 * @file sender.hpp
 * @brief Header file for the Sender class, which manages client connections.
 * @version 0.1
 * @date 2025-04-25
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef SENDER_HPP
#define SENDER_HPP

#include <map>
#include <mutex>
#include <string>
#include <sys/socket.h>

/**
 * @class Sender
 * @brief Class responsible for managing client connections and sending messages.
 *
 */
class Sender
{
  public:
    /**
     * @brief Default constructor for the Sender class.
     *
     */
    Sender() = default;

    /**
     * @brief Default destructor for the Sender class.
     *
     */
    ~Sender() = default;

    /**
     * @brief Add a new client connection to the registry.
     *
     * @param clientId
     * @param socket
     */
    void addConnection(const std::string& clientId, int socket);

    /**
     * @brief Remove a client connection from the registry.
     *
     * @param clientId
     */
    virtual void removeConnection(const std::string& clientId);

    /**
     * @brief Get the singleton instance of the Sender class.
     *
     * @return Sender& Reference to the singleton Sender instance.
     */
    static Sender& getInstance(void);

    /**
     * @brief Method to send a JSON message to a specific client.
     *
     * @param clientId
     * @param jsonMessage
     * @return int Returns 0 on success, -1 on failure.
     */
    virtual int sendMessageToClient(const std::string& clientId, const std::string& jsonMessage);

    /**
     * @brief Get the socket associated with a client ID.
     */
    int getConnection(const std::string& clientId);

  private:
    /**
     * @brief Map of client connections.
     *
     */
    std::map<std::string, int> connections;

    /**
     * @brief Mutex for thread safety.
     *
     */
    std::mutex registryMutex;
};

#endif // SENDER_HPP

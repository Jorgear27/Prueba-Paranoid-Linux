/**
 * @file server.hpp
 * @brief This file contains the Server class which handles the main server operations
 *        including initialization, running the server, and handling client connections.
 * @version 0.1
 * @date 2025-04-06
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef SERVER_H
#define SERVER_H

#include "log.hpp"
#include "orders.hpp"
#include "request_router.hpp"
#include "sender.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

/**
 * @brief Default port number for the server.
 *
 */
#define PORT_DEFAULT 8080

/**
 * @brief Maximum number of connections the server can handle.
 *
 */
#define MAX_CONNECTIONS 100

/**
 * @brief Buffer size for reading data from the client.
 *
 */
#define BUFFER_SIZE 1024

/**
 * @brief This class handles the main server operations including initialization,
 *
 */
class Server
{
  public:
    /**
     * @brief Construct a new Server object
     *
     * @param logger       Reference to the logger instance
     * @param router       Reference to the RequestRouter instance
     * @param orderManager Reference to the shared OrderManager instance used to launch
     *                     the background processApprovedOrders thread. Must outlive the Server.
     */
    Server(Logger& logger, RequestRouter& router, OrderManager& orderManager);

    /**
     * @brief Destroy the Server object
     *
     */
    ~Server();

    /**
     * @brief Initialize the server by setting up the socket and binding to the port.
     *
     * @return true if initialization is successful
     * @return false if initialization fails
     */
    bool initialize();

    /**
     * @brief Start the server and listen for incoming connections.
     *
     */
    void run();

    /**
     * @brief Handle incoming client connections.
     *
     * @param clientSocket Socket file descriptor for the client
     */
    void handleClient(int clientSocket);

    /**
     * @brief Handle server shutdown signals.
     *
     * @param signal Signal number
     */
    static void signalHandler(int signal);

    /**
     * @brief Stop the server gracefully.
     *
     */
    void stop(void);

    /**
     * @brief Check if the server is running.
     *
     * @return true if the server is running
     * @return false if the server is stopped
     */
    bool isRunning(void);

  private:
    Logger& logger_;             //!< Reference to the logger instance
    RequestRouter& router_;      //!< Reference to the RequestRouter instance
    OrderManager& orderManager_; //!< Reference to the shared OrderManager instance
    bool running = true;         //!< Flag to control the server loop
    int port;                    //!< Number of port
};

#endif

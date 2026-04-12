#include "authentication.hpp"
#include "database.hpp"
#include "graph_router.hpp"
#include "http_server.hpp"
#include "inventory.hpp"
#include "log.hpp"
#include "mongo_repository.hpp"
#include "orders.hpp"
#include "request_router.hpp"
#include "server.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

int main()
{
    // ------------------------------------------------------------------
    // Connect to PostgreSQL / supply-chain subsystem.
    // ------------------------------------------------------------------
    Database& db = Database::getInstance();

    if (!db.is_connected())
    {
        return EXIT_FAILURE;
    }

    // ------------------------------------------------------------------
    // Build the existing TCP server (port 8080).
    // ------------------------------------------------------------------
    Logger& logger = Logger::getInstance();
    Sender& sender = Sender::getInstance();
    Authentication auth(db, sender);
    InventoryManager inventoryManager;
    OrderManager orderManager;
    RequestRouter router(auth, inventoryManager, orderManager);

    // Create the Server instance with the required dependencies
    Server server(logger, router, orderManager);

    // Initialize the server
    if (!server.initialize())
    {
        logger.log("Main", "[ERROR] Failed to initialize the TCP server.");
        std::cerr << "[ERROR] Failed to initialize the TCP server.\n";
        return EXIT_FAILURE;
    }

    MongoRepository& mongo = MongoRepository::getInstance();
    GraphRouter graphRouter(mongo);

    // Create the HTTP server with the required dependencies
    HttpServer httpServer(graphRouter, logger);

    // Initialize the HTTP server
    if (!httpServer.initialize())
    {
        logger.log("Main", "[ERROR] Failed to initialize the HTTP server.");
        std::cerr << "[ERROR] Failed to initialize the HTTP server.\n";
        return EXIT_FAILURE;
    }

    // Launch the HTTP server in a detached background thread.
    // It runs independently of the TCP server's blocking run() call below.
    std::thread httpThread([&httpServer]() { httpServer.run(); });
    httpThread.detach();

    logger.log("Main", "[INFO] HTTP REST server started on background thread.");
    std::cout << "[INFO] HTTP REST server started on background thread.\n";

    // ------------------------------------------------------------------
    // Run the TCP server — blocks until shutdown signal.
    // ------------------------------------------------------------------
    server.run();

    logger.log("Main", "[INFO] Server started successfully.");
    std::cout << "[INFO] Server started successfully.\n";

    return EXIT_SUCCESS;
}

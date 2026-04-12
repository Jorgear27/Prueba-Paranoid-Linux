/**
 * @file http_server.hpp
 * @brief Minimal HTTP/1.1 TCP listener for the routing engine REST API.
 *
 * HttpServer listens on a configurable port and dispatches requests to GraphRouter
 *
 * HTTP parsing:
 *   The server reads the request line and headers to extract:
 *     - Method (GET / POST)
 *     - Path (e.g. "/map", "/results/?algorithm=bellman_ford&limit=5")
 *     - Content-Length (to read the exact body)
 *
 * Responses:
 *   All responses use Content-Type: application/json and include
 *   Connection: close. Status codes are 200, 400, or 404.
 *
 * @version 0.1
 * @date 2026-04-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include "graph_router.hpp"
#include "log.hpp"
#include <string>
#include <unordered_map>

/**
 * @brief Default port for the HTTP REST API.
 *
 * Overridable via the HTTP_PORT environment variable.
 */
#define HTTP_PORT_DEFAULT 8081

/**
 * @brief Maximum number of pending connections in the accept queue.
 */
#define HTTP_BACKLOG 64

/**
 * @brief Read buffer size for incoming HTTP requests (bytes).
 *
 */
#define HTTP_HEADER_BUFFER 4096

/**
 * @brief Maximum allowed request body size (bytes).
 *
 */
#define HTTP_MAX_BODY_SIZE (10 * 1024 * 1024)

/**
 * @brief Minimal HTTP/1.1 server for the routing engine REST API.
 *
 * HttpServer owns the listening socket and the port. All request logic is delegated to a
 * GraphRouter reference. It has no state beyond the socket descriptor and the running flag.
 */
class HttpServer
{
  public:
    /**
     * @brief Construct a new HttpServer.
     *
     * @param router  Reference to the GraphRouter that handles requests.
     * @param logger  Reference to the shared Logger singleton.
     */
    HttpServer(GraphRouter& router, Logger& logger);

    /**
     * @brief Destructor. Closes the server socket if open.
     */
    ~HttpServer();

    // Non-copyable.
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /**
     * @brief Bind and listen on the configured port.
     *
     * @return true if the socket was bound and is ready to accept.
     * @return false on any socket/bind/listen error.
     */
    bool initialize();

    /**
     * @brief Accept connections and dispatch them until stop() is called.
     *
     * Blocks the calling thread. Each accepted connection is handled in a
     * new detached thread via handleConnection().
     */
    void run();

    /**
     * @brief Signal the server to stop accepting new connections.
     *
     * Sets the running flag to false. The accept loop will exit on its
     * next iteration. Running connections will continue until completion.
     */
    void stop();

    /**
     * @brief Returns true if the server is in the running state.
     */
    bool isRunning() const;

  private:
    /**
     * @brief Parse and respond to a single HTTP connection.
     *
     * Reads the request line, headers, and body; calls router_.route();
     * writes the HTTP response; closes the socket.
     *
     * @param clientSocket  Accepted client socket file descriptor.
     */
    void handleConnection(int clientSocket);

    /**
     * @brief Parse raw HTTP headers into a key→value map.
     *
     * @param raw      Full received buffer including request line and headers.
     * @param method   Output: HTTP method ("GET", "POST", etc.).
     * @param path     Output: Raw request path including query string.
     * @param headers  Output: Parsed header map (lowercase names).
     * @param bodyStart Output: Byte offset where the body begins in raw.
     *
     * @return true if parsing succeeded; false on malformed input.
     */
    bool parseHeaders(const std::string& raw, std::string& method, std::string& path,
                      std::unordered_map<std::string, std::string>& headers, std::size_t& bodyStart);

    /**
     * @brief Extract the path component and query parameters from a URL.
     *
     * Splits "/results/?algorithm=bellman_ford&limit=5" into:
     *   cleanPath = "/results/"
     *   params["algorithm"] = "bellman_ford"
     *   params["limit"] = "5"
     *
     * @param rawPath   Full path string including query string.
     * @param cleanPath Output: Path without query string.
     * @param params    Output: Parsed query parameter map.
     */
    static void parsePathAndQuery(const std::string& rawPath, std::string& cleanPath,
                                  std::unordered_map<std::string, std::string>& params);

    /**
     * @brief Build a complete HTTP/1.1 response string.
     *
     * @param statusCode  HTTP status code (200, 400, 404, etc.).
     * @param body        Response body (JSON string).
     * @return Full HTTP response including status line, headers, and body.
     */
    static std::string buildHttpResponse(int statusCode, const std::string& body);

    /**
     * @brief Returns the standard HTTP reason phrase for a status code.
     *
     * @param code  HTTP status code.
     * @return Reason phrase string (e.g. "OK", "Bad Request", "Not Found").
     */
    static std::string reasonPhrase(int code);

    GraphRouter& router_; ///< Algorithm dispatcher (injected).
    Logger& logger_;      ///< Shared logger (injected).
    bool running_;        ///< Accept-loop control flag.
    int port_;            ///< Resolved listen port.
    int serverFd_;        ///< Server socket file descriptor (-1 if not open).
};

#endif // HTTP_SERVER_HPP

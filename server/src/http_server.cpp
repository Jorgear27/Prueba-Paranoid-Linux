#include "http_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

HttpServer::HttpServer(GraphRouter& router, Logger& logger)
    : router_(router), logger_(logger), running_(false), port_(HTTP_PORT_DEFAULT), serverFd_(-1)
{
}

HttpServer::~HttpServer()
{
    if (serverFd_ >= 0)
    {
        close(serverFd_);
        serverFd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// initialize()
// ---------------------------------------------------------------------------

bool HttpServer::initialize()
{
    // Read port from environment variable.
    const char* portEnv = std::getenv("HTTP_PORT");
    if (portEnv != nullptr && portEnv[0] != '\0')
    {
        try
        {
            port_ = std::stoi(portEnv);
        }
        catch (const std::exception&)
        {
            logger_.log("HttpServer",
                        "[WARN] Invalid HTTP_PORT value. Using default: " + std::to_string(HTTP_PORT_DEFAULT));
            port_ = HTTP_PORT_DEFAULT;
        }
    }

    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0)
    {
        logger_.log("HttpServer", "[ERROR] Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }

    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr
    {
    };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(serverFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        logger_.log("HttpServer",
                    "[ERROR] bind() failed on port " + std::to_string(port_) + ": " + std::string(strerror(errno)));
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    if (listen(serverFd_, HTTP_BACKLOG) < 0)
    {
        logger_.log("HttpServer", "[ERROR] listen() failed: " + std::string(strerror(errno)));
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    logger_.log("HttpServer", "[INFO] HTTP REST server listening on port " + std::to_string(port_));
    std::cout << "[INFO] HTTP REST server listening on port " << port_ << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

void HttpServer::run()
{
    running_ = true;

    struct sockaddr_in clientAddr
    {
    };
    socklen_t addrLen = sizeof(clientAddr);

    while (running_)
    {
        // Use select() with a short timeout so stop() can interrupt the loop.
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverFd_, &readfds);

        struct timeval timeout
        {
        };
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100 ms

        const int activity = select(serverFd_ + 1, &readfds, nullptr, nullptr, &timeout);

        if (activity < 0 && errno != EINTR)
        {
            logger_.log("HttpServer", "[ERROR] select() error: " + std::string(strerror(errno)));
            break;
        }

        if (activity > 0 && FD_ISSET(serverFd_, &readfds))
        {
            const int clientFd = accept(serverFd_, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
            if (clientFd < 0)
            {
                logger_.log("HttpServer", "[WARN] accept() failed: " + std::string(strerror(errno)));
                continue;
            }

            // Handle each connection in its own thread.
            std::thread(&HttpServer::handleConnection, this, clientFd).detach();
        }
    }

    logger_.log("HttpServer", "[INFO] HTTP server stopped.");
}

// ---------------------------------------------------------------------------
// stop() / isRunning()
// ---------------------------------------------------------------------------

void HttpServer::stop()
{
    running_ = false;
    logger_.log("HttpServer", "[INFO] Stopping HTTP server...");
}

bool HttpServer::isRunning() const
{
    return running_;
}

// ---------------------------------------------------------------------------
// handleConnection()
// ---------------------------------------------------------------------------

void HttpServer::handleConnection(int clientSocket)
{
    // ------------------------------------------------------------------
    // Read raw request into a buffer. We read until we have all headers
    // (terminated by \r\n\r\n), then read the body using Content-Length.
    // ------------------------------------------------------------------
    std::string raw;
    raw.reserve(HTTP_HEADER_BUFFER);

    char buf[HTTP_HEADER_BUFFER];
    bool headersComplete = false;
    std::size_t bodyStart = 0;

    while (!headersComplete)
    {
        const ssize_t n = recv(clientSocket, buf, sizeof(buf) - 1, 0);
        if (n <= 0)
        {
            close(clientSocket);
            return;
        }
        buf[n] = '\0';
        raw.append(buf, static_cast<std::size_t>(n));

        const std::size_t pos = raw.find("\r\n\r\n");
        if (pos != std::string::npos)
        {
            headersComplete = true;
            bodyStart = pos + 4;
        }

        if (raw.size() > HTTP_HEADER_BUFFER && !headersComplete)
        {
            // Headers too large — reject.
            const std::string resp = buildHttpResponse(400, "{\"status\":\"error\",\"message\":\"Headers too large\"}");
            send(clientSocket, resp.c_str(), resp.size(), 0);
            close(clientSocket);
            return;
        }
    }

    // ------------------------------------------------------------------
    // Parse headers.
    // ------------------------------------------------------------------
    std::string method;
    std::string rawPath;
    std::unordered_map<std::string, std::string> headers;

    if (!parseHeaders(raw, method, rawPath, headers, bodyStart))
    {
        const std::string resp = buildHttpResponse(400, "{\"status\":\"error\",\"message\":\"Malformed request\"}");
        send(clientSocket, resp.c_str(), resp.size(), 0);
        close(clientSocket);
        return;
    }

    // ------------------------------------------------------------------
    // Read body using Content-Length.
    // ------------------------------------------------------------------
    std::string body;

    const auto clIt = headers.find("content-length");
    if (clIt != headers.end())
    {
        std::size_t contentLength = 0;
        try
        {
            contentLength = static_cast<std::size_t>(std::stoul(clIt->second));
        }
        catch (const std::exception&)
        {
            contentLength = 0;
        }

        if (contentLength > HTTP_MAX_BODY_SIZE)
        {
            const std::string resp =
                buildHttpResponse(400, "{\"status\":\"error\",\"message\":\"Request body too large\"}");
            send(clientSocket, resp.c_str(), resp.size(), 0);
            close(clientSocket);
            return;
        }

        // We may already have part of the body in `raw` past bodyStart.
        body = raw.substr(bodyStart);

        while (body.size() < contentLength)
        {
            const ssize_t n = recv(clientSocket, buf, sizeof(buf) - 1, 0);
            if (n <= 0)
            {
                break;
            }
            buf[n] = '\0';
            body.append(buf, static_cast<std::size_t>(n));
        }

        if (body.size() > contentLength)
        {
            body.resize(contentLength);
        }
    }

    // ------------------------------------------------------------------
    // Parse path and query string, then route.
    // ------------------------------------------------------------------
    std::string cleanPath;
    std::unordered_map<std::string, std::string> queryParams;
    parsePathAndQuery(rawPath, cleanPath, queryParams);

    int statusCode = 200;
    std::string responseBody;

    // GET /results/ may require query params, different to POST requests.
    if (method == "GET" && cleanPath == "/results/")
    {
        const std::string filter = queryParams.count("algorithm") != 0 ? queryParams.at("algorithm") : "";
        int limit = 20;
        if (queryParams.count("limit") != 0)
        {
            try
            {
                limit = std::stoi(queryParams.at("limit"));
            }
            catch (const std::exception&)
            {
                limit = 20;
            }
        }
        responseBody = router_.handleGetResults(filter, limit, statusCode);
    }
    else
    {
        responseBody = router_.route(method, cleanPath, body, statusCode);
    }

    const std::string httpResponse = buildHttpResponse(statusCode, responseBody);
    send(clientSocket, httpResponse.c_str(), httpResponse.size(), 0);
    close(clientSocket);
}

// ---------------------------------------------------------------------------
// parseHeaders()
// ---------------------------------------------------------------------------

bool HttpServer::parseHeaders(const std::string& raw, std::string& method, std::string& path,
                              std::unordered_map<std::string, std::string>& headers, std::size_t& bodyStart)
{
    std::istringstream stream(raw);
    std::string line;

    // Request line: "METHOD /path HTTP/1.1"
    if (!std::getline(stream, line))
    {
        return false;
    }
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    std::istringstream reqLine(line);
    std::string version;
    if (!(reqLine >> method >> path >> version))
    {
        return false;
    }

    // Header lines: "Name: value\r\n"
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            break; // Blank line — end of headers.
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue; // Malformed header — skip.
        }

        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Lowercase the name for case-insensitive lookup.
        for (char& c : name)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        // Trim leading whitespace from value.
        const std::size_t valueStart = value.find_first_not_of(' ');
        if (valueStart != std::string::npos)
        {
            value = value.substr(valueStart);
        }

        headers[name] = value;
    }

    // bodyStart was set by the caller from the \r\n\r\n position.
    (void)bodyStart; // Already set by handleConnection.
    return true;
}

// ---------------------------------------------------------------------------
// parsePathAndQuery()
// ---------------------------------------------------------------------------

void HttpServer::parsePathAndQuery(const std::string& rawPath, std::string& cleanPath,
                                   std::unordered_map<std::string, std::string>& params)
{
    const std::size_t qpos = rawPath.find('?');
    if (qpos == std::string::npos)
    {
        cleanPath = rawPath;
        return;
    }

    cleanPath = rawPath.substr(0, qpos);
    const std::string qs = rawPath.substr(qpos + 1);

    // Parse key=value pairs separated by '&'.
    std::istringstream qstream(qs);
    std::string pair;
    while (std::getline(qstream, pair, '&'))
    {
        const std::size_t eq = pair.find('=');
        if (eq == std::string::npos)
        {
            params[pair] = "";
        }
        else
        {
            params[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
    }
}

// ---------------------------------------------------------------------------
// buildHttpResponse()
// ---------------------------------------------------------------------------

std::string HttpServer::buildHttpResponse(int statusCode, const std::string& body)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << reasonPhrase(statusCode) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

// ---------------------------------------------------------------------------
// reasonPhrase()
// ---------------------------------------------------------------------------

std::string HttpServer::reasonPhrase(int code)
{
    switch (code)
    {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}

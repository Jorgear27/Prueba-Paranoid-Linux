#include "sender.hpp"

void Sender::addConnection(const std::string& clientId, int socket)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    connections[clientId] = socket;
}

void Sender::removeConnection(const std::string& clientId)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    connections.erase(clientId);
}

int Sender::getConnection(const std::string& clientId)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    if (connections.find(clientId) != connections.end())
    {
        return connections[clientId];
    }
    return -1; // Return -1 if the client is not found
}

Sender& Sender::getInstance(void)
{
    static Sender instance;
    return instance;
}

int Sender::sendMessageToClient(const std::string& clientId, const std::string& jsonMessage)
{
    int socket = getConnection(clientId);
    if (socket != -1)
    {
        return send(socket, jsonMessage.c_str(), jsonMessage.size(), 0);
    }
    else
    {
        printf("Client %s not found\n", clientId.c_str());
        return -1; // Return -1 if the client is not found
    }
}

#include "log.hpp"

Logger::Logger()
{
    logFile.open(LOG_FILE, std::ios::app); // Open log file in append mode
    if (!logFile.is_open())
    {
        std::cerr << "[ERROR] Failed to open log file.\n";
    }
}

Logger::~Logger()
{
    if (logFile.is_open())
    {
        logFile.close();
    }
}

Logger& Logger::getInstance()
{
    static Logger instance; // Singleton instance
    return instance;
}

void Logger::log(const std::string& component, const std::string& message)
{
    std::lock_guard<std::mutex> lock(logMutex); // Ensure thread-safe logging

    // Get the current time
    std::time_t now = std::time(nullptr);
    char timeBuffer[20];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    // Write the log entry
    if (logFile.is_open())
    {
        logFile << "[" << timeBuffer << "] [" << component << "] " << message << "\n";
        logFile.flush(); // Ensure the log entry is written to the file immediately
    }
    else
    {
        std::cerr << "[ERROR] Log file is not open.\n";
    }
}

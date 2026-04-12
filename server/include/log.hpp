/**
 * @file log.hpp
 * @brief Logger class for handling logging messages for server components.
 * @version 0.1
 * @date 2025-04-06
 *
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

/**
 * @def LOG_FILE
 * @brief Path to the log file.
 */
#define LOG_FILE "paranoid_linux.log"

/**
 * @class Logger
 * @brief Thread-safe logger implementation using singleton pattern.
 *
 */
class Logger
{
  public:
    /**
     * @brief Get the singleton instance of the Logger.
     *
     * @return Logger& Reference to the Logger instance.
     */
    static Logger& getInstance();

    /**
     * @brief Log a message to the log file.
     *
     * @param component The component generating the log (e.g., "Authentication", "Inventory").
     * @param message The message to be logged.
     */
    virtual void log(const std::string& component, const std::string& message);

    /**
     * @brief Private constructor for singleton pattern.
     */
    Logger();

    /**
     * @brief Destructor that closes the log file.
     */
    virtual ~Logger();

  private:
    // Delete copy constructor and assignment operator
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream logFile; //!< Log file stream
    std::mutex logMutex;   //!< Mutex for thread-safe logging
};

#endif

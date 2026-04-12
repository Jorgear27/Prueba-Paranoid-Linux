#include "log.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

// Test for logging a message
TEST(LoggerTest, LogMessage)
{
    // Clear the log file before the test
    std::ofstream clearLog("server.log", std::ios::trunc);
    clearLog.close();

    Logger& logger = Logger::getInstance();
    logger.log("TestComponent", "This is a test log message.");

    std::ifstream logFile("server.log");
    ASSERT_TRUE(logFile.is_open());

    std::string lastLine, line;
    while (std::getline(logFile, line))
    {
        if (!line.empty())
        {
            lastLine = line; // Keep updating until the last non-empty line
        }
    }
    logFile.close();

    EXPECT_NE(lastLine.find("TestComponent"), std::string::npos);
    EXPECT_NE(lastLine.find("This is a test log message."), std::string::npos);
}

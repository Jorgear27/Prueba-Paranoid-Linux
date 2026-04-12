#include "sender.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

// Override the send system call
extern "C"
{
    ssize_t send(int sockfd, const void* buf, size_t len, int flags)
    {
        // Avoid unused parameter warnings
        (void)sockfd;
        (void)buf;
        (void)flags;

        // Simulate successful message sending
        return len; // Return the number of bytes "sent"
    }
}

// Test fixture for Sender
class SenderTest : public ::testing::Test
{
  protected:
    Sender& sender = Sender::getInstance();

    void SetUp() override
    {
        // Clear connections before each test
        sender.removeConnection("client1");
        sender.removeConnection("client2");
    }

    void TearDown() override
    {
        // Clear connections after each test
        sender.removeConnection("client1");
        sender.removeConnection("client2");
    }
};

// Test addConnection
TEST_F(SenderTest, AddConnection)
{
    sender.addConnection("client1", 10);
    EXPECT_EQ(sender.sendMessageToClient("client1", "test message"), 12); // Expect the length of "test message"
}

// Test removeConnection
TEST_F(SenderTest, RemoveConnection)
{
    sender.addConnection("client1", 10);
    sender.removeConnection("client1");
    EXPECT_EQ(sender.sendMessageToClient("client1", "test message"), -1);
}

// Test getConnection
TEST_F(SenderTest, GetConnection)
{
    sender.addConnection("client1", 10);
    EXPECT_EQ(sender.getConnection("client1"), 10);
    EXPECT_EQ(sender.getConnection("client2"), -1); // Client not found
}

// Test sendMessageToClient
TEST_F(SenderTest, SendMessageToClient)
{
    sender.addConnection("client1", 10);
    sender.removeConnection("client1");
    sender.sendMessageToClient("client1", "test message");
}

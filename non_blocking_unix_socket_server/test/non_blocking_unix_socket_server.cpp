#include "non_blocking_unix_socket_server.h"
#include <gtest/gtest.h>

namespace InterProcessCommunication::Test
{
class NonBlockingUnixSocketServerTest : public ::testing::Test
{
protected:
    const std::string m_unix_socket_path = "test.sock";

    void SetUp() {}
    void TearDown() {}
};

TEST_F(NonBlockingUnixSocketServerTest, Constructor)
{
    NonBlockingUnixSocketServer server(m_unix_socket_path);
}

}


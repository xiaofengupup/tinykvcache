#include "tinykv/net/socket_util.h"

#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <iostream>

namespace {

TEST(SocketUtilTest, CreateListenSocket)
{
    auto server = tinykv::CreateListenSocket("127.0.0.1", 0);
    EXPECT_TRUE(server.Valid());
}

TEST(SocketUtilTest, SetNonBlocking)
{
    auto server = tinykv::CreateListenSocket("127.0.0.1", 0);
    EXPECT_TRUE(server.Valid());

    tinykv::SetNonBlocking(server.Get());
}

} // namespace

#include "test_utils.h"

#include "tinykv/net/socket_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <iostream>

namespace {

void TestCreateListenSocket()
{
    auto server = tinykv::CreateListenSocket("127.0.0.1", 0);
    TINYKV_CHECK(server.Valid());
}

void TestSetNonBlocking()
{
    auto server = tinykv::CreateListenSocket("127.0.0.1", 0);
    TINYKV_CHECK(server.Valid());

    tinykv::SetNonBlocking(server.Get());
}

} // namespace

int main()
{
    TestCreateListenSocket();
    TestSetNonBlocking();

    std::cout << "socket util tests passed\n";
    return 0;
}
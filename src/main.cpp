#include "non_blocking_unix_socket_server.h"
#include <iostream>

using namespace InterProcessCommunication;

const uint MINIMUM_CLI_ARGS = 2;

int main(int argc, char* argv[])
{
    if(argc < MINIMUM_CLI_ARGS)
    {
        std::cerr << "Invalid CLI arguments. Expecting 1 argument: <unix socket path>" << std::endl;
        return 1;
    }

    const std::string unix_socket_path = std::string(argv[1]);

    NonBlockingUnixSocketServer server(unix_socket_path, 1, std::chrono::milliseconds{1000});
    server.Start();

    while(true)
    {
        server.Run();
    }

    return 0;
}
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main() {
    const char* socket_path = "/tmp/my_socket"; // Path to the Unix domain socket

    // Create a socket
    int client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        return 1;
    }

    // Set up the server address
    sockaddr_un server_address{};
    server_address.sun_family = AF_UNIX;
    strncpy(server_address.sun_path, socket_path, sizeof(server_address.sun_path) - 1);

    // Connect to the server
    if (connect(client_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        perror("Connect failed");
        close(client_socket);
        return 1;
    }

    std::cout << "Connected to the server!" << std::endl;

    // Send a message to the server
    const char* message = "Hello from the client!";
    if (send(client_socket, message, strlen(message), 0) == -1) {
        perror("Send failed");
        close(client_socket);
        return 1;
    }

    std::cout << "Message sent to server: " << message << std::endl;

    // Clean up
    close(client_socket);

    return 0;
}

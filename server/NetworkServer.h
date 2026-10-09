#pragma once

#include <vector>
#include <winsock2.h>

namespace FalconMirror {

// A simple TCP server that listens on a port, accepts one client, and
// implements the frame protocol [4-byte size] + [N-byte JPEG data]
// for sending each frame.
class NetworkServer {
public:
    NetworkServer() = default;
    ~NetworkServer();

    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;

    // Initializes Winsock and binds/listens on the given port.
    // Returns false on failure.
    bool start(unsigned short port);

    // Blocks until a new client connects.
    // Returns true on success.
    bool waitForClient();

    // Sends one JPEG frame using the [size][data] protocol to the
    // current client. Returns false if the connection was lost.
    bool sendFrame(const std::vector<unsigned char>& jpegData);

    // Closes the current client's connection (before waiting for a new one)
    void closeClient();

    // Cleans up everything (sockets and Winsock)
    void stop();

private:
    SOCKET serverSocket_ = INVALID_SOCKET;
    SOCKET clientSocket_ = INVALID_SOCKET;
    bool wsaInitialized_ = false;
};

} // namespace FalconMirror

// Author: ArshiaAlikhani
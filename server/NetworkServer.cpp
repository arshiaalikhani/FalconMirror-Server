#include "NetworkServer.h"

#include <iostream>

#pragma comment(lib, "ws2_32.lib")

namespace FalconMirror {

namespace {
bool sendAll(SOCKET sock, const char* buffer, int length) {
    int sent = 0;
    while (sent < length) {
        int result = send(sock, buffer + sent, length - sent, 0);
        if (result <= 0) return false;
        sent += result;
    }
    return true;
}
} // namespace

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(unsigned short port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[NetworkServer] WSAStartup failed!\n";
        return false;
    }
    wsaInitialized_ = true;

    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ == INVALID_SOCKET) {
        std::cerr << "[NetworkServer] Socket creation failed!\n";
        return false;
    }

    sockaddr_in serverAddress = {};
    serverAddress.sin_family      = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port        = htons(port);

    if (bind(serverSocket_, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "[NetworkServer] Bind failed! Error: " << WSAGetLastError() << "\n";
        return false;
    }

    if (listen(serverSocket_, 1) == SOCKET_ERROR) {
        std::cerr << "[NetworkServer] Listen failed!\n";
        return false;
    }

    std::cout << "[NetworkServer] Listening on port " << port << "...\n";
    return true;
}

bool NetworkServer::waitForClient() {
    std::cout << "[NetworkServer] Waiting for client connection...\n";
    clientSocket_ = accept(serverSocket_, NULL, NULL);
    if (clientSocket_ == INVALID_SOCKET) {
        std::cerr << "[NetworkServer] Accept failed!\n";
        return false;
    }

    int flag = 1;
    setsockopt(clientSocket_, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    std::cout << "[NetworkServer] Client connected!\n";
    return true;
}

bool NetworkServer::sendFrame(const std::vector<unsigned char>& jpegData) {
    uint32_t frameSize = htonl((uint32_t)jpegData.size());
    if (!sendAll(clientSocket_, (char*)&frameSize, 4)) return false;
    if (!sendAll(clientSocket_, (char*)jpegData.data(), (int)jpegData.size())) return false;
    return true;
}

void NetworkServer::closeClient() {
    if (clientSocket_ != INVALID_SOCKET) {
        closesocket(clientSocket_);
        clientSocket_ = INVALID_SOCKET;
    }
}

void NetworkServer::stop() {
    closeClient();
    if (serverSocket_ != INVALID_SOCKET) {
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }
    if (wsaInitialized_) {
        WSACleanup();
        wsaInitialized_ = false;
    }
}

} // namespace FalconMirror

// Author: ArshiaAlikhani
// TrayApp.cpp
// Falcon Mirror - Windows System Tray Application
//
// Runs with NO console window and NO taskbar button. On launch it drops
// straight into the system tray (notification area). Right-click gives
// three options: Connect, About, Exit.
//
// The actual screen-capture + network-server logic (ScreenCapture /
// NetworkServer) is unchanged - it just runs on a background thread now,
// independent of the tray UI thread.
//
// ---------------------------------------------------------------------
// BUILD NOTES (Code::Blocks):
//   1. This file REPLACES main.cpp as the program's entry point.
//      Only ONE of them (main.cpp OR TrayApp.cpp) should be compiled
//      into the project at a time - both define a program entry point.
//   2. Project -> Build options -> Linker settings -> "Other linker
//      options" -> add:  -mwindows
//      This switches the binary to the GUI subsystem (no console window)
//      and makes WinMain the entry point instead of main().
//   3. Make sure these libraries are linked (already declared via
//      #pragma comment below, but add them under Linker -> Link
//      libraries too if your toolchain ignores pragmas):
//        ws2_32, shell32, gdi32 (gdi32 is required by ScreenCapture.cpp)
// ---------------------------------------------------------------------
//
// Author: ArshiaAlikhani

// Must be defined BEFORE windows.h/winsock2.h are included, otherwise
// MinGW targets an old Windows API level where getaddrinfo/inet_ntop/
// freeaddrinfo aren't declared.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // Windows Vista or later
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>

#include "ScreenCapture.h"
#include "NetworkServer.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

using namespace FalconMirror;

namespace {

constexpr UINT WM_TRAYICON     = WM_APP + 1;
constexpr UINT ID_TRAY_CONNECT = 1001;
constexpr UINT ID_TRAY_ABOUT   = 1002;
constexpr UINT ID_TRAY_EXIT    = 1003;
constexpr unsigned short SERVER_PORT = 5000;
constexpr wchar_t WINDOW_CLASS_NAME[] = L"FalconMirrorTrayWindowClass";

NOTIFYICONDATAA g_nid = {};
HWND g_hWnd = nullptr;
std::atomic<bool> g_clientConnected{false};
std::atomic<bool> g_running{true};

// Returns the machine's local IPv4 addresses (excluding loopback) as a
// display-friendly string, so the user knows what to type into the
// Android client's manual-IP field.
std::string getLocalIPv4Text() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return "Could not determine hostname.";
    }

    addrinfo hints = {};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) {
        return "Could not resolve local IP address.";
    }

    std::string text;
    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        // inet_ntoa is older than inet_ntop but is declared by every
        // MinGW/TDM-GCC version, unlike inet_ntop on some older toolchains.
        char* ipStr = inet_ntoa(addr->sin_addr);
        if (ipStr != nullptr && std::strcmp(ipStr, "127.0.0.1") != 0) {
            text += ipStr;
            text += "\n";
        }
    }
    freeaddrinfo(result);

    if (text.empty()) {
        return "No active network connection detected.";
    }
    return text;
}

void showConnectDialog() {
    std::string message =
        "Falcon Mirror is running.\n\n"
        "On your Android device, connect to:\n\n" +
        getLocalIPv4Text() +
        "Port: " + std::to_string(SERVER_PORT) + "\n\n" +
        (g_clientConnected.load()
            ? "Status: A client is currently connected."
            : "Status: Waiting for a client to connect.");

    MessageBoxA(g_hWnd, message.c_str(), "Falcon Mirror - Connect",
                MB_OK | MB_ICONINFORMATION);
}

void showAboutDialog() {
    MessageBoxA(g_hWnd,
        "Falcon Mirror\n"
        "Screen mirroring server for Windows.\n\n"
        "Author: ArshiaAlikhani\n"
        "github.com/arshiaalikhani/FalconMirror",
        "About Falcon Mirror",
        MB_OK | MB_ICONINFORMATION);
}

void addTrayIcon(HWND hWnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    strncpy(g_nid.szTip, "Falcon Mirror", sizeof(g_nid.szTip) - 1);
    g_nid.szTip[sizeof(g_nid.szTip) - 1] = '\0';
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

void removeTrayIcon() {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

void showTrayMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_CONNECT, "Connect");
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_ABOUT,   "About");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT,    "Exit");

    // Needed so the popup menu closes correctly if the user clicks away
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
    PostMessage(hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                showTrayMenu(hWnd);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_CONNECT:
                    showConnectDialog();
                    return 0;
                case ID_TRAY_ABOUT:
                    showAboutDialog();
                    return 0;
                case ID_TRAY_EXIT:
                    DestroyWindow(hWnd);
                    return 0;
            }
            break;

        case WM_DESTROY:
            g_running = false;
            removeTrayIcon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Background thread: keeps the capture/send loop running for the whole
// lifetime of the app, independent of the tray UI. Same protocol as the
// original main.cpp ([4-byte size][JPEG data]), just moved off the main
// thread so the tray/message loop stays responsive.
void serverThreadFunc() {
    NetworkServer server;
    if (!server.start(SERVER_PORT)) {
        MessageBoxA(g_hWnd, "Failed to start the network server.",
                    "Falcon Mirror - Error", MB_OK | MB_ICONERROR);
        return;
    }

    ScreenCapture capture;
    const int targetFPS = 30;
    const double targetFrameMs = 1000.0 / targetFPS;
    const int jpegQuality = 40;

    while (g_running) {
        if (!server.waitForClient()) {
            continue;
        }

        g_clientConnected = true;

        while (g_running) {
            auto frameStart = std::chrono::steady_clock::now();

            std::vector<unsigned char> frame = capture.captureFrame(jpegQuality);
            if (frame.empty() || !server.sendFrame(frame)) {
                break;
            }

            auto now = std::chrono::steady_clock::now();
            double elapsedMs = std::chrono::duration<double, std::milli>(now - frameStart).count();
            double sleepMs = targetFrameMs - elapsedMs;
            if (sleepMs > 0) {
                Sleep(static_cast<DWORD>(sleepMs));
            }
        }

        g_clientConnected = false;
        server.closeClient();
    }

    server.stop();
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Prevent a second instance (a second tray icon would just be
    // confusing, and only one process can bind port 5000 anyway).
    HANDLE hMutex = CreateMutexA(nullptr, TRUE, "FalconMirrorSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(nullptr, "Falcon Mirror is already running.",
                    "Falcon Mirror", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassExW(&wc);

    // Message-only-style hidden window: exists purely to receive tray
    // and menu messages. It is never shown, so no taskbar button appears.
    g_hWnd = CreateWindowExW(0, WINDOW_CLASS_NAME, L"Falcon Mirror",
                              0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    addTrayIcon(g_hWnd);

    std::thread server(serverThreadFunc);
    server.detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WSACleanup();
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return static_cast<int>(msg.wParam);
}

// Author: ArshiaAlikhani

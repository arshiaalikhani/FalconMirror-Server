// ============================================================
//  FalconMirror - Server (real screen-capture + streaming side)
//  Runs on the Windows PC. Captures the screen, JPEG-encodes it
//  in memory, and streams it to whoever connects (test client or
//  the Android app).
// ============================================================

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <iostream>
#include <vector>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

// ============================================================
//  کال‌بک stb_image_write: به‌جای نوشتن روی دیسک، بایت‌های JPEG
//  رو داخل یک std::vector می‌ریزه (انکود کاملاً در حافظه)
// ============================================================
static void jpegWriteCallback(void* context, void* data, int size) {
    std::vector<unsigned char>* buffer =
        reinterpret_cast<std::vector<unsigned char>*>(context);
    unsigned char* bytes = reinterpret_cast<unsigned char*>(data);
    buffer->insert(buffer->end(), bytes, bytes + size);
}

// ============================================================
//  captureScreen: کل صفحه (یا مجموعه‌ی مانیتورها) رو می‌گیره،
//  به RGB تبدیل می‌کنه و JPEG برمی‌گردونه
//  quality: کیفیت JPEG بین 1 تا 100
// ============================================================
std::vector<unsigned char> captureScreen(int quality) {
    // 1. ابعاد کل صفحه‌ی مجازی (اگه چند مانیتور باشه هم پوشش میده)
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width   = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height  = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (width <= 0 || height <= 0) {
        std::cerr << "[captureScreen] Invalid screen dimensions\n";
        return {};
    }

    // 2. گرفتن پیکسل‌ها با GDI (BitBlt)
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) return {};

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    BOOL ok = BitBlt(hMemoryDC, 0, 0, width, height,
                      hScreenDC, screenX, screenY, SRCCOPY | CAPTUREBLT);

    if (!ok) {
        std::cerr << "[captureScreen] BitBlt failed\n";
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return {};
    }

    // 3. تعریف فرمتی که از GetDIBits می‌خوایم: 24-bit BGR, top-down
    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = width;
    bi.biHeight      = -height; // منفی یعنی top-down (بدون این، تصویر وارونه میشه)
    bi.biPlanes      = 1;
    bi.biBitCount    = 24;
    bi.biCompression = BI_RGB;

    int rowSize = ((width * 3 + 3) & ~3); // هر ردیف باید مضرب 4 بایت باشه (پدینگ GDI)
    std::vector<unsigned char> bgrBuffer(static_cast<size_t>(rowSize) * height);

    int linesCopied = GetDIBits(hMemoryDC, hBitmap, 0, height, bgrBuffer.data(),
                                 (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    // 4. آزاد کردن منابع GDI
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);

    if (linesCopied == 0) {
        std::cerr << "[captureScreen] GetDIBits failed\n";
        return {};
    }

    // 5. تبدیل BGR -> RGB و حذف پدینگ ردیف‌ها (stb_image_write بافر فشرده می‌خواد)
    std::vector<unsigned char> rgbBuffer(static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        unsigned char* srcRow = bgrBuffer.data() + static_cast<size_t>(y) * rowSize;
        unsigned char* dstRow = rgbBuffer.data() + static_cast<size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x) {
            dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // R
            dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
            dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // B
        }
    }

    // 6. انکود به JPEG، مستقیم داخل حافظه (بدون فایل روی دیسک)
    std::vector<unsigned char> jpegBuffer;
    stbi_write_jpg_to_func(jpegWriteCallback, &jpegBuffer,
                            width, height, 3, rgbBuffer.data(), quality);

    return jpegBuffer;
}

// ============================================================
//  sendAll: تضمین می‌کنه کل بافر ارسال بشه (مکمل recvAll سمت کلاینت)
// ============================================================
bool sendAll(SOCKET sock, const char* buffer, int length) {
    int sent = 0;
    while (sent < length) {
        int result = send(sock, buffer + sent, length - sent, 0);
        if (result <= 0) return false;
        sent += result;
    }
    return true;
}

// ============================================================
//  main (سرور واقعی)
// ============================================================
int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed!\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cout << "Socket creation failed!\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family      = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY; // روی همه‌ی رابط‌های شبکه گوش بده
    serverAddress.sin_port        = htons(5000);

    if (bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cout << "Bind failed! Error: " << WSAGetLastError() << "\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, 1) == SOCKET_ERROR) {
        std::cout << "Listen failed!\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "=== Falcon Mirror Server ===\n";
    std::cout << "Listening on port 5000...\n";
    std::cout << "Waiting for client connection...\n";

    // نکته: این حلقه‌ی بیرونی باعث میشه بعد از قطع شدن یک کلاینت،
    // سرور دوباره منتظر اتصال جدید بمونه (به‌جای بسته شدن کامل برنامه)
    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cout << "Accept failed!\n";
            continue;
        }

        std::cout << "Client connected! Starting stream...\n\n";

        int frameCount = 0;
        while (true) {
            std::vector<unsigned char> frame = captureScreen(40);

            if (frame.empty()) {
                std::cout << "[ERROR] Capture failed! frame is empty.\n";
                break;
            }

            // پروتکل: [4 بایت size] + [N بایت JPEG]
            uint32_t frameSize = htonl((uint32_t)frame.size());

            if (!sendAll(clientSocket, (char*)&frameSize, 4)) {
                std::cout << "[ERROR] Connection lost while sending size.\n";
                break;
            }

            if (!sendAll(clientSocket, (char*)frame.data(), (int)frame.size())) {
                std::cout << "[ERROR] Connection lost while sending data.\n";
                break;
            }

            frameCount++;
            std::cout << "[OK] Frame #" << frameCount << " sent (" << frame.size() << " bytes)\n";

            Sleep(50); // تقریباً 20 فریم بر ثانیه
        }

        closesocket(clientSocket);
        std::cout << "Client disconnected. Waiting for a new connection...\n\n";
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}

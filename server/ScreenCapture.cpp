#include "ScreenCapture.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <iostream>

#pragma comment(lib, "gdi32.lib")

namespace FalconMirror {

namespace {

// stb_image_write callback: instead of writing to disk, appends the
// encoded JPEG bytes into a std::vector (fully in-memory encoding).
void jpegWriteCallback(void* context, void* data, int size) {
    auto* buffer = reinterpret_cast<std::vector<unsigned char>*>(context);
    auto* bytes  = reinterpret_cast<unsigned char*>(data);
    buffer->insert(buffer->end(), bytes, bytes + size);
}

} // namespace

ScreenCapture::~ScreenCapture() {
    release();
}

void ScreenCapture::release() {
    if (hMemoryDC_) {
        SelectObject(hMemoryDC_, hOldBitmap_);
        DeleteDC(hMemoryDC_);
        hMemoryDC_ = nullptr;
    }
    if (hBitmap_) {
        DeleteObject(hBitmap_);
        hBitmap_ = nullptr;
    }
    if (hScreenDC_) {
        ReleaseDC(NULL, hScreenDC_);
        hScreenDC_ = nullptr;
    }
    pixels_ = nullptr;
    width_  = 0;
    height_ = 0;
}

bool ScreenCapture::setup(int width, int height) {
    release();

    hScreenDC_ = GetDC(NULL);
    if (!hScreenDC_) return false;

    // 32-bit so each row is always a multiple of 4 bytes (no manual padding needed)
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height; // negative = top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    hBitmap_ = CreateDIBSection(hScreenDC_, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hBitmap_) return false;

    hMemoryDC_  = CreateCompatibleDC(hScreenDC_);
    hOldBitmap_ = (HBITMAP)SelectObject(hMemoryDC_, hBitmap_);
    pixels_     = reinterpret_cast<unsigned char*>(bits);
    width_      = width;
    height_     = height;
    return true;
}

std::vector<unsigned char> ScreenCapture::captureFrame(int quality) {
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width   = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height  = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (width <= 0 || height <= 0) return {};

    // Only rebuild resources if the screen size changed (or first run)
    if (width != width_ || height != height_) {
        if (!setup(width, height)) {
            std::cerr << "[ScreenCapture] Failed to set up capture resources\n";
            return {};
        }
    }

    BOOL ok = BitBlt(hMemoryDC_, 0, 0, width, height,
                      hScreenDC_, screenX, screenY, SRCCOPY | CAPTUREBLT);
    if (!ok) {
        std::cerr << "[ScreenCapture] BitBlt failed\n";
        return {};
    }

    // Convert BGRA -> RGB directly from the DIB memory (no extra copy)
    const int pixelCount = width * height;
    std::vector<unsigned char> rgbBuffer(static_cast<size_t>(pixelCount) * 3);
    const unsigned char* src = pixels_;
    for (int i = 0; i < pixelCount; ++i) {
        rgbBuffer[i * 3 + 0] = src[i * 4 + 2]; // R
        rgbBuffer[i * 3 + 1] = src[i * 4 + 1]; // G
        rgbBuffer[i * 3 + 2] = src[i * 4 + 0]; // B
    }

    std::vector<unsigned char> jpegBuffer;
    stbi_write_jpg_to_func(jpegWriteCallback, &jpegBuffer,
                            width, height, 3, rgbBuffer.data(), quality);
    return jpegBuffer;
}

} // namespace FalconMirror

// Author: ArshiaAlikhani
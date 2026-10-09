#pragma once

#include <vector>
#include <windows.h>

namespace FalconMirror {

// Responsible for capturing the Windows screen and encoding it as JPEG.
// Keeps GDI resources alive across calls and only rebuilds them when the
// screen resolution changes (e.g. a monitor gets plugged/unplugged), so we
// avoid the cost of creating/destroying them on every single frame.
class ScreenCapture {
public:
    ScreenCapture() = default;
    ~ScreenCapture();

    // Non-copyable, since it owns OS-level GDI handles.
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Captures one frame and returns it JPEG-encoded.
    // quality ranges from 1 to 100 (default 40, a balance of quality/speed).
    // Returns an empty vector on failure.
    std::vector<unsigned char> captureFrame(int quality = 40);

private:
    bool setup(int width, int height);
    void release();

    HDC hScreenDC_      = nullptr;
    HDC hMemoryDC_      = nullptr;
    HBITMAP hBitmap_    = nullptr;
    HBITMAP hOldBitmap_ = nullptr;
    unsigned char* pixels_ = nullptr; // Direct pointer to pixel buffer (from CreateDIBSection)
    int width_  = 0;
    int height_ = 0;
};

} // namespace FalconMirror

// Author: ArshiaAlikhani
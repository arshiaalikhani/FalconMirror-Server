<div align="center">

# FalconMirror — Windows Server

**Lightweight desktop streaming from Windows to Android, powered by C++.**

[![Platform](https://img.shields.io/badge/platform-Windows-0078D4?style=flat-square)](https://www.microsoft.com/windows)
![Language](https://img.shields.io/badge/language-C%2B%2B-00599C?style=flat-square)
![Transport](https://img.shields.io/badge/transport-TCP-475569?style=flat-square)
![Status](https://img.shields.io/badge/status-early%20development-F59E0B?style=flat-square)

**Android companion:** [**FalconMirror**](https://github.com/arshiaalikhani/FalconMirror)

</div>

---

## Overview

**FalconMirror Windows Server** is the C++ desktop-side component for [FalconMirror](https://github.com/arshiaalikhani/FalconMirror), an Android screen-mirroring project.

It captures the Windows desktop, compresses each frame into JPEG, and streams the result over a TCP connection to the Android client. The current implementation focuses on **one-way, local-network screen mirroring** with a minimal Windows system-tray interface.

> [!NOTE]
> This repository contains the **Windows streaming server**. The Android application is maintained separately in the [FalconMirror repository](https://github.com/arshiaalikhani/FalconMirror).

## Features

- **Live desktop capture** using the Windows GDI API (`BitBlt`).
- **Multi-monitor coverage** through the Windows virtual desktop coordinates.
- **In-memory JPEG encoding** using `stb_image_write.h`; frames are not written to image files.
- **TCP-based streaming** with a simple length-prefixed frame protocol.
- **One connected viewer at a time**, with the server returning to a listening state after disconnection.
- **System-tray interface** with **Connect**, **About**, and **Exit** actions.
- **Local IPv4 address display** in the Connect dialog.
- **Single-instance protection** to prevent multiple copies of the tray application from starting.
- **Reusable capture resources** to reduce repeated GDI allocation.

The current tray-based server targets **30 FPS** with **JPEG quality 40**. Actual performance depends on display resolution, hardware, and network conditions.

## How It Works

```text
         WINDOWS PC                              ANDROID DEVICE
   ┌────────────────────┐                   ┌────────────────────┐
   │    ScreenCapture   │                   │    FalconMirror    │
   │  Windows GDI/BitBlt │                   │    Android app     │
   └─────────┬──────────┘                   └──────────▲─────────┘
             │                                         │
             ▼                                         │
   ┌────────────────────┐                   ┌──────────┴─────────┐
   │     JPEG encoder   │                   │  JPEG frame decode │
   │  stb_image_write   │                   │     and display    │
   └─────────┬──────────┘                   └──────────▲─────────┘
             │                                         │
             ▼                                         │
   ┌────────────────────┐    TCP / port 5000 ┌─────────┴──────────┐
   │    NetworkServer   ├───────────────────►│   Android client   │
   └────────────────────┘                   └────────────────────┘
```

Each video frame is transmitted as:

```text
┌─────────────────────────────┬───────────────────────────────┐
│ 4-byte unsigned frame size  │ N bytes of JPEG image data    │
│     (network byte order)    │                               │
└─────────────────────────────┴───────────────────────────────┘
```

The server converts the frame length with `htonl()` and sends the length before the JPEG bytes. The receiver must read **exactly four bytes**, interpret the size in **big-endian** order, then read exactly that many JPEG bytes. TCP is a byte stream, so neither the header nor the image is guaranteed to arrive in a single read.

## Repository Layout

```text
.
├── server/
│   ├── TrayApp.cpp          # Windows tray UI and streaming loop (active entry point)
│   ├── NetworkServer.h      # TCP server interface
│   ├── NetworkServer.cpp    # Socket setup, connections, and frame transmission
│   ├── ScreenCapture.h      # Screen capture interface
│   ├── ScreenCapture.cpp    # GDI capture and JPEG encoding
│   ├── stb_image_write.h    # Third-party JPEG encoder (bundled)
│   └── server.cbp           # Code::Blocks project
└── server_main.cpp          # Older, standalone console-based implementation
```

> **Important:** The Code::Blocks project uses `TrayApp.cpp` as its entry point. `server_main.cpp` is an alternative implementation and must **not** be compiled into the same executable as `TrayApp.cpp`.

## Requirements

- A **Windows PC** with access to a desktop session.
- A **C++11-capable** Windows compiler (for example, MinGW-w64/GCC).
- **Code::Blocks** (recommended for the included `.cbp` project), or another compatible C++ build environment.
- An Android device running the [FalconMirror client](https://github.com/arshiaalikhani/FalconMirror).
- Both devices connected to the **same trusted local network** for the initial setup.

**Windows libraries:** `ws2_32`, `shell32`, and `gdi32` (provided by the Windows toolchain/SDK).

## Build

### Option A — Code::Blocks

1. Open `server/server.cbp` in Code::Blocks.
2. Select your configured GCC/MinGW toolchain.
3. Choose **Build → Build** (or **Build and Run**).
4. Launch the generated server executable. With `-mwindows`, it starts without a console window and places an icon in the Windows notification area.

The supplied project already specifies the required linker flags and Windows libraries.

### Option B — MinGW-w64 Command Line

From the `server` directory, run:

```bash
g++ -std=c++11 -O2 -Wall -mwindows \
    TrayApp.cpp NetworkServer.cpp ScreenCapture.cpp \
    -o FalconMirror.exe \
    -lws2_32 -lshell32 -lgdi32
```

Use a Windows MinGW-w64 shell/toolchain. **Do not add `server_main.cpp`** to this build command.

## Run & Connect

1. Start `FalconMirror.exe` on your Windows PC. Look for its icon in the **system tray** (you may need to expand hidden icons).
2. Right-click the icon and select **Connect** to view the PC's local IPv4 address(es), listening port, and connection status.
3. Ensure the Android device and PC are on the same trusted LAN or Wi-Fi network.
4. Open the [FalconMirror Android app](https://github.com/arshiaalikhani/FalconMirror) and configure its destination to use the **PC's IPv4 address** and **TCP port `5000`**, using the connection method supported by your Android client version. Do not assume the client's default IP matches your PC.
5. Connect from Android. Once connected, the server begins sending JPEG frames.
6. To stop the Windows application, select **Exit** from its tray menu.

If Windows Firewall asks for permission, allow access **only on trusted private networks** as appropriate. Do not expose TCP port `5000` to the public internet.

## Configuration

| Setting | Current value | Defined in |
| --- | --- | --- |
| TCP port | `5000` | `TrayApp.cpp` → `SERVER_PORT` |
| Target frame rate | `30 FPS` | `TrayApp.cpp` → `targetFPS` |
| JPEG quality | `40` (scale: 1–100) | `TrayApp.cpp` → `jpegQuality` |
| Bind address | All local IPv4 interfaces (`INADDR_ANY`) | `NetworkServer.cpp` |
| Connected viewers | One at a time | Current server design |

These values are configured in the source code; there is currently no external settings file or configuration screen for the Windows server.

## Current Limitations & Security

This is an **early-stage, experimental implementation**, not a hardened remote-desktop product.

- **No authentication or encryption:** anyone who can reach the server's listening socket may be able to view the desktop. Use it only on a trusted network; do not forward the port or expose it publicly.
- **Screen mirroring only:** remote mouse/keyboard control, file transfer, audio streaming, and multi-client viewing are **not implemented**.
- **Shutdown handling needs improvement:** the current tray application detaches its worker thread, so graceful cancellation of blocking socket operations is not guaranteed.
- **JPEG-over-TCP performance varies:** large displays and slow links can introduce latency; there is no adaptive bitrate or hardware video encoder.
- **Desktop capture has platform limitations:** protected, secure, or otherwise inaccessible Windows surfaces may not be captured correctly.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| No tray icon | Expand the hidden-icons area; verify the executable started. |
| Android cannot connect | Verify the PC's actual LAN IP, port `5000`, Wi-Fi/LAN reachability, and Windows Firewall rules. |
| Server cannot listen | Check whether another process is already using port `5000`. |
| A previous address stopped working | The PC's local IP may have changed after reconnecting to the network; reopen **Connect**. |
| Black, incomplete, or delayed image | Check display-capture restrictions, CPU load, network quality, and capture resolution. |
| Build/link errors | Use a Windows C++ compiler, link `ws2_32`, `shell32`, and `gdi32`, and compile only one entry point. |

## Roadmap

Possible directions for future versions include:

- [ ] Configurable network and streaming settings
- [ ] Client pairing, authentication, and encrypted transport
- [ ] Robust connection lifecycle and clean shutdown
- [ ] Lower-latency video streaming
- [ ] Remote mouse and keyboard input (with explicit user authorization)
- [ ] Audio streaming and clipboard synchronization
- [ ] File transfer and additional device-management features

These are **ideas for future development**, not features of the current release.

## Related Project

**FalconMirror — Android client**  
https://github.com/arshiaalikhani/FalconMirror

## Credits

**Developed by [Arshia Alikhani](https://github.com/arshiaalikhani).**

JPEG encoding uses the bundled [`stb_image_write.h`](https://github.com/nothings/stb) library. Refer to its source header for the applicable third-party license notices.

---

<div align="center">

**FalconMirror Windows Server** · Native C++ · Windows → Android

</div>

# DX12-Overlay

A high-performance, transparent DirectX 12 overlay implementation using DirectComposition and Dear ImGui.

## Features

- **Transparent Overlay**: Uses DirectComposition with premultiplied alpha for per-pixel transparency
- **High Performance**: Reuses command allocators and descriptor heaps across frames
- **Modern DX12**: Flip model swap chain with waitable frame latency for optimal pacing
- **Customizable Hotkeys**: Rebindable toggle and exit keys with modifier support
- **FPS Control**: Configurable frame rate limiter with accurate hybrid sleep/spin pacing
- **FPS Counter**: Smooth, stable FPS display using exponential moving average

## Requirements

- **Windows 10** or later (DirectComposition support)
- **DirectX 12** capable GPU
- **Visual Studio 2022** (v143 toolset) or later
- **Windows SDK 10.0** or later

## Dependencies

All dependencies are included in the repository:
- **Dear ImGui** (for UI rendering)
- **DirectX 12** libraries (d3d12.lib, dxgi.lib, d3dcompiler.lib, dcomp.lib)

## Building

### Visual Studio

1. Open `Roblox.vcxproj` in Visual Studio 2022
2. Select your desired configuration:
   - **Debug|x64** - For development with debug symbols
   - **Release|x64** - Optimized build for production
3. Build the project (Ctrl+Shift+B)
4. Run the executable from the output directory

### Command Line (MSBuild)

```cmd
# Debug build
msbuild Roblox.vcxproj /p:Configuration=Debug /p:Platform=x64

# Release build
msbuild Roblox.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Usage

1. Launch the executable
2. The overlay will appear as a transparent window on top of other applications
3. Default hotkeys:
   - **INSERT** - Toggle overlay visibility
   - **DELETE** - Exit application
4. Use the UI to:
   - Rebind hotkeys (supports Ctrl, Alt, Shift modifiers)
   - Adjust FPS limiter (0 = unlimited)
   - Monitor frame rate and frame time

## Architecture

### Resource Management

- **ComPtr Usage**: All DirectX and DXGI objects use `Microsoft::WRL::ComPtr` for automatic reference counting
- **RAII Pattern**: Resources are properly released in destructors
- **Frame Synchronization**: Fence-based synchronization prevents GPU stalls

### Performance Optimizations

- **Command Allocator Reuse**: Per-frame command allocators eliminate recreation overhead
- **Descriptor Heap Caching**: SRV and RTV heaps created once and reused
- **Waitable Swap Chain**: Reduces CPU overhead and improves frame pacing
- **Minimal State Changes**: Viewport and scissor rect set only when needed

### Error Handling

- **HRESULT Checking**: All DirectX API calls check for errors
- **Debug Logging**: Error messages with HRESULT codes output to debugger
- **Graceful Degradation**: Falls back to simpler configurations when features unavailable

## Code Structure

```
DX12-Overlay/
├── main.cpp              # Application entry point, message loop, ImGui setup
├── DX12Renderer.h/cpp    # DirectX 12 rendering engine
├── OverlayWindow.h/cpp   # Win32 window management
├── imgui/                # Dear ImGui library files
└── Roblox.vcxproj        # Visual Studio project file
```

## Customization

### Changing Overlay Size

Modify the window creation in `main.cpp`:

```cpp
window.Create(L"DX12", width, height);
```

### Adjusting Frame Rate

In the UI or modify the default in `main.cpp`:

```cpp
static int g_targetFps = 144; // 0 = unlimited, or set to desired FPS
```

### DirectComposition vs HWND Mode

Toggle in `main.cpp`:

```cpp
// Transparent composition (default)
renderer.Initialize(window.GetHandle(), /*transparentComposition=*/true);

// Opaque HWND mode
renderer.Initialize(window.GetHandle(), /*transparentComposition=*/false);
```

## Troubleshooting

### Overlay Not Visible

- Ensure DirectComposition is supported (Windows 10+)
- Check if another application is blocking topmost windows
- Verify GPU drivers are up to date

### Performance Issues

- Reduce FPS limit if CPU usage is high
- Check for other applications using significant GPU resources
- In Release builds, ensure compiler optimizations are enabled

### Build Errors

- Verify Windows SDK 10.0 is installed
- Ensure Visual Studio 2022 with C++ desktop development workload
- Check that all imgui source files are present

## Performance Metrics

The overlay includes built-in performance monitoring:
- **Instant FPS**: Real-time frame rate
- **Smoothed FPS**: Exponentially smoothed for stable display
- **Frame Time**: Milliseconds per frame

## License

This project is provided as-is for educational and development purposes.

## Contributing

When contributing, ensure:
1. All DirectX resources use ComPtr
2. HRESULT values are checked and logged
3. Code follows existing style conventions
4. Performance-critical paths are optimized

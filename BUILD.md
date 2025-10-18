# Build Instructions for DX12-Overlay

This document provides detailed instructions for building DX12-Overlay from source.

## Prerequisites

### Required Software

1. **Windows 10 or later** (version 1903 or newer recommended)
   - DirectComposition requires Windows 10 for transparency support

2. **Visual Studio 2022** (recommended) or Visual Studio 2019
   - Download from: https://visualstudio.microsoft.com/downloads/
   - Required workload: "Desktop development with C++"
   - Required components:
     - MSVC v143 (or v142 for VS2019) - C++ build tools
     - Windows 10 SDK (10.0.19041.0 or later)
     - C++ ATL for latest build tools

3. **Windows SDK 10.0** or later
   - Included with Visual Studio
   - Standalone download: https://developer.microsoft.com/windows/downloads/windows-sdk/

### Optional Tools

- **PIX for Windows**: GPU debugging and profiling (https://devblogs.microsoft.com/pix/)
- **RenderDoc**: Graphics debugging (https://renderdoc.org/)
- **clang-format**: Code formatting (included with Visual Studio or standalone)

## Build Steps

### Using Visual Studio IDE

1. **Clone the repository**
   ```bash
   git clone https://github.com/nyydsatead/DX12-Overlay.git
   cd DX12-Overlay
   ```

2. **Open the project**
   - Double-click `Roblox.vcxproj` or open it from Visual Studio
   - Visual Studio will load the project

3. **Select configuration**
   - Choose build configuration from dropdown:
     - **Debug|x64**: For development with full debug symbols
     - **Release|x64**: Optimized build for production use
   - Note: x86 (Win32) builds are available but x64 is recommended

4. **Build the project**
   - Press `Ctrl+Shift+B` or select `Build > Build Solution`
   - Build output will be in `x64/Debug/` or `x64/Release/`

5. **Run the application**
   - Press `F5` to run with debugger, or `Ctrl+F5` to run without debugging
   - The overlay window should appear on your desktop

### Using MSBuild (Command Line)

1. **Open Developer Command Prompt for VS 2022**
   - Start Menu > Visual Studio 2022 > Developer Command Prompt for VS 2022

2. **Navigate to project directory**
   ```cmd
   cd C:\path\to\DX12-Overlay
   ```

3. **Build Debug configuration**
   ```cmd
   msbuild Roblox.vcxproj /p:Configuration=Debug /p:Platform=x64
   ```

4. **Build Release configuration**
   ```cmd
   msbuild Roblox.vcxproj /p:Configuration=Release /p:Platform=x64
   ```

5. **Run the executable**
   ```cmd
   x64\Release\Roblox.exe
   ```

### Using CMake (Advanced)

> Note: Currently the project uses Visual Studio project files. CMake support can be added if needed.

To add CMake support, create a `CMakeLists.txt` file:

```cmake
cmake_minimum_required(VERSION 3.20)
project(DX12Overlay)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
file(GLOB SOURCES
    "*.cpp"
    "imgui/*.cpp"
)

file(GLOB HEADERS
    "*.h"
    "imgui/*.h"
)

add_executable(DX12Overlay WIN32 ${SOURCES} ${HEADERS})

target_include_directories(DX12Overlay PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_link_libraries(DX12Overlay PRIVATE
    d3d12
    dxgi
    d3dcompiler
    dcomp
)
```

## Build Configuration Details

### Debug Configuration

- **Preprocessor Defines**: `_DEBUG`, `_CONSOLE`
- **Optimization**: Disabled (`/Od`)
- **Runtime Library**: Multi-threaded Debug DLL (`/MDd`)
- **Debug Info**: Full (`/Zi`)
- **Features**:
  - D3D12 Debug Layer enabled
  - Detailed error messages
  - Assertions active
  - No optimizations for easier debugging

### Release Configuration

- **Preprocessor Defines**: `NDEBUG`, `_CONSOLE`
- **Optimization**: Maximum speed (`/O2`)
- **Runtime Library**: Multi-threaded DLL (`/MD`)
- **Debug Info**: Program Database (`/Zi`) for crash analysis
- **Features**:
  - Full optimizations
  - Function-level linking
  - Intrinsic functions
  - No debug layer overhead

## Project Structure

```
DX12-Overlay/
├── main.cpp              # Application entry point and main loop
├── DX12Renderer.cpp/h    # DirectX 12 rendering engine
├── DX12Utils.h           # Utility functions for error handling
├── OverlayWindow.cpp/h   # Win32 window management
├── imgui/                # Dear ImGui library (included)
│   ├── imgui.cpp/h
│   ├── imgui_draw.cpp
│   ├── imgui_impl_dx12.cpp/h
│   ├── imgui_impl_win32.cpp/h
│   └── ...
├── Roblox.vcxproj        # Visual Studio project file
├── README.md             # Project documentation
├── PERFORMANCE.md        # Performance optimization guide
└── CONTRIBUTING.md       # Contribution guidelines
```

## Dependencies

All dependencies are included in the repository:

### DirectX 12 Libraries (System)
- `d3d12.lib` - Direct3D 12 API
- `dxgi.lib` - DirectX Graphics Infrastructure
- `d3dcompiler.lib` - Shader compiler
- `dcomp.lib` - DirectComposition

### Dear ImGui (Included)
- Version: Latest compatible (check imgui directory)
- Backend implementations for DX12 and Win32 included
- No external installation required

## Troubleshooting

### Build Errors

**Error: Cannot open include file 'd3d12.h'**
- **Solution**: Install Windows SDK 10.0 via Visual Studio Installer
- Go to: Tools > Get Tools and Features > Individual Components
- Select: Windows 10 SDK (10.0.19041.0 or later)

**Error: MSB8036: The Windows SDK version was not found**
- **Solution**: Update project to use installed SDK version
- Right-click project > Properties > General > Windows SDK Version
- Select available SDK from dropdown

**Error: LNK2019: Unresolved external symbol**
- **Solution**: Ensure all required libraries are linked
- Check: Project Properties > Linker > Input > Additional Dependencies
- Should include: d3d12.lib;dxgi.lib;d3dcompiler.lib;dcomp.lib

**Error: C1083: Cannot open source file**
- **Solution**: Verify all source files are present
- Check imgui directory exists and contains all .cpp files
- Re-clone repository if files are missing

### Runtime Errors

**Application fails to launch**
- Ensure Windows 10 or later (DirectComposition requirement)
- Update GPU drivers to latest version
- Check Windows Event Viewer for application errors

**Black screen/No overlay visible**
- Verify DirectComposition is supported on your system
- Try running as administrator
- Check if another overlay is conflicting

**Performance issues**
- Build in Release configuration (Debug builds are slower)
- Close unnecessary background applications
- Adjust FPS limiter in the UI

## Performance Verification

After building, verify performance:

1. Run the Release build
2. Check FPS counter in the overlay window
3. Expected performance:
   - **Idle**: 144+ FPS (or unlimited)
   - **Frame Time**: <7ms on modern hardware
   - **CPU Usage**: <5% on modern CPUs
   - **Memory**: ~50-100 MB

If performance is significantly worse, check:
- Built in Release configuration (not Debug)
- GPU drivers are up to date
- No background GPU-intensive applications
- Windows is not in power saving mode

## Clean Build

To perform a clean build:

**Visual Studio:**
1. Build > Clean Solution
2. Build > Rebuild Solution

**Command Line:**
```cmd
msbuild Roblox.vcxproj /t:Clean /p:Configuration=Release /p:Platform=x64
msbuild Roblox.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

## Code Formatting

To format code using clang-format:

```bash
# Format all source files
clang-format -i *.cpp *.h

# Format specific file
clang-format -i DX12Renderer.cpp
```

The project includes a `.clang-format` configuration file with the style guide.

## Continuous Integration

For automated builds, use:

```yaml
# Example GitHub Actions workflow
name: Build
on: [push, pull_request]
jobs:
  build:
    runs-on: windows-latest
    steps:
    - uses: actions/checkout@v2
    - name: Setup MSBuild
      uses: microsoft/setup-msbuild@v1
    - name: Build
      run: msbuild Roblox.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Next Steps

After successful build:
1. Read [README.md](README.md) for usage instructions
2. Check [PERFORMANCE.md](PERFORMANCE.md) for optimization details
3. Review [CONTRIBUTING.md](CONTRIBUTING.md) before making changes
4. Run the application and test the overlay functionality

## Support

For build issues:
1. Check existing issues on GitHub
2. Ensure all prerequisites are installed
3. Try a clean rebuild
4. Open an issue with build output and system details

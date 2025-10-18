#pragma once
#include <d3d12.h>
#include <comdef.h>
#include <sstream>
#include <iomanip>
#include <string>

// ---- Utility functions for DX12 development ----

namespace DX12Utils {

    // Convert HRESULT to a human-readable error string with hex code
    inline std::string HResultToString(HRESULT hr) {
        _com_error err(hr);
        LPCTSTR msg = err.ErrorMessage();
        std::wstring wmsg = msg ? msg : L"(no message)";
        std::string s(wmsg.begin(), wmsg.end());
        std::ostringstream ss;
        ss << "HRESULT 0x" << std::hex << std::setw(8) << std::setfill('0') << (unsigned)hr << " : " << s;
        return ss.str();
    }

    // Output HRESULT error to debug console (Windows OutputDebugString)
    inline void OutputHRError(const char* operation, HRESULT hr) {
        std::ostringstream ss;
        ss << operation << " failed. " << HResultToString(hr) << "\n";
        OutputDebugStringA(ss.str().c_str());
    }

    // Check HRESULT and output error if failed, returns true if succeeded
    inline bool CheckHR(const char* operation, HRESULT hr) {
        if (FAILED(hr)) {
            OutputHRError(operation, hr);
            return false;
        }
        return true;
    }

#ifdef _DEBUG
    // Debug-only logging
    inline void DebugLog(const char* message) {
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
    }
#else
    // No-op in release builds
    inline void DebugLog(const char*) {}
#endif

    // Helper to get the name of a DXGI format
    inline const char* GetFormatName(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        default: return "UNKNOWN";
        }
    }

} // namespace DX12Utils

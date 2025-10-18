#include "OverlayWindow.h"
#include "imgui_impl_win32.h"
#include <windows.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool OverlayWindow::Create(const wchar_t* title, int width, int height) {
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProcStatic;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DX12Class";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // no background paint (we draw via DComp/DX)
    
    // RegisterClassExW returns 0 on failure, but ERROR_CLASS_ALREADY_EXISTS is acceptable
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            OutputDebugStringA("Failed to register window class.\n");
            return false;
        }
    }

    // Create overlay window
    // IMPORTANT:
    // - No WS_EX_LAYERED (flip-model + DXGI composition doesn't need it and it breaks some drivers)
    // - WS_EX_TOPMOST to keep on top of other windows
    // - WS_POPUP | WS_VISIBLE for borderless transparent overlay
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        title,
        WS_POPUP | WS_VISIBLE,
        0, 0, width, height,
        nullptr, nullptr, wc.hInstance, this
    );

    if (!m_hwnd) {
        OutputDebugStringA("Failed to create window.\n");
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    m_visible = true;
    return true;
}

void OverlayWindow::Destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClassW(L"DX12Class", GetModuleHandle(nullptr));
}

bool OverlayWindow::ProcessMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void OverlayWindow::SetClickThrough(bool enable) {
    if (!m_hwnd) return;
    
    m_clickThrough = enable;
    LONG ex = GetWindowLongW(m_hwnd, GWL_EXSTYLE);
    if (enable)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongW(m_hwnd, GWL_EXSTYLE, ex);
}

void OverlayWindow::Show() {
    if (m_hwnd && !m_visible) {
        ShowWindow(m_hwnd, SW_SHOW);
        m_visible = true;
    }
}

void OverlayWindow::Hide() {
    if (m_hwnd && m_visible) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
    }
}

LRESULT CALLBACK OverlayWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* window = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    else {
        window = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window)
        return window->WndProc(hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    // Keep default behavior for window messages
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_NCHITTEST:
        if (m_clickThrough) return HTTRANSPARENT;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
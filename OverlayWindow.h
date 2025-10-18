#pragma once
#include <Windows.h>
#include <functional>

class OverlayWindow {
public:
    using RenderCallback = std::function<void()>;

    bool Create(const wchar_t* title, int width, int height);
    void Destroy();
    bool ProcessMessages();
    void SetRenderCallback(RenderCallback callback) { m_renderCallback = callback; }
    HWND GetHandle() const { return m_hwnd; }
    bool IsVisible() const { return m_visible; }
    // Optional: enable mouse click-through (pass-through)
    void SetClickThrough(bool enable);

    // NEW: show/hide window
    void Show();
    void Hide();

private:
    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    bool m_visible = true;
    bool m_clickThrough = false;
    RenderCallback m_renderCallback;
};
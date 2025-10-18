#include <windows.h>
#include <chrono>                    // for throttling rebind scans and FPS limiter
#include <thread>                    // for sleep/yield in FPS limiter
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>                 // for std::find
#include <cmath>                     // for std::exp
#include "DX12Renderer.h"
#include "OverlayWindow.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

// -- FPS Limiter -------------------------------------------------------------
//
// Accurate frame pacing with a hybrid sleep+spin technique.
// Call BeginFrame() at the start of your frame and EndFrame(targetFps) at the end.
// If targetFps <= 0, the limiter is disabled (unlimited).
class FpsLimiter {
public:
    void BeginFrame() {
        m_frameStart = Clock::now();
    }

    void EndFrame(int targetFps) {
        if (targetFps <= 0) return; // unlimited

        using namespace std::chrono;
        const auto frameDuration = duration_cast<Clock::duration>(duration<double>(1.0 / targetFps));
        const auto targetTime = m_frameStart + frameDuration;

        auto now = Clock::now();
        if (now >= targetTime) return; // already late, skip sleeping

        // Coarse sleep using sleep_until to reduce oversleeping
        auto coarseUntil = targetTime - milliseconds(2);
        if (coarseUntil > now) {
            std::this_thread::sleep_until(coarseUntil);
        }

        // Fine spin/yield until target time is reached
        while ((now = Clock::now()) < targetTime) {
            std::this_thread::yield();
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_frameStart = Clock::now();
};

// -- FPS Counter (Stable and Accurate) ---------------------------------------
//
// Measures Start-to-Start frame intervals and computes:
// - Instant FPS (1/dt)
// - Smoothed FPS and smoothed frametime using EMA with time-constant tau.
// The EMA uses a time-dependent alpha so smoothing stays consistent across FPS.
class FpsCounter {
public:
    // Call once at the beginning of each main-loop iteration
    void StartFrame() {
        const auto now = Clock::now();
        if (m_hasPrev) {
            const double dt = std::chrono::duration<double>(now - m_prev).count();

            // Ignore absurdly large pauses (e.g., debugger break) to keep EMA stable
            if (dt > 0.0) {
                const double clampedDt = dt > m_maxDt ? m_maxDt : dt;
                m_instFps = 1.0 / clampedDt;

                // Time-dependent alpha for EMA
                const double alpha = 1.0 - std::exp(-clampedDt / m_tauSec);

                if (!m_initialized) {
                    m_smoothFps = m_instFps;
                    m_smoothMs = clampedDt * 1000.0;
                    m_initialized = true;
                }
                else {
                    m_smoothFps = (1.0 - alpha) * m_smoothFps + alpha * m_instFps;
                    const double ms = clampedDt * 1000.0;
                    m_smoothMs = (1.0 - alpha) * m_smoothMs + alpha * ms;
                }
            }
        }
        m_prev = now;
        m_hasPrev = true;
    }

    double InstantFPS() const { return m_instFps; }      // may be jittery
    double SmoothedFPS() const { return m_smoothFps; }   // stable
    double SmoothedMs() const { return m_smoothMs; }     // stable

    void Reset() {
        m_hasPrev = false;
        m_initialized = false;
        m_instFps = 0.0;
        m_smoothFps = 0.0;
        m_smoothMs = 0.0;
    }

    // Optional: adjust smoothing time-constant at runtime (in seconds)
    void SetTau(double tauSeconds) { m_tauSec = (tauSeconds > 0.01 ? tauSeconds : 0.01); }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_prev{};
    bool   m_hasPrev{ false };
    bool   m_initialized{ false };
    double m_instFps{ 0.0 };
    double m_smoothFps{ 0.0 };
    double m_smoothMs{ 0.0 };
    double m_tauSec{ 0.7 };          // EMA time constant (~0.7s gives smooth yet responsive readout)
    const double m_maxDt{ 0.25 };    // clamp dt to 250ms to avoid large spikes polluting EMA
};

// -- Improved hotkey state and configuration --
struct Keybind {
    int vkCode = 0;     // Virtual key code
    bool ctrl = false;  // Modifier keys
    bool alt = false;
    bool shift = false;

    // Check if this keybind matches current key state
    bool Matches(int vk, bool ctrlDown, bool altDown, bool shiftDown) const {
        return vk == vkCode && ctrl == ctrlDown && alt == altDown && shift == shiftDown;
    }

    // For debugging and equality checks
    bool operator==(const Keybind& other) const {
        return vkCode == other.vkCode && ctrl == other.ctrl &&
            alt == other.alt && shift == other.shift;
    }
};

// Keybind configurations
static Keybind g_toggleOverlayBind = { VK_INSERT };  // default: INSERT toggles overlay
static Keybind g_exitBind = { VK_DELETE };           // default: DELETE exits

// Keybind state
static bool g_overlayVisible = true;
static bool g_waitingForToggleBind = false;
static bool g_waitingForExitBind = false;
static bool g_ctrlDown = false;
static bool g_altDown = false;
static bool g_shiftDown = false;

// FPS limiter and counter settings
static FpsLimiter g_fpsLimiter;
static FpsCounter g_fpsCounter;
static int g_targetFps = 144; // 0 = Unlimited

// Helper to get the display name of a key with modifiers
static std::string GetKeybindDisplayName(const Keybind& bind) {
    std::string result;
    if (bind.ctrl) result += "Ctrl+";
    if (bind.alt) result += "Alt+";
    if (bind.shift) result += "Shift+";

    // Get key name using Windows API
    char keyName[32] = { 0 };
    UINT scanCode = MapVirtualKeyA(bind.vkCode, MAPVK_VK_TO_VSC);
    LONG lparam = (scanCode << 16);

    // Handle extended keys
    if (bind.vkCode == VK_UP || bind.vkCode == VK_DOWN || bind.vkCode == VK_LEFT || bind.vkCode == VK_RIGHT ||
        bind.vkCode == VK_PRIOR || bind.vkCode == VK_NEXT || bind.vkCode == VK_END || bind.vkCode == VK_HOME ||
        bind.vkCode == VK_INSERT || bind.vkCode == VK_DELETE || bind.vkCode == VK_DIVIDE) {
        lparam |= 0x1000000; // Extended bit
    }

    if (GetKeyNameTextA(lparam, keyName, sizeof(keyName))) {
        result += keyName;
    }
    else {
        // Fallback for function keys
        if (bind.vkCode >= VK_F1 && bind.vkCode <= VK_F24) {
            result += "F" + std::to_string(bind.vkCode - VK_F1 + 1);
        }
        else {
            // Generic fallback
            result += "Key(" + std::to_string(bind.vkCode) + ")";
        }
    }

    return result;
}

// Update modifier key states based on current keyboard state
static void UpdateModifierStates() {
    g_ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_RCONTROL) & 0x8000);

    g_altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) ||
        (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
        (GetAsyncKeyState(VK_RMENU) & 0x8000);

    g_shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
        (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ||
        (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
}

// Check if a key is a modifier key
static bool IsModifierKey(int vkCode) {
    return (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
        vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU ||
        vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT);
}

// Check for conflicts between keybinds
static bool HasConflict(const Keybind& bind1, const Keybind& bind2) {
    return bind1.vkCode == bind2.vkCode &&
        bind1.ctrl == bind2.ctrl &&
        bind1.alt == bind2.alt &&
        bind1.shift == bind2.shift;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Improve DPI handling on modern Windows (optional)
    HMODULE hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32) {
        using SetDpiAwarenessContext_t = BOOL(WINAPI*)(HANDLE);
        auto SetDpiAwarenessContextFn = reinterpret_cast<SetDpiAwarenessContext_t>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (SetDpiAwarenessContextFn) {
            SetDpiAwarenessContextFn((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
        }
        FreeLibrary(hUser32);
    }

    // Create overlay window
    OverlayWindow window;
    if (!window.Create(L"DX12", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))) {
        MessageBoxW(nullptr, L"Failed to create overlay window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Initialize renderer with transparent DirectComposition swap chain
    DX12Renderer renderer;
    if (!renderer.Initialize(window.GetHandle(), /*transparentComposition=*/true)) {
        MessageBoxW(nullptr, L"Failed to initialize DX12 renderer.", L"Error", MB_OK | MB_ICONERROR);
        window.Destroy();
        return 1;
    }

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;

    ImGui_ImplWin32_Init(window.GetHandle());
    ImGui_ImplDX12_Init(
        renderer.GetDevice(),
        DX12Renderer::FRAME_COUNT,
        renderer.GetBackBufferFormat(),
        renderer.GetSRVHeap(),
        renderer.GetSRVHeap()->GetCPUDescriptorHandleForHeapStart(),
        renderer.GetSRVHeap()->GetGPUDescriptorHandleForHeapStart()
    );

    // Ensure font atlas is built (upload handled by backend)
    {
        unsigned char* pixels = nullptr; int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h, nullptr);
        UNUSED(pixels); UNUSED(w); UNUSED(h);
    }

    // Main loop
    bool running = true;

    // Timer for throttling rebind scans
    auto lastRebindScan = std::chrono::steady_clock::now();
    const auto scanInterval = std::chrono::milliseconds(100);

    // List of keys to ignore for bindings (modifiers and special system keys)
    std::vector<int> blacklistedKeys = {
        VK_LWIN, VK_RWIN,  // Windows keys
        VK_APPS,           // Context menu key
        VK_CAPITAL,        // Caps Lock
        VK_NUMLOCK,        // Num Lock
        VK_SCROLL          // Scroll Lock
    };

    while (running && window.ProcessMessages()) {
        // Begin frame timing for FPS limiter and counter
        g_fpsLimiter.BeginFrame();
        g_fpsCounter.StartFrame();

        // Update modifier key states
        UpdateModifierStates();

        // Handle active keybind detection
        if (!g_waitingForToggleBind && !g_waitingForExitBind) {
            // Check for toggle overlay keybind
            if (GetAsyncKeyState(g_toggleOverlayBind.vkCode) & 0x8000) {
                if ((g_toggleOverlayBind.ctrl == g_ctrlDown) &&
                    (g_toggleOverlayBind.alt == g_altDown) &&
                    (g_toggleOverlayBind.shift == g_shiftDown))
                {
                    // Toggle overlay visibility
                    g_overlayVisible = !g_overlayVisible;
                    if (g_overlayVisible) window.Show();
                    else window.Hide();

                    // Debounce - wait until key is released
                    while (GetAsyncKeyState(g_toggleOverlayBind.vkCode) & 0x8000) {
                        Sleep(1); // Small sleep to prevent CPU hogging
                    }
                }
            }

            // Check for exit keybind
            if (GetAsyncKeyState(g_exitBind.vkCode) & 0x8000) {
                if ((g_exitBind.ctrl == g_ctrlDown) &&
                    (g_exitBind.alt == g_altDown) &&
                    (g_exitBind.shift == g_shiftDown))
                {
                    running = false;
                    // Debounce the key press
                    while (GetAsyncKeyState(g_exitBind.vkCode) & 0x8000) {
                        Sleep(1);
                    }
                }
            }
        }

        // Handle rebinding logic with throttled polling
        auto now = std::chrono::steady_clock::now();
        if ((g_waitingForToggleBind || g_waitingForExitBind) &&
            (now - lastRebindScan > scanInterval))
        {
            lastRebindScan = now;

            // Update modifier states again
            UpdateModifierStates();

            // Scan for pressed keys
            for (int vk = 1; vk < 256; vk++) {
                if (GetAsyncKeyState(vk) & 0x8000) {
                    // Skip modifier keys
                    if (IsModifierKey(vk)) continue;

                    // Skip blacklisted keys
                    if (std::find(blacklistedKeys.begin(), blacklistedKeys.end(), vk) != blacklistedKeys.end())
                        continue;

                    // Create new keybind with current modifiers
                    Keybind newBind = { vk, g_ctrlDown, g_altDown, g_shiftDown };

                    // Handle conflict detection and assignment
                    if (g_waitingForToggleBind) {
                        if (!HasConflict(newBind, g_exitBind)) {
                            g_toggleOverlayBind = newBind;
                            g_waitingForToggleBind = false;
                        }
                    }
                    else if (g_waitingForExitBind) {
                        if (!HasConflict(newBind, g_toggleOverlayBind)) {
                            g_exitBind = newBind;
                            g_waitingForExitBind = false;
                        }
                    }

                    // Debounce the key press
                    while (GetAsyncKeyState(vk) & 0x8000) {
                        Sleep(1);
                    }
                    break;
                }
            }
        }

        // Renderer begin frame
        renderer.BeginFrame();

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // UI
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
        ImGui::Begin("Overlay Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        // Hotkey customization section
        ImGui::Text("Hotkeys:");
        ImGui::Separator();

        // Toggle Overlay rebinding
        std::string toggleBindStr = GetKeybindDisplayName(g_toggleOverlayBind);
        ImGui::Text("Toggle Overlay: %s", toggleBindStr.c_str());
        ImGui::SameLine();

        if (g_waitingForToggleBind) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Cancel##Toggle")) {
                g_waitingForToggleBind = false;
            }
            ImGui::PopStyleColor();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Press new key combination...");
        }
        else {
            if (ImGui::Button("Rebind Toggle")) {
                g_waitingForToggleBind = true;
                g_waitingForExitBind = false; // Cancel any other rebinding
            }
        }

        // Exit rebinding
        std::string exitBindStr = GetKeybindDisplayName(g_exitBind);
        ImGui::Text("Exit App: %s", exitBindStr.c_str());
        ImGui::SameLine();

        if (g_waitingForExitBind) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Cancel##Exit")) {
                g_waitingForExitBind = false;
            }
            ImGui::PopStyleColor();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Press new key combination...");
        }
        else {
            if (ImGui::Button("Rebind Exit")) {
                g_waitingForExitBind = true;
                g_waitingForToggleBind = false; // Cancel any other rebinding
            }
        }

        ImGui::Separator();
        ImGui::Text("Overlay is %s", g_overlayVisible ? "Visible" : "Hidden");

        // Stable FPS/frametime readout (EMA)
        const double fpsSmooth = g_fpsCounter.SmoothedFPS();
        const double msSmooth = g_fpsCounter.SmoothedMs();
        const double fpsInst = g_fpsCounter.InstantFPS();

        if (fpsSmooth > 0.0) {
            ImGui::Text("FPS: %.1f (inst %.1f) | Frame: %.2f ms", fpsSmooth, fpsInst, msSmooth);
        }
        else {
            // Until EMA initializes
            ImGui::Text("FPS: (warming up)...");
        }

        ImGui::Separator();
        ImGui::Text("Transparent overlay");

        // FPS limiter UI
        ImGui::Separator();
        ImGui::Text("Frame pacing");
        if (ImGui::SliderInt("FPS limit (0 = Unlimited)", &g_targetFps, 0, 1000)) {
            if (g_targetFps < 0) g_targetFps = 0;
        }
        if (g_targetFps == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(Unlimited)");
        }

        if (ImGui::Button("Exit")) running = false;
        ImGui::End();

        // Render UI
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), renderer.GetCommandList());

        renderer.EndFrame();

        // End frame pacing
        g_fpsLimiter.EndFrame(g_targetFps);
    }

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    renderer.Shutdown();
    window.Destroy();
    return 0;
}
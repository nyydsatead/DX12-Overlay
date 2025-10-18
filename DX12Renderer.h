#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <windows.h>

using Microsoft::WRL::ComPtr;

class DX12Renderer {
public:
    static const UINT FRAME_COUNT = 2;

    bool Initialize(HWND hwnd, bool transparentComposition = true);
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void Resize(UINT width, UINT height);
    void WaitForGPU();

    ID3D12Device* GetDevice() const { return m_device.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }
    ID3D12DescriptorHeap* GetSRVHeap() const { return m_srvHeap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const { return m_rtvHandles[m_frameIndex]; }
    DXGI_FORMAT GetBackBufferFormat() const { return m_backBufferFormat; }

private:
    bool InitForComposition(HWND hwnd, IDXGIFactory4* factory);
    bool InitForHwnd(HWND hwnd, IDXGIFactory4* factory);

    void CreateRenderTargets();
    void CleanupRenderTargets();

    // Core D3D12
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FRAME_COUNT];
    ComPtr<ID3D12Fence> m_fence;

    // Swap chain
    ComPtr<IDXGISwapChain3> m_swapChain;
    UINT m_swapChainFlags = 0;
    DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool m_useDComp = false;

    // DirectComposition
    ComPtr<IDCompositionDevice> m_dcompDevice;
    ComPtr<IDCompositionTarget> m_dcompTarget;
    ComPtr<IDCompositionVisual> m_dcompVisual;

    // Descriptors
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[FRAME_COUNT] = {};
    UINT m_rtvDescriptorSize = 0;

    // Back buffers
    ComPtr<ID3D12Resource> m_renderTargets[FRAME_COUNT];

    // Sync
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValue = 0;
    UINT64 m_fenceValues[FRAME_COUNT] = { 0 };
    UINT m_frameIndex = 0;

    // Waitable swap chain object for better pacing
    HANDLE m_frameLatencyWaitableObject = nullptr;

    // Cached window size
    UINT m_width = 0;
    UINT m_height = 0;
};
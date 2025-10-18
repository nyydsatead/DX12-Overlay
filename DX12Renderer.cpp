#include "DX12Renderer.h"
#include "DX12Utils.h"
#include <windows.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dcomp.lib")

using namespace DX12Utils;

bool DX12Renderer::Initialize(HWND hwnd, bool transparentComposition) {
    // Enable DirectX 12 debug layer in debug builds for better error reporting
#ifdef _DEBUG
    if (ComPtr<ID3D12Debug> debugController; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        DebugLog("D3D12 Debug Layer enabled");
    }
#endif

    // Create DXGI factory for enumerating adapters and creating swap chains
    UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { OutputHRError("CreateDXGIFactory2", hr); return false; }

    // Create D3D12 device on the default adapter with feature level 11.0
    // Feature level 11.0 provides good compatibility while supporting modern features
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) { OutputHRError("D3D12CreateDevice", hr); return false; }

    // Create direct command queue for submitting rendering commands to GPU
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr)) { OutputHRError("CreateCommandQueue", hr); return false; }

    // Get window client area dimensions for swap chain sizing
    RECT rect{};
    GetClientRect(hwnd, &rect);
    m_width = (rect.right > rect.left) ? (rect.right - rect.left) : 1920;
    m_height = (rect.bottom > rect.top) ? (rect.bottom - rect.top) : 1080;

    // Initialize appropriate swap chain type: DirectComposition for transparency or HWND for opaque
    m_useDComp = transparentComposition;
    bool ok = m_useDComp ? InitForComposition(hwnd, factory.Get()) : InitForHwnd(hwnd, factory.Get());
    if (!ok) return false;

    // Create descriptor heaps for render target views (RTV) and shader resource views (SRV)
    {
        // RTV heap for back buffer render targets (one per frame in flight)
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FRAME_COUNT;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
        if (FAILED(hr)) { OutputHRError("CreateDescriptorHeap(RTV)", hr); return false; }
        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // SRV heap for ImGui font textures and user textures (shader-visible)
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 64;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap));
        if (FAILED(hr)) { OutputHRError("CreateDescriptorHeap(SRV)", hr); return false; }
    }

    // Create command allocators - one per frame to enable lock-free recording
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]));
        if (FAILED(hr)) { OutputHRError("CreateCommandAllocator", hr); return false; }
    }
    
    // Create command list in closed state (will be reset in BeginFrame)
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList));
    if (FAILED(hr)) { OutputHRError("CreateCommandList", hr); return false; }
    m_commandList->Close();

    // Create fence and event for CPU-GPU synchronization
    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) { OutputHRError("CreateFence", hr); return false; }
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) { OutputDebugStringA("CreateEvent failed.\n"); return false; }

    // Create render target views for swap chain back buffers
    CreateRenderTargets();
    
    DebugLog("DX12Renderer initialized successfully");
    return true;
}

bool DX12Renderer::InitForComposition(HWND hwnd, IDXGIFactory4* factory) {
    // DirectComposition path: BGRA with premultiplied alpha for per-pixel transparency
    // This format is best supported by Windows Desktop Window Manager (DWM)
    m_backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    // Enable waitable swap chain for better frame pacing and reduced CPU overhead
    m_swapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    // Create composition swap chain descriptor
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = m_width;
    scDesc.Height = m_height;
    scDesc.Format = m_backBufferFormat;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = FRAME_COUNT;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; // KEY: enables per-pixel transparency
    scDesc.Flags = m_swapChainFlags;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = factory->CreateSwapChainForComposition(m_commandQueue.Get(), &scDesc, nullptr, &sc1);
    if (FAILED(hr)) { OutputHRError("CreateSwapChainForComposition", hr); return false; }

    hr = sc1.As(&m_swapChain);
    if (FAILED(hr)) { OutputHRError("swapChain.As", hr); return false; }

    // Reduce latency by setting max frame latency to 2-3 frames
    // This improves responsiveness while maintaining smooth presentation
    if (ComPtr<IDXGISwapChain2> sc2; SUCCEEDED(m_swapChain.As(&sc2)) && sc2) {
        sc2->SetMaximumFrameLatency(2);
        m_frameLatencyWaitableObject = sc2->GetFrameLatencyWaitableObject();
    }

    // Create DirectComposition device and visual tree
    // This composites the swap chain onto the desktop with transparency
    hr = DCompositionCreateDevice(nullptr, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(m_dcompDevice.GetAddressOf()));
    if (FAILED(hr)) { OutputHRError("DCompositionCreateDevice", hr); return false; }

    hr = m_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &m_dcompTarget);
    if (FAILED(hr)) { OutputHRError("IDCompositionDevice::CreateTargetForHwnd", hr); return false; }

    hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
    if (FAILED(hr)) { OutputHRError("IDCompositionDevice::CreateVisual", hr); return false; }

    hr = m_dcompVisual->SetContent(m_swapChain.Get());
    if (FAILED(hr)) { OutputHRError("IDCompositionVisual::SetContent", hr); return false; }

    hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
    if (FAILED(hr)) { OutputHRError("IDCompositionTarget::SetRoot", hr); return false; }

    // Commit the composition tree to make it visible
    hr = m_dcompDevice->Commit();
    if (FAILED(hr)) { OutputHRError("IDCompositionDevice::Commit", hr); return false; }

    return true;
}

bool DX12Renderer::InitForHwnd(HWND hwnd, IDXGIFactory4* factory) {
    // HWND path (opaque): R8G8B8A8 with tearing when supported
    m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    BOOL tearingSupported = FALSE;
    if (ComPtr<IDXGIFactory5> f5; SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&f5))) && f5) {
        if (FAILED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported)))) {
            tearingSupported = FALSE;
        }
    }

    // Enable frame latency waitable object; also allow tearing if available
    m_swapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (tearingSupported) m_swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = FRAME_COUNT;
    scDesc.Width = m_width;
    scDesc.Height = m_height;
    scDesc.Format = m_backBufferFormat;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scDesc.Flags = m_swapChainFlags;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1);
    if (FAILED(hr)) {
        // Fallback without tearing
        scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        hr = factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1);
        if (FAILED(hr)) { OutputHRError("CreateSwapChainForHwnd", hr); return false; }
        // Keep only waitable flag
        m_swapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    }

    hr = sc1.As(&m_swapChain);
    if (FAILED(hr)) { OutputHRError("swapChain.As", hr); return false; }

    // Reduce latency and obtain waitable object
    if (ComPtr<IDXGISwapChain2> sc2; SUCCEEDED(m_swapChain.As(&sc2)) && sc2) {
        sc2->SetMaximumFrameLatency(2);
        m_frameLatencyWaitableObject = sc2->GetFrameLatencyWaitableObject();
    }

    return true;
}

void DX12Renderer::CreateRenderTargets() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        if (FAILED(hr)) { 
            OutputHRError("IDXGISwapChain::GetBuffer", hr); 
            continue; 
        }
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        m_rtvHandles[i] = rtvHandle;
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
}

void DX12Renderer::CleanupRenderTargets() {
    // Ensure GPU finished with these back buffers
    WaitForGPU();
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        m_renderTargets[i].Reset();
        m_fenceValues[i] = 0;
    }
}

void DX12Renderer::BeginFrame() {
    // Wait for swap chain frame latency object (improves pacing and reduces CPU overhead)
    // This blocks until the GPU is ready to accept a new frame
    if (m_frameLatencyWaitableObject) {
        WaitForSingleObject(m_frameLatencyWaitableObject, 1000);
    }

    // Get current back buffer index from swap chain
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Wait if this frame's resources are still in use by GPU
    // This prevents overwriting command allocator/render target still being processed
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    // Reset command allocator and command list for recording new commands
    // Allocator can only be reset when GPU has finished executing all commands from it
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);

    // Transition back buffer from PRESENT state to RENDER_TARGET state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    // Clear render target: transparent (0,0,0,0) for DComp, opaque for HWND mode
    const float clearColorOpaque[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
    const float clearColorTransparent[4] = { 0.f, 0.f, 0.f, 0.f };
    const float* clear = m_useDComp ? clearColorTransparent : clearColorOpaque;

    m_commandList->ClearRenderTargetView(m_rtvHandles[m_frameIndex], clear, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &m_rtvHandles[m_frameIndex], FALSE, nullptr);

    // Bind SRV descriptor heap for ImGui texture access
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);
}

void DX12Renderer::EndFrame() {
    // Transition back buffer from RENDER_TARGET state back to PRESENT state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    // Close command list and submit to command queue for GPU execution
    m_commandList->Close();
    ID3D12CommandList* cmdLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, cmdLists);

    // Present the frame to the display
    // syncInterval=0 means immediate (no vsync), allows for higher frame rates
    UINT syncInterval = 0;
    UINT presentFlags = 0;
    
    // Enable tearing in HWND mode if supported (not applicable to DirectComposition)
    if (!m_useDComp && (m_swapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) && syncInterval == 0) {
        presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    HRESULT hr = m_swapChain->Present(syncInterval, presentFlags);
    if (FAILED(hr)) {
        OutputHRError("Present", hr);
    }

    // Note: DirectComposition Commit() not needed per-frame, only on topology changes

    // Signal fence for this frame to track when GPU completes processing
    // This allows CPU to know when it's safe to reuse this frame's resources
    m_fenceValue++;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
    m_fenceValues[m_frameIndex] = m_fenceValue;
}
void DX12Renderer::Resize(UINT width, UINT height) {
    if (width == 0 || height == 0) return;
    m_width = width;
    m_height = height;

    // Ensure GPU is idle before resizing
    WaitForGPU();

    CleanupRenderTargets();

    HRESULT hr = m_swapChain->ResizeBuffers(
        FRAME_COUNT,
        m_width, m_height,
        m_backBufferFormat,
        m_swapChainFlags
    );
    if (FAILED(hr)) {
        OutputHRError("ResizeBuffers", hr);
        return;
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateRenderTargets();

    if (m_useDComp && m_dcompDevice) {
        // Commit when topology changes (resize is a good time to keep things in sync)
        m_dcompDevice->Commit();
    }
}

void DX12Renderer::WaitForGPU() {
    // Signal and wait on the latest fence value
    m_fenceValue++;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DX12Renderer::Shutdown() {
    if (m_commandQueue && m_fence) {
        WaitForGPU();
    }
    CleanupRenderTargets();

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    // Release DComp first
    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();

    // Release D3D
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        m_commandAllocators[i].Reset();
        m_renderTargets[i].Reset();
    }
    m_commandList.Reset();
    m_rtvHeap.Reset();
    m_srvHeap.Reset();
    m_swapChain.Reset();
    m_fence.Reset();
    m_commandQueue.Reset();
    m_device.Reset();

    if (m_frameLatencyWaitableObject) {
        CloseHandle(m_frameLatencyWaitableObject);
        m_frameLatencyWaitableObject = nullptr;
    }
}
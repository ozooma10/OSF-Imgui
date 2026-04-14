#pragma once

#include "pch.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#ifdef ERROR
#undef ERROR
#endif

struct ImGui_ImplDX12_InitInfo;

class DebugOverlayService
{
public:
	static DebugOverlayService& GetSingleton();

	bool Install();
	void RegisterBuiltInPanels();

	[[nodiscard]] bool        IsHookInstalled() const;
	[[nodiscard]] bool        IsInitialized() const;
	[[nodiscard]] bool        IsVisible() const;
	[[nodiscard]] HWND        GetWindowHandle() const;
	[[nodiscard]] std::size_t GetBufferCount() const;
	[[nodiscard]] std::string GetLastError() const;

private:
	using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags);
	using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain* a_swapChain, UINT a_bufferCount, UINT a_width, UINT a_height, DXGI_FORMAT a_newFormat, UINT a_swapChainFlags);
	using CreateSwapChainFn = HRESULT(__stdcall*)(IDXGIFactory* a_factory, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_swapChain);
	using CreateSwapChainForHwndFn = HRESULT(__stdcall*)(IDXGIFactory2* a_factory, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fullscreenDesc, IDXGIOutput* a_restrictToOutput, IDXGISwapChain1** a_swapChain);

	struct FrameContext
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
		Microsoft::WRL::ComPtr<ID3D12Resource>         renderTarget;
		D3D12_CPU_DESCRIPTOR_HANDLE                    rtvHandle{};
	};

	DebugOverlayService() = default;
	~DebugOverlayService() = default;

	DebugOverlayService(const DebugOverlayService&) = delete;
	DebugOverlayService(DebugOverlayService&&) = delete;
	DebugOverlayService& operator=(const DebugOverlayService&) = delete;
	DebugOverlayService& operator=(DebugOverlayService&&) = delete;

	void SetLastError(std::string message);
	void ToggleVisibility();
	void CaptureSwapChain(IUnknown* a_deviceCandidate, IUnknown* a_swapChainCandidate, HWND a_hwnd);
	bool EnsureInitialized();
	bool CreateRenderTargets();
	void CleanupRenderTargets();
	void CleanupOverlayState();
	void RenderOverlay();
	void DrawRootWindow();
	void AttachWindow(HWND a_hwnd);
	void DetachWindow();

	static bool IsMouseMessage(UINT a_message);
	static bool IsKeyboardMessage(UINT a_message);
	static void AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo* a_info, D3D12_CPU_DESCRIPTOR_HANDLE* a_cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* a_gpuHandle);
	static void FreeSrvDescriptor(ImGui_ImplDX12_InitInfo* a_info, D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle);

	static HRESULT __stdcall HookCreateSwapChain(IDXGIFactory* a_factory, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_swapChain);
	static HRESULT __stdcall HookCreateSwapChainForHwnd(IDXGIFactory2* a_factory, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fullscreenDesc, IDXGIOutput* a_restrictToOutput, IDXGISwapChain1** a_swapChain);
	static HRESULT __stdcall HookPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags);
	static HRESULT __stdcall HookResizeBuffers(IDXGISwapChain* a_swapChain, UINT a_bufferCount, UINT a_width, UINT a_height, DXGI_FORMAT a_newFormat, UINT a_swapChainFlags);
	static LRESULT CALLBACK HookWndProc(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam);

	mutable std::mutex _lock;
	bool               _hooksInstalled{ false };
	bool               _builtInPanelsRegistered{ false };
	bool               _imguiInitialized{ false };
	bool               _overlayVisible{ false };
	bool               _showDemoWindow{ false };
	bool               _srvAllocated{ false };
	std::string        _lastError;

	HWND    _window{ nullptr };
	WNDPROC _originalWndProc{ nullptr };

	Microsoft::WRL::ComPtr<ID3D12CommandQueue>       _commandQueue;
	Microsoft::WRL::ComPtr<ID3D12Device>             _device;
	Microsoft::WRL::ComPtr<IDXGISwapChain3>          _swapChain;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>     _rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>     _srvHeap;
	std::vector<FrameContext>                        _frames;
	D3D12_CPU_DESCRIPTOR_HANDLE                      _fontSrvCpuHandle{};
	D3D12_GPU_DESCRIPTOR_HANDLE                      _fontSrvGpuHandle{};
	DXGI_FORMAT                                      _rtvFormat{ DXGI_FORMAT_UNKNOWN };
	UINT                                             _rtvDescriptorSize{ 0 };

	PresentFn             _originalPresent{ nullptr };
	ResizeBuffersFn       _originalResizeBuffers{ nullptr };
	CreateSwapChainFn     _originalCreateSwapChain{ nullptr };
	CreateSwapChainForHwndFn _originalCreateSwapChainForHwnd{ nullptr };
};

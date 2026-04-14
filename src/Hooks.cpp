#include "pch.h"

#include "Hooks.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#ifdef ERROR
#	undef ERROR
#endif

#include "REL/Relocation.h"

namespace Hooks
{
	namespace
	{
		constexpr std::size_t kCreateSwapChainForHwndVtblIndex = 15;

		using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
			IDXGIFactory2*,
			IUnknown*,
			HWND,
			const DXGI_SWAP_CHAIN_DESC1*,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
			IDXGIOutput*,
			IDXGISwapChain1**);

		std::mutex g_stateLock;

		CreateSwapChainForHwndFn g_originalCreateSwapChainForHwnd{ nullptr };
		ID3D12CommandQueue*      g_commandQueue{ nullptr };
		ID3D12Device*            g_device{ nullptr };
		IDXGISwapChain3*         g_swapChain{ nullptr };
		bool                     g_installed{ false };
		bool                     g_loggedValidation{ false };

		template <class T>
		void ReplaceCapturedPointer(T*& a_dst, T* a_src)
		{
			if (a_dst == a_src) {
				return;
			}

			if (a_dst) {
				a_dst->Release();
			}

			a_dst = a_src;
		}

		void CaptureDeviceFromQueue(ID3D12CommandQueue* a_queue)
		{
			if (!a_queue) {
				return;
			}

			ID3D12Device* device = nullptr;
			if (FAILED(a_queue->GetDevice(IID_PPV_ARGS(&device)))) {
				REX::WARN("Hooks: failed to query ID3D12Device from command queue");
				return;
			}

			std::scoped_lock lock(g_stateLock);
			ReplaceCapturedPointer(g_device, device);
		}

		void CaptureCommandQueue(IUnknown* a_device)
		{
			if (!a_device) {
				return;
			}

			ID3D12CommandQueue* queue = nullptr;
			if (FAILED(a_device->QueryInterface(IID_PPV_ARGS(&queue)))) {
				return;
			}

			{
				std::scoped_lock lock(g_stateLock);
				ReplaceCapturedPointer(g_commandQueue, queue);
			}

			CaptureDeviceFromQueue(queue);
		}

		void CaptureSwapChain(IDXGISwapChain1* a_swapChain)
		{
			if (!a_swapChain) {
				return;
			}

			IDXGISwapChain3* swapChain3 = nullptr;
			if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
				return;
			}

			std::scoped_lock lock(g_stateLock);
			ReplaceCapturedPointer(g_swapChain, swapChain3);
		}

		void ValidateCapture()
		{
			ID3D12CommandQueue* queue = nullptr;
			ID3D12Device*       device = nullptr;
			IDXGISwapChain3*    swapChain = nullptr;

			{
				std::scoped_lock lock(g_stateLock);
				if (g_loggedValidation || !g_commandQueue || !g_device || !g_swapChain) {
					return;
				}

				queue = g_commandQueue;
				device = g_device;
				swapChain = g_swapChain;

				queue->AddRef();
				device->AddRef();
				swapChain->AddRef();
			}

			bool queueOk = false;
			bool deviceOk = false;
			bool swapChainOk = false;
			bool swapChainDeviceMatches = false;

			D3D12_COMMAND_QUEUE_DESC queueDesc{};
			if (queue) {
				queueDesc = queue->GetDesc();
				queueOk = true;
			}

			UINT nodeCount = 0;
			if (device) {
				nodeCount = device->GetNodeCount();
				deviceOk = nodeCount > 0;
			}

			DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
			if (swapChain && SUCCEEDED(swapChain->GetDesc1(&swapChainDesc))) {
				swapChainOk = true;
			}

			ID3D12Device* swapChainDevice = nullptr;
			if (swapChain && SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&swapChainDevice)))) {
				swapChainDeviceMatches = (swapChainDevice == device);
				swapChainDevice->Release();
			}

			REX::INFO(
				"Hooks: validation queue={} type={} flags=0x{:X} device={} nodes={} swapchain={} buffers={} format={} currentBackBuffer={} sameDevice={}",
				queueOk,
				static_cast<std::uint32_t>(queueDesc.Type),
				static_cast<std::uint32_t>(queueDesc.Flags),
				deviceOk,
				nodeCount,
				swapChainOk,
				swapChainDesc.BufferCount,
				static_cast<std::uint32_t>(swapChainDesc.Format),
				swapChain ? swapChain->GetCurrentBackBufferIndex() : 0u,
				swapChainDeviceMatches);

			{
				std::scoped_lock lock(g_stateLock);
				g_loggedValidation = true;
			}

			swapChain->Release();
			device->Release();
			queue->Release();
		}

		HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd_Hook(
			IDXGIFactory2* a_factory,
			IUnknown* a_device,
			HWND a_wnd,
			const DXGI_SWAP_CHAIN_DESC1* a_desc,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fullscreenDesc,
			IDXGIOutput* a_restrictToOutput,
			IDXGISwapChain1** a_swapChain)
		{
			const auto original = g_originalCreateSwapChainForHwnd;
			if (!original) {
				return E_FAIL;
			}

			const auto hr = original(
				a_factory,
				a_device,
				a_wnd,
				a_desc,
				a_fullscreenDesc,
				a_restrictToOutput,
				a_swapChain);

			if (SUCCEEDED(hr)) {
				CaptureCommandQueue(a_device);
				if (a_swapChain && *a_swapChain) {
					CaptureSwapChain(*a_swapChain);
				}

				if (g_commandQueue && g_device) {
					REX::INFO("Hooks: captured D3D12 command queue and device from CreateSwapChainForHwnd");
				}

				ValidateCapture();
			}

			return hr;
		}
	}

	bool Install()
	{
		std::scoped_lock lock(g_stateLock);
		if (g_installed) {
			return true;
		}

		IDXGIFactory2* factory = nullptr;
		const auto hr = ::CreateDXGIFactory1(IID_PPV_ARGS(&factory));
		if (FAILED(hr) || !factory) {
			REX::ERROR("Hooks: CreateDXGIFactory1 failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(hr));
			return false;
		}

		auto** vtbl = *reinterpret_cast<void***>(factory);
		auto* current = reinterpret_cast<CreateSwapChainForHwndFn>(vtbl[kCreateSwapChainForHwndVtblIndex]);

		if (current != &CreateSwapChainForHwnd_Hook) {
			auto vtblRel = REL::Relocation<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(vtbl));
			g_originalCreateSwapChainForHwnd = reinterpret_cast<CreateSwapChainForHwndFn>(
				vtblRel.write_vfunc(kCreateSwapChainForHwndVtblIndex, &CreateSwapChainForHwnd_Hook));
		}

		factory->Release();
		g_installed = true;

		REX::INFO("Hooks: installed IDXGIFactory2::CreateSwapChainForHwnd hook");
		return true;
	}

	ID3D12CommandQueue* GetCommandQueue()
	{
		std::scoped_lock lock(g_stateLock);
		return g_commandQueue;
	}

	ID3D12Device* GetDevice()
	{
		std::scoped_lock lock(g_stateLock);
		return g_device;
	}

	IDXGISwapChain3* GetSwapChain()
	{
		std::scoped_lock lock(g_stateLock);
		return g_swapChain;
	}
}

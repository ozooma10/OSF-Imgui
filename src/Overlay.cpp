#include "Overlay.h"
#include "UIManager.h"

#include <cstdint>
#include <mutex>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#ifdef ERROR
#undef ERROR
#endif

#include "REX/REX.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace Overlay
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		struct DescriptorHeapAllocator
		{
			bool Create(ID3D12Device *a_device, ID3D12DescriptorHeap *a_heap)
			{
				if (!a_device || !a_heap)
				{
					return false;
				}

				const auto desc = a_heap->GetDesc();
				if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
					(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == 0)
				{
					return false;
				}

				std::scoped_lock lock(mutex);

				heap = a_heap;
				heapStartCpu = heap->GetCPUDescriptorHandleForHeapStart();
				heapStartGpu = heap->GetGPUDescriptorHandleForHeapStart();
				increment = a_device->GetDescriptorHandleIncrementSize(desc.Type);

				freeIndices.clear();
				freeIndices.reserve(desc.NumDescriptors);
				for (UINT i = 0; i < desc.NumDescriptors; ++i)
				{
					freeIndices.push_back(static_cast<int>(desc.NumDescriptors - i - 1));
				}

				return true;
			}

			bool Allocate(
				D3D12_CPU_DESCRIPTOR_HANDLE *a_outCpuHandle,
				D3D12_GPU_DESCRIPTOR_HANDLE *a_outGpuHandle)
			{
				if (!a_outCpuHandle || !a_outGpuHandle)
				{
					return false;
				}

				std::scoped_lock lock(mutex);

				if (!heap || freeIndices.empty())
				{
					return false;
				}

				const auto index = static_cast<UINT>(freeIndices.back());
				freeIndices.pop_back();

				a_outCpuHandle->ptr = heapStartCpu.ptr + static_cast<SIZE_T>(index) * increment;
				a_outGpuHandle->ptr = heapStartGpu.ptr + static_cast<UINT64>(index) * increment;
				return true;
			}

			void Free(
				D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
				D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle)
			{
				std::scoped_lock lock(mutex);

				if (!heap || increment == 0)
				{
					return;
				}

				const auto cpuOffset = a_cpuHandle.ptr - heapStartCpu.ptr;
				const auto gpuOffset = a_gpuHandle.ptr - heapStartGpu.ptr;
				const auto cpuIndex = static_cast<int>(cpuOffset / increment);
				const auto gpuIndex = static_cast<int>(gpuOffset / increment);

				if (cpuIndex >= 0 && cpuIndex == gpuIndex)
				{
					freeIndices.push_back(cpuIndex);
				}
			}

			std::mutex mutex;
			ComPtr<ID3D12DescriptorHeap> heap;
			D3D12_CPU_DESCRIPTOR_HANDLE heapStartCpu{};
			D3D12_GPU_DESCRIPTOR_HANDLE heapStartGpu{};
			UINT increment{0};
			std::vector<int> freeIndices;
		};

		struct RuntimeState
		{
			std::mutex mutex;
			bool initialized{false};
			ComPtr<IDXGISwapChain3> swapChain;
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12CommandQueue> commandQueue;
			ComPtr<ID3D12DescriptorHeap> srvHeap;
			HWND hwnd{nullptr};
			UINT framesInFlight{2};
			DXGI_FORMAT rtvFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
			DescriptorHeapAllocator srvAllocator;
			ComPtr<ID3D12DescriptorHeap> rtvHeap;
			UINT rtvDescriptorSize{0};
			std::vector<ComPtr<ID3D12Resource>> backBuffers;
			std::vector<ComPtr<ID3D12CommandAllocator>> commandAllocators;
			ComPtr<ID3D12GraphicsCommandList> commandList;
		};

		RuntimeState g_state;

		void AllocateSrvDescriptor(
			ImGui_ImplDX12_InitInfo *,
			D3D12_CPU_DESCRIPTOR_HANDLE *a_outCpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE *a_outGpuHandle)
		{
			if (!g_state.srvAllocator.Allocate(a_outCpuHandle, a_outGpuHandle))
			{
				REX::ERROR("Overlay: failed to allocate a shader-visible SRV descriptor for ImGui");
			}
		}

		void FreeSrvDescriptor(
			ImGui_ImplDX12_InitInfo *,
			D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle)
		{
			g_state.srvAllocator.Free(a_cpuHandle, a_gpuHandle);
		}

		bool CreateRenderTargets()
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(g_state.swapChain->GetDesc(&desc)))
			{
				REX::ERROR("Overlay: failed to read swap-chain desc for render targets");
				return false;
			}

			const UINT bufferCount = desc.BufferCount;

			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
			rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.NumDescriptors = bufferCount;
			rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			if (FAILED(g_state.device->CreateDescriptorHeap(
					&rtvHeapDesc, IID_PPV_ARGS(g_state.rtvHeap.ReleaseAndGetAddressOf()))))
			{
				REX::ERROR("Overlay: failed to create RTV descriptor heap");
				return false;
			}

			g_state.rtvDescriptorSize = g_state.device->GetDescriptorHandleIncrementSize(
				D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			g_state.backBuffers.resize(bufferCount);
			auto rtvHandle = g_state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT i = 0; i < bufferCount; ++i)
			{
				if (FAILED(g_state.swapChain->GetBuffer(
						i, IID_PPV_ARGS(g_state.backBuffers[i].ReleaseAndGetAddressOf()))))
				{
					REX::ERROR("Overlay: failed to get back buffer {}", i);
					return false;
				}
				g_state.device->CreateRenderTargetView(
					g_state.backBuffers[i].Get(), nullptr, rtvHandle);
				rtvHandle.ptr += g_state.rtvDescriptorSize;
			}

			g_state.commandAllocators.resize(bufferCount);
			for (UINT i = 0; i < bufferCount; ++i)
			{
				if (FAILED(g_state.device->CreateCommandAllocator(
						D3D12_COMMAND_LIST_TYPE_DIRECT,
						IID_PPV_ARGS(g_state.commandAllocators[i].ReleaseAndGetAddressOf()))))
				{
					REX::ERROR("Overlay: failed to create command allocator {}", i);
					return false;
				}
			}

			if (FAILED(g_state.device->CreateCommandList(
					0, D3D12_COMMAND_LIST_TYPE_DIRECT,
					g_state.commandAllocators[0].Get(), nullptr,
					IID_PPV_ARGS(g_state.commandList.ReleaseAndGetAddressOf()))))
			{
				REX::ERROR("Overlay: failed to create command list");
				return false;
			}
			g_state.commandList->Close();

			REX::INFO("Overlay: render targets created buffers={}", bufferCount);
			return true;
		}

		void DestroyRenderTargets()
		{
			g_state.commandList.Reset();
			g_state.commandAllocators.clear();
			g_state.backBuffers.clear();
			g_state.rtvHeap.Reset();
			g_state.rtvDescriptorSize = 0;
		}
	}

	bool InitializeFromSwapChain(IDXGISwapChain *a_swapChain, ID3D12CommandQueue *a_commandQueue)
	{
		if (!a_swapChain || !a_commandQueue)
		{
			return false;
		}

		std::scoped_lock lock(g_state.mutex);

		if (g_state.initialized)
		{
			return true;
		}

		ComPtr<IDXGISwapChain3> swapChain3;
		if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(swapChain3.GetAddressOf()))))
		{
			REX::ERROR("Overlay: failed to upgrade the swap chain to IDXGISwapChain3");
			return false;
		}

		ComPtr<ID3D12Device> device;
		if (FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(device.GetAddressOf()))))
		{
			REX::ERROR("Overlay: failed to retrieve ID3D12Device from the swap chain");
			return false;
		}

		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		if (FAILED(a_swapChain->GetDesc(&swapChainDesc)))
		{
			REX::ERROR("Overlay: failed to read the swap-chain description");
			return false;
		}

		if (!swapChainDesc.OutputWindow)
		{
			REX::ERROR("Overlay: swap-chain output window is null");
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 16;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		ComPtr<ID3D12DescriptorHeap> srvHeap;
		if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(srvHeap.GetAddressOf()))))
		{
			REX::ERROR("Overlay: failed to create the ImGui SRV descriptor heap");
			return false;
		}

		if (!g_state.srvAllocator.Create(device.Get(), srvHeap.Get()))
		{
			REX::ERROR("Overlay: failed to initialize the ImGui descriptor allocator");
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		if (!ImGui_ImplWin32_Init(swapChainDesc.OutputWindow))
		{
			REX::ERROR("Overlay: ImGui Win32 backend initialization failed");
			ImGui::DestroyContext();
			return false;
		}

		ImGui_ImplDX12_InitInfo initInfo{};
		initInfo.Device = device.Get();
		initInfo.CommandQueue = a_commandQueue;
		initInfo.NumFramesInFlight = static_cast<int>(std::max<UINT>(swapChainDesc.BufferCount, 2));
		initInfo.RTVFormat = swapChainDesc.BufferDesc.Format;
		initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
		initInfo.SrvDescriptorHeap = srvHeap.Get();
		initInfo.SrvDescriptorAllocFn = AllocateSrvDescriptor;
		initInfo.SrvDescriptorFreeFn = FreeSrvDescriptor;

		if (!ImGui_ImplDX12_Init(&initInfo))
		{
			REX::ERROR("Overlay: ImGui DX12 backend initialization failed");
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			return false;
		}

		g_state.swapChain = std::move(swapChain3);
		g_state.device = std::move(device);
		g_state.commandQueue = a_commandQueue;
		g_state.srvHeap = std::move(srvHeap);
		g_state.hwnd = swapChainDesc.OutputWindow;
		g_state.framesInFlight = std::max<UINT>(swapChainDesc.BufferCount, 2);
		g_state.rtvFormat = swapChainDesc.BufferDesc.Format;
		g_state.initialized = true;

		if (!CreateRenderTargets())
		{
			REX::ERROR("Overlay: render target creation failed; rendering will be disabled");
		}

		REX::INFO(
			"Overlay: ImGui initialized hwnd={:#x} buffers={} format={:#x}",
			reinterpret_cast<std::uintptr_t>(g_state.hwnd),
			g_state.framesInFlight,
			static_cast<std::uint32_t>(g_state.rtvFormat));

		return true;
	}

	bool IsInitialized()
	{
		std::scoped_lock lock(g_state.mutex);
		return g_state.initialized;
	}

	void RenderFrame()
	{
		std::scoped_lock lock(g_state.mutex);

		if (!g_state.initialized || g_state.backBuffers.empty())
		{
			return;
		}

		const UINT frameIndex = g_state.swapChain->GetCurrentBackBufferIndex();

		auto &cmdAllocator = g_state.commandAllocators[frameIndex];
		cmdAllocator->Reset();
		g_state.commandList->Reset(cmdAllocator.Get(), nullptr);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = g_state.backBuffers[frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_state.commandList->ResourceBarrier(1, &barrier);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
		rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * g_state.rtvDescriptorSize;
		g_state.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		ID3D12DescriptorHeap *heaps[] = {g_state.srvHeap.Get()};
		g_state.commandList->SetDescriptorHeaps(1, heaps);

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("OSF");
		ImGui::Text("ImGui is alive");
		ImGui::End();
		UIManager::DrawFrame();

		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_state.commandList.Get());

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		g_state.commandList->ResourceBarrier(1, &barrier);

		g_state.commandList->Close();

		ID3D12CommandList *cmdLists[] = {g_state.commandList.Get()};
		g_state.commandQueue->ExecuteCommandLists(1, cmdLists);
	}

	void ReleaseRenderTargets()
	{
		std::scoped_lock lock(g_state.mutex);
		DestroyRenderTargets();
	}

	void RebuildRenderTargets()
	{
		std::scoped_lock lock(g_state.mutex);

		if (!g_state.initialized)
		{
			return;
		}

		if (!CreateRenderTargets())
		{
			REX::ERROR("Overlay: failed to rebuild render targets after resize");
		}
	}
}

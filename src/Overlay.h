#pragma once

#include <d3d12.h>

struct IDXGISwapChain;
struct ID3D12CommandQueue;
struct ID3D12Device;

namespace RE
{
	class InputEvent;
}

namespace Overlay
{
	bool InitializeFromSwapChain(IDXGISwapChain* a_swapChain, ID3D12CommandQueue* a_commandQueue);
	bool IsInitialized();
	bool TryGetTextureLoaderContext(ID3D12Device** a_outDevice, ID3D12CommandQueue** a_outCommandQueue);
	bool AllocateShaderVisibleSrv(
		D3D12_CPU_DESCRIPTOR_HANDLE* a_outCpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* a_outGpuHandle);
	void FreeShaderVisibleSrv(
		D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle);
	void HandleRawInput(void* a_rawInputHandle);
	bool ConsumeInputQueue(const RE::InputEvent* a_queueHead);
	bool WantsInputCapture();
	void RenderFrame();
	void ReleaseRenderTargets();
	void RebuildRenderTargets();
}

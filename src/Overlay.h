#pragma once

struct IDXGISwapChain;
struct ID3D12CommandQueue;

namespace Overlay
{
	bool InitializeFromSwapChain(IDXGISwapChain* a_swapChain, ID3D12CommandQueue* a_commandQueue);
	bool IsInitialized();
}

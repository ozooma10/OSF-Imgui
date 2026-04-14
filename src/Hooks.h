#pragma once

struct ID3D12CommandQueue;
struct ID3D12Device;
struct IDXGISwapChain3;

namespace Hooks
{
	bool Install();

	// Returned pointers are borrowed references owned by the hook module.
	ID3D12CommandQueue* GetCommandQueue();
	ID3D12Device*       GetDevice();
	IDXGISwapChain3*    GetSwapChain();
}

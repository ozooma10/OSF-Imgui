#pragma once

#include "REL/Relocation.h"

// Forward declarations for D3D12/DXGI COM types.
// Consumers must include <d3d12.h>, <dxgi1_4.h> etc. for full definitions.
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Debug;
struct IDXGIFactory2;
struct IDXGIAdapter;
struct IDXGISwapChain;

namespace RE::BSGraphics
{
	struct DeviceProperties;
	struct GameSwapChainWrapper;

	namespace detail
	{
		// Internal traversal types for the command queue pointer chain.
		// Names are placeholders -- the actual Bethesda class names are unknown.
		// Access pattern: RendererRoot->queueOwnerA->queueOwnerB->commandQueue

		struct CommandQueueOwnerB
		{
			std::byte             pad00[0x60];    // 00
			ID3D12CommandQueue   *commandQueue;   // 60
		};
		static_assert(offsetof(CommandQueueOwnerB, commandQueue) == 0x60);

		struct CommandQueueOwnerA
		{
			std::byte             pad00[0x08];    // 00
			CommandQueueOwnerB   *queueOwnerB;    // 08
		};
		static_assert(offsetof(CommandQueueOwnerA, queueOwnerB) == 0x08);
	}

	// Global renderer root singleton.
	// Access: REL::ID(944397)
	struct RendererRoot
	{
		[[nodiscard]] static RendererRoot *GetSingleton()
		{
			static REL::Relocation<RendererRoot **> singleton{REL::ID(944397)};
			return *singleton;
		}

		// Traverses the pointer chain to retrieve the ID3D12CommandQueue.
		// Returns nullptr if any link in the chain is null.
		[[nodiscard]] ID3D12CommandQueue *GetCommandQueue() const
		{
			if (!queueOwnerA)
				return nullptr;
			auto *b = queueOwnerA->queueOwnerB;
			if (!b)
				return nullptr;
			return b->commandQueue;
		}

		// members
		std::byte                      pad000[0x28];        // 000
		detail::CommandQueueOwnerA    *queueOwnerA;         // 028 - traversal path to ID3D12CommandQueue
		DeviceProperties              *pDeviceProperties;   // 030 - Bethesda name: arDeviceProperties
		std::byte                      pad038[0x30];        // 038
		void                          *renderCmdContext;    // 068 - render command context; +0x30 -> command queue allocator
		std::byte                      pad070[0x50];        // 070
	};
	static_assert(offsetof(RendererRoot, queueOwnerA) == 0x28);
	static_assert(offsetof(RendererRoot, pDeviceProperties) == 0x30);
	static_assert(offsetof(RendererRoot, renderCmdContext) == 0x68);

	// Central D3D12/DXGI device properties struct.
	// Bethesda internal name: arDeviceProperties (from error strings).
	// Holds the DXGI factory, adapter, D3D12 device, GPU capabilities,
	// format support table, and display/adapter info.
	struct DeviceProperties
	{
		std::byte       pad000[0x18];               // 000
		void           *renderState;                // 018 - allocated 0x28-byte struct
		std::byte       pad020[0x110];              // 020
		void           *pAGSContext;                // 130 - AMD GPU Services context (nullptr on non-AMD)
		std::byte       pad138[0x08];               // 138
		std::int32_t    maxTextureDim;              // 140 - = 512
		std::int32_t    maxCubeMapDim;              // 144 - = 256
		std::int32_t    maxVolumeDim;               // 148 - = 256
		std::int32_t    maxAnisotropy;              // 14C - = 32
		std::int32_t    gpuFeatureField;            // 150
		std::int32_t    gpuFeatureField2;           // 154
		std::int32_t    shaderModel;                // 158
		std::byte       pad15C[0x04];               // 15C
		std::uint8_t    tessellationLevel;          // 160 - 0=none, 3=partial, 6=full
		std::uint8_t    gpuCapFlags;                // 161 - bitfield from adapter desc
		std::byte       pad162[0x06];               // 162
		std::uint64_t   dedicatedVideoMemory;       // 168
		std::uint64_t   sharedSystemMemory;         // 170
		char            gpuVendorName[64];          // 178
		char            gpuFeatureStr1[64];         // 1B8
		char            gpuFeatureStr2[64];         // 1F8
		char            gpuName[64];                // 238 - registered as 'gpu_name' condition var
		std::byte       pad278[0x02];               // 278
		std::uint8_t    formatSupportTable[0x16B];  // 27A - 0x79 formats * 3 bytes each
		std::byte       pad3E5[0x03];               // 3E5
		void           *adapterInfoBegin;           // 3E8 - BSTArray-like; entries are 0x48 bytes
		void           *adapterInfoEnd;             // 3F0
		std::byte       pad3F8[0x08];               // 3F8
		std::int32_t    selectedFeatureLevel;       // 400 - D3D_FEATURE_LEVEL (0xC000/C100/C200)
		std::byte       pad404[0x04];               // 404
		IDXGIFactory2  *pDXGIFactory;               // 408 - Bethesda name: arDeviceProperties.pDXGIFactory
		IDXGIAdapter   *pDxActiveGPU;               // 410 - Bethesda name: arDeviceProperties.pDxActiveGPU
		ID3D12Device   *pDxDevice;                  // 418 - Bethesda name: arDeviceProperties.pDxDevice
		std::byte       pad420[0x118];              // 420
		std::int32_t    gpuFieldA;                  // 538
		std::int32_t    gpuFieldB;                  // 53C
		std::byte       pad540[0x30];               // 540
		ID3D12Debug    *pDebugInterface;            // 570 - ID3D12Debug3* (optional, debug layer)
		ID3D12Debug    *pDebugFallback;             // 578 - ID3D12Debug* fallback
		void           *pDeviceQI_A;                // 580 - QI'd interface from ID3D12Device
		void           *pDeviceQI_B;                // 588 - fallback QI from ID3D12Device
	};
	static_assert(offsetof(DeviceProperties, renderState) == 0x18);
	static_assert(offsetof(DeviceProperties, pAGSContext) == 0x130);
	static_assert(offsetof(DeviceProperties, maxTextureDim) == 0x140);
	static_assert(offsetof(DeviceProperties, maxCubeMapDim) == 0x144);
	static_assert(offsetof(DeviceProperties, maxVolumeDim) == 0x148);
	static_assert(offsetof(DeviceProperties, maxAnisotropy) == 0x14C);
	static_assert(offsetof(DeviceProperties, gpuFeatureField) == 0x150);
	static_assert(offsetof(DeviceProperties, gpuFeatureField2) == 0x154);
	static_assert(offsetof(DeviceProperties, shaderModel) == 0x158);
	static_assert(offsetof(DeviceProperties, tessellationLevel) == 0x160);
	static_assert(offsetof(DeviceProperties, gpuCapFlags) == 0x161);
	static_assert(offsetof(DeviceProperties, dedicatedVideoMemory) == 0x168);
	static_assert(offsetof(DeviceProperties, sharedSystemMemory) == 0x170);
	static_assert(offsetof(DeviceProperties, gpuVendorName) == 0x178);
	static_assert(offsetof(DeviceProperties, gpuFeatureStr1) == 0x1B8);
	static_assert(offsetof(DeviceProperties, gpuFeatureStr2) == 0x1F8);
	static_assert(offsetof(DeviceProperties, gpuName) == 0x238);
	static_assert(offsetof(DeviceProperties, formatSupportTable) == 0x27A);
	static_assert(offsetof(DeviceProperties, adapterInfoBegin) == 0x3E8);
	static_assert(offsetof(DeviceProperties, adapterInfoEnd) == 0x3F0);
	static_assert(offsetof(DeviceProperties, selectedFeatureLevel) == 0x400);
	static_assert(offsetof(DeviceProperties, pDXGIFactory) == 0x408);
	static_assert(offsetof(DeviceProperties, pDxActiveGPU) == 0x410);
	static_assert(offsetof(DeviceProperties, pDxDevice) == 0x418);
	static_assert(offsetof(DeviceProperties, gpuFieldA) == 0x538);
	static_assert(offsetof(DeviceProperties, gpuFieldB) == 0x53C);
	static_assert(offsetof(DeviceProperties, pDebugInterface) == 0x570);
	static_assert(offsetof(DeviceProperties, pDebugFallback) == 0x578);
	static_assert(offsetof(DeviceProperties, pDeviceQI_A) == 0x580);
	static_assert(offsetof(DeviceProperties, pDeviceQI_B) == 0x588);

	// Swap chain wrapper around IDXGISwapChain3/4.
	// Bethesda internal name: pSwapChain (from error strings).
	// Allocated as 0xD8 bytes in Renderer_CreateSwapChainForHwnd.
	struct GameSwapChainWrapper
	{
		void           *hwnd;                       // 000 - HWND for this swap chain's window
		std::uint32_t   configA;                    // 008
		std::byte       pad00C[0x04];               // 00C
		std::uint32_t   bufferCount;                // 010
		std::uint8_t    sampleCount;                // 014 - MSAA sample count; clamped to 1
		std::byte       pad015[0x0F];               // 015
		std::uint8_t    stereoEnabled;              // 024
		std::byte       pad025[0x03];               // 025
		std::uint32_t   swapEffect;                 // 028 - DXGI_SWAP_EFFECT; init = FLIP_SEQUENTIAL (3)
		std::byte       pad02C[0x04];               // 02C
		DeviceProperties *pDeviceProperties;        // 030 - back-pointer to device properties
		std::byte       pad038[0x08];               // 038
		IDXGISwapChain *pDxSwapChain;               // 040 - Bethesda name: pSwapChain->pDxSwapChain (IDXGISwapChain3/4)
		void           *frameLatencyHandle;         // 048 - HANDLE from GetFrameLatencyWaitableObject
		std::uint32_t   presentFlags;               // 050
		std::uint32_t   flags;                      // 054
		std::uint32_t   backBufferArrayCount;       // 058
		std::byte       pad05C[0x04];               // 05C
		std::uint32_t   backBufferArrayCapacity;    // 060 - 0x80000000 = use inline storage
		std::byte       pad064[0x44];               // 064
		std::uint32_t   renderTargetArrayCount;     // 0A8
		std::byte       pad0AC[0x04];               // 0AC
		std::uint32_t   renderTargetArrayCapacity;  // 0B0 - 0x80000000 = use inline storage
		std::byte       pad0B4[0x24];               // 0B4
	};
	static_assert(sizeof(GameSwapChainWrapper) == 0xD8);
	static_assert(offsetof(GameSwapChainWrapper, hwnd) == 0x00);
	static_assert(offsetof(GameSwapChainWrapper, bufferCount) == 0x10);
	static_assert(offsetof(GameSwapChainWrapper, sampleCount) == 0x14);
	static_assert(offsetof(GameSwapChainWrapper, stereoEnabled) == 0x24);
	static_assert(offsetof(GameSwapChainWrapper, swapEffect) == 0x28);
	static_assert(offsetof(GameSwapChainWrapper, pDeviceProperties) == 0x30);
	static_assert(offsetof(GameSwapChainWrapper, pDxSwapChain) == 0x40);
	static_assert(offsetof(GameSwapChainWrapper, frameLatencyHandle) == 0x48);
	static_assert(offsetof(GameSwapChainWrapper, presentFlags) == 0x50);
	static_assert(offsetof(GameSwapChainWrapper, flags) == 0x54);
	static_assert(offsetof(GameSwapChainWrapper, backBufferArrayCount) == 0x58);
	static_assert(offsetof(GameSwapChainWrapper, backBufferArrayCapacity) == 0x60);
	static_assert(offsetof(GameSwapChainWrapper, renderTargetArrayCount) == 0xA8);
	static_assert(offsetof(GameSwapChainWrapper, renderTargetArrayCapacity) == 0xB0);

} // namespace RE::BSGraphics

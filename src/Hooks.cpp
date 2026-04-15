#include "Hooks.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#include <dxgi.h>

#ifdef ERROR
#undef ERROR
#endif

#include "Overlay.h"
#include "RE/BSGraphics.h"
#include "REL/Relocation.h"
#include "SFSE/API.h"

bool Hooks::Install()
{
	return SwapChainWrapperHook::install();
}

bool Hooks::SwapChainWrapperHook::install()
{
	auto &trampoline = REL::GetTrampoline();

	// 0x145F375B8
	REL::Relocation<std::uintptr_t> callsite{REL::ID(141996), 0x130};
	originalFunction = trampoline.write_call<5>(callsite.address(), thunk);
	if (!originalFunction)
	{
		REX::ERROR(
			"SwapChainWrapperHook: failed to install hook at {:#x}",
			callsite.address());
		return false;
	}

	REX::INFO(
		"SwapChainWrapperHook: installed callsite hook at {:#x}",
		callsite.address());
	return true;
}

std::int64_t Hooks::SwapChainWrapperHook::thunk(void *a_context,
												void *a_descOrState,
												void *a_swapChainState)
{
	if (!originalFunction)
	{
		REX::ERROR("SwapChainWrapperHook: original function pointer is null");
		return 0;
	}

	const auto result = originalFunction(a_context, a_descOrState, a_swapChainState);

	struct BootstrapState
	{
		RE::BSGraphics::GameSwapChainWrapper *swapChainWrapper;
	};

	static std::atomic_bool loggedMissingData{false};

	if (Overlay::IsInitialized())
	{
		return 0;
	}

	const auto *state = static_cast<const BootstrapState *>(a_swapChainState);
	const auto *wrapper = state ? state->swapChainWrapper : nullptr;
	IDXGISwapChain *swapChain = wrapper ? wrapper->pDxSwapChain : nullptr;

	const auto *rendererRoot = RE::BSGraphics::RendererRoot::GetSingleton();
	ID3D12CommandQueue *commandQueue = rendererRoot ? rendererRoot->GetCommandQueue() : nullptr;

	if (swapChain && commandQueue)
	{
		if (Overlay::InitializeFromSwapChain(swapChain, commandQueue))
		{
			// InstallDXGIHooks(swapChain);
		}
	}

	if (!loggedMissingData.exchange(true, std::memory_order_relaxed))
	{
		REX::WARN(
			"SwapChainWrapperHook: bootstrap is missing data swapChain={:#x} commandQueue={:#x}",
			reinterpret_cast<std::uintptr_t>(swapChain),
			reinterpret_cast<std::uintptr_t>(commandQueue));
	}

	return result;
}

// namespace Hooks
// {
// 	namespace
// 	{
// 		constexpr UINT kVtablePresent = 8;
// 		constexpr UINT kVtableResizeBuffers = 13;

// 		using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
// 		using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// 		PresentFn g_originalPresent{ nullptr };
// 		ResizeBuffersFn g_originalResizeBuffers{ nullptr };

// 		HRESULT STDMETHODCALLTYPE HookedPresent(
// 			IDXGISwapChain* a_self, UINT a_syncInterval, UINT a_flags)
// 		{
// 			Overlay::RenderFrame();
// 			return g_originalPresent(a_self, a_syncInterval, a_flags);
// 		}

// 		HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
// 			IDXGISwapChain* a_self, UINT a_bufferCount, UINT a_width, UINT a_height,
// 			DXGI_FORMAT a_newFormat, UINT a_flags)
// 		{
// 			Overlay::ReleaseRenderTargets();
// 			const auto hr = g_originalResizeBuffers(
// 				a_self, a_bufferCount, a_width, a_height, a_newFormat, a_flags);
// 			if (SUCCEEDED(hr)) {
// 				Overlay::RebuildRenderTargets();
// 			}
// 			return hr;
// 		}

// 		bool PatchVtableEntry(void** a_vtable, UINT a_index, void* a_hook, void** a_outOriginal)
// 		{
// 			DWORD oldProtect;
// 			if (!VirtualProtect(
// 					&a_vtable[a_index], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
// 				return false;
// 			}
// 			*a_outOriginal = a_vtable[a_index];
// 			a_vtable[a_index] = a_hook;
// 			VirtualProtect(&a_vtable[a_index], sizeof(void*), oldProtect, &oldProtect);
// 			return true;
// 		}

// 		bool InstallDXGIHooks(IDXGISwapChain* a_swapChain)
// 		{
// 			if (!a_swapChain) {
// 				return false;
// 			}

// 			auto** vtable = *reinterpret_cast<void***>(a_swapChain);

// 			if (!PatchVtableEntry(vtable, kVtablePresent,
// 					reinterpret_cast<void*>(HookedPresent),
// 					reinterpret_cast<void**>(&g_originalPresent))) {
// 				REX::ERROR("Hooks: failed to patch Present vtable entry");
// 				return false;
// 			}

// 			if (!PatchVtableEntry(vtable, kVtableResizeBuffers,
// 					reinterpret_cast<void*>(HookedResizeBuffers),
// 					reinterpret_cast<void**>(&g_originalResizeBuffers))) {
// 				REX::ERROR("Hooks: failed to patch ResizeBuffers vtable entry");
// 				return false;
// 			}

// 			REX::INFO("Hooks: Present and ResizeBuffers vtable hooks installed");
// 			return true;
// 		}

// }

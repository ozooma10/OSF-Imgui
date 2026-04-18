#include "Hooks.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

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
	const auto framePresentInstalled = FramePresentHook::install();
	const auto swapChainInstalled = SwapChainWrapperHook::install();
	return framePresentInstalled && swapChainInstalled;
}

// ── Engine end-of-frame wrapper callsite hook ──────────────────

void Hooks::FramePresentHook::thunk(void *a_presentBatch)
{
	if (Overlay::IsInitialized())
	{
		Overlay::RenderFrame();
	}

	originalFunction(a_presentBatch);
}

bool Hooks::FramePresentHook::install()
{
	auto &trampoline = REL::GetTrampoline();
	REL::Relocation<std::uintptr_t> callsite{REL::ID(142984), 0xD};
	originalFunction = trampoline.write_call<5>(callsite.address(), thunk);
	if (!originalFunction)
	{
		REX::ERROR(
			"FramePresentHook: failed to install direct call hook at {:#x}",
			callsite.address());
		return false;
	}

	REX::INFO(
		"FramePresentHook: installed direct call hook at {:#x}, original target {:#x}",
		callsite.address(),
		originalFunction.address());
	return true;
}

// ── Swap chain init hook (captures the swap chain pointer) ─────

bool Hooks::SwapChainWrapperHook::install()
{
	auto &trampoline = REL::GetTrampoline();

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

	if (Overlay::IsInitialized())
	{
		return result;
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
			REX::INFO(
				"SwapChainWrapperHook: initialized overlay from swap chain {:#x} and command queue {:#x}",
				reinterpret_cast<std::uintptr_t>(swapChain),
				reinterpret_cast<std::uintptr_t>(commandQueue));
		}
		else
		{
			REX::ERROR(
				"SwapChainWrapperHook: failed to initialize overlay from swap chain {:#x} and command queue {:#x}",
				reinterpret_cast<std::uintptr_t>(swapChain),
				reinterpret_cast<std::uintptr_t>(commandQueue));
		}
		return result;
	}

	static std::atomic_bool loggedMissingData{false};
	if (!loggedMissingData.exchange(true, std::memory_order_relaxed))
	{
		REX::WARN(
			"SwapChainWrapperHook: missing data swapChain={:#x} commandQueue={:#x}",
			reinterpret_cast<std::uintptr_t>(swapChain),
			reinterpret_cast<std::uintptr_t>(commandQueue));
	}

	return result;
}

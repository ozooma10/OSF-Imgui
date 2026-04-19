#include "Hooks.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <dxgi.h>

#ifdef ERROR
#undef ERROR
#endif

#include "Overlay.h"
#include "RE/BSGraphics.h"
#include "REL/Relocation.h"
#include "SFSE/API.h"

namespace
{
	constexpr std::uintptr_t kProcessRawInputCallSiteOffset = 0x01883A49;
	constexpr std::uintptr_t kExpectedProcessRawInputTargetOffset = 0x022D9FC0;
	constexpr std::array<std::uint8_t, 5> kExpectedProcessRawInputCallBytes{
		0xE8, 0x72, 0x65, 0xA5, 0x00};

	constexpr std::uintptr_t kUITranslatedInputGateOffset = 0x02543DA0;
}

bool Hooks::Install()
{
	const auto framePresentInstalled = FramePresentHook::install();
	const auto swapChainInstalled = SwapChainWrapperHook::install();
	const auto rawInputInstalled = RawInputQueueHook::install();
	// const auto translatedGateInstalled = UITranslatedInputHook::install();
	return framePresentInstalled && swapChainInstalled && rawInputInstalled; // && translatedGateInstalled;
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

// ── Raw input queue hook (primary ImGui input source) ─────────

std::uintptr_t Hooks::RawInputQueueHook::thunk(void *a_context,
											   void *a_rawInputHandle,
											   void *a_state,
											   void *a_rawInputHandleMirror)
{
	Overlay::HandleRawInput(a_rawInputHandle);
	return originalFunction(a_context, a_rawInputHandle, a_state, a_rawInputHandleMirror);
}

bool Hooks::RawInputQueueHook::install()
{
	auto &trampoline = REL::GetTrampoline();
	REL::Relocation<std::uintptr_t> callsite{REL::Offset(kProcessRawInputCallSiteOffset)};
	const auto siteAddress = callsite.address();
	const auto *siteBytes = reinterpret_cast<const std::uint8_t *>(siteAddress);
	if (std::memcmp(siteBytes, kExpectedProcessRawInputCallBytes.data(), kExpectedProcessRawInputCallBytes.size()) != 0)
	{
		REX::ERROR(
			"RawInputQueueHook: callsite bytes drifted at {:#x}: {:02X} {:02X} {:02X} {:02X} {:02X}",
			siteAddress,
			siteBytes[0],
			siteBytes[1],
			siteBytes[2],
			siteBytes[3],
			siteBytes[4]);
		return false;
	}

	originalFunction = trampoline.write_call<5>(siteAddress, thunk);
	if (!originalFunction)
	{
		REX::ERROR(
			"RawInputQueueHook: failed to install direct call hook at {:#x}",
			siteAddress);
		return false;
	}

	const auto moduleBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleA("Starfield.exe"));
	const auto originalRva = moduleBase && originalFunction.address() >= moduleBase ? originalFunction.address() - moduleBase : 0;
	REX::INFO(
		"RawInputQueueHook: installed direct call hook at {:#x}, original target {:#x} (RVA {:#x}, expected {:#x})",
		siteAddress,
		originalFunction.address(),
		originalRva,
		kExpectedProcessRawInputTargetOffset);
	return true;
}

// ── UI translated-input gate hook (0x142543DA0) ──────────────

void Hooks::UITranslatedInputHook::thunk(void *a_receiver, const RE::InputEvent *a_queueHead)
{
	if (Overlay::WantsInputCapture())
	{
		return;
	}

	originalFunction(a_receiver, a_queueHead);
}

bool Hooks::UITranslatedInputHook::install()
{
	auto &trampoline = REL::GetTrampoline();
	REL::Relocation<std::uintptr_t> target{REL::Offset(kUITranslatedInputGateOffset)};
	originalFunction = trampoline.write_jmp<5>(target.address(), thunk);
	if (!originalFunction)
	{
		REX::ERROR(
			"UITranslatedInputHook: failed to install function hook at {:#x}",
			target.address());
		return false;
	}

	REX::INFO(
		"UITranslatedInputHook: installed suppression gate hook at {:#x}",
		target.address());
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

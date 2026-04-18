#include "Hooks.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <d3d12.h>
#include <dxgi.h>
#include <xbyak/xbyak.h>

#ifdef ERROR
#undef ERROR
#endif

#include "Overlay.h"
#include "RE/BSGraphics.h"
#include "REL/Relocation.h"
#include "SFSE/API.h"

namespace
{
	constexpr REL::Offset kFramePresentCallsiteOffset{0x02A131E0};
	constexpr std::array<std::uint8_t, 6> kFramePresentExpectedBytes{
		0x48, 0x8B, 0x01, 0xFF, 0x50, 0x40
	};

	[[nodiscard]] bool ValidateExpectedBytes(std::uintptr_t a_address, std::span<const std::uint8_t> a_expected)
	{
		return std::memcmp(reinterpret_cast<const void*>(a_address), a_expected.data(), a_expected.size()) == 0;
	}

	struct FramePresentDetourCode final : Xbyak::CodeGenerator
	{
		FramePresentDetourCode(
			std::uintptr_t a_helperAddress,
			std::uintptr_t a_returnAddress)
		{
			// Reserve shadow space and preserve the original Present arguments
			// before calling back into C++.
			sub(rsp, 0x40);
			mov(ptr[rsp + 0x20], rcx);
			mov(ptr[rsp + 0x28], rdx);
			mov(ptr[rsp + 0x30], r8);

			mov(rax, a_helperAddress);
			call(rax);

			// Restore the COM call arguments, then replay the displaced
			// instructions we overwrote at the engine callsite.
			mov(rcx, ptr[rsp + 0x20]);
			mov(rdx, ptr[rsp + 0x28]);
			mov(r8, ptr[rsp + 0x30]);
			add(rsp, 0x40);

			mov(rax, ptr[rcx]);
			call(ptr[rax + 0x40]);

			// Continue immediately after the original 6-byte mov/call pair.
			mov(r10, a_returnAddress);
			jmp(r10);
		}
	};

	void STDMETHODCALLTYPE OnFramePresent(
		IDXGISwapChain *a_swapChain,
		UINT,
		UINT)
	{
		if (!a_swapChain)
		{
			return;
		}

		if (Overlay::IsInitialized())
		{
			Overlay::RenderFrame();
		}
	}
}

// ── DXGI vtable hooks (ResizeBuffers only) ─────────────────────

namespace Hooks::DXGIHooks
{
	namespace
	{
		constexpr UINT kVtableResizeBuffers = 13;

		using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		ResizeBuffersFn g_originalResizeBuffers{nullptr};

		HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
			IDXGISwapChain *a_self, UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_newFormat, UINT a_flags)
		{
			Overlay::ReleaseRenderTargets();
			const auto hr = g_originalResizeBuffers(
				a_self, a_bufferCount, a_width, a_height, a_newFormat, a_flags);
			if (SUCCEEDED(hr))
			{
				Overlay::RebuildRenderTargets();
			}
			return hr;
		}

		bool PatchVtableEntry(void **a_vtable, UINT a_index, void *a_hook, void **a_outOriginal)
		{
			DWORD oldProtect;
			if (!VirtualProtect(
					&a_vtable[a_index], sizeof(void *), PAGE_READWRITE, &oldProtect))
			{
				return false;
			}
			*a_outOriginal = a_vtable[a_index];
			a_vtable[a_index] = a_hook;
			VirtualProtect(&a_vtable[a_index], sizeof(void *), oldProtect, &oldProtect);
			return true;
		}
	}

	bool Install(IDXGISwapChain *a_swapChain)
	{
		if (!a_swapChain)
		{
			return false;
		}

		auto **vtable = *reinterpret_cast<void ***>(a_swapChain);

		if (!PatchVtableEntry(vtable, kVtableResizeBuffers,
							  reinterpret_cast<void *>(HookedResizeBuffers),
							  reinterpret_cast<void **>(&g_originalResizeBuffers)))
		{
			REX::ERROR("DXGIHooks: failed to patch ResizeBuffers vtable entry");
			return false;
		}

		REX::INFO("DXGIHooks: ResizeBuffers vtable hook installed");
		return true;
	}
}

// ── Engine end-of-frame Present callsite hook ──────────────────

bool Hooks::FramePresentHook::install()
{
	auto &trampoline = REL::GetTrampoline();
	REL::Relocation<std::uintptr_t> callsite{kFramePresentCallsiteOffset};

	if (!ValidateExpectedBytes(callsite.address(), kFramePresentExpectedBytes))
	{
		REX::ERROR(
			"FramePresentHook: unexpected bytes at {:#x}; aborting inline hook",
			callsite.address());
		return false;
	}

	FramePresentDetourCode stubCode{
		REX::UNRESTRICTED_CAST<std::uintptr_t>(&OnFramePresent),
		callsite.address() + kFramePresentExpectedBytes.size()
	};
	auto *stub = trampoline.allocate(stubCode);

	trampoline.write_jmp<6>(callsite.address(), reinterpret_cast<std::uintptr_t>(stub));

	REX::INFO(
		"FramePresentHook: installed inline hook at {:#x}, return {:#x}, stub {:#x}",
		callsite.address(),
		callsite.address() + kFramePresentExpectedBytes.size(),
		reinterpret_cast<std::uintptr_t>(stub));
	return true;
}

// ── Swap chain init hook (captures the swap chain pointer) ─────

bool Hooks::Install()
{
	const auto framePresentInstalled = FramePresentHook::install();
	const auto swapChainInstalled = SwapChainWrapperHook::install();
	return framePresentInstalled && swapChainInstalled;
}

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
			if (!DXGIHooks::Install(swapChain))
			{
				REX::WARN("SwapChainWrapperHook: failed to install DXGI hooks");
			}
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

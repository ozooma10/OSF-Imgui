#include "Hooks.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#include <dxgi.h>

#ifdef ERROR
#undef ERROR
#endif

#include "Overlay.h"
#include "REL/Relocation.h"
#include "SFSE/API.h"

namespace Hooks
{
	namespace
	{
		constexpr UINT kVtablePresent = 8;
		constexpr UINT kVtableResizeBuffers = 13;

		using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
		using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		PresentFn g_originalPresent{ nullptr };
		ResizeBuffersFn g_originalResizeBuffers{ nullptr };

		HRESULT STDMETHODCALLTYPE HookedPresent(
			IDXGISwapChain* a_self, UINT a_syncInterval, UINT a_flags)
		{
			Overlay::RenderFrame();
			return g_originalPresent(a_self, a_syncInterval, a_flags);
		}

		HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
			IDXGISwapChain* a_self, UINT a_bufferCount, UINT a_width, UINT a_height,
			DXGI_FORMAT a_newFormat, UINT a_flags)
		{
			Overlay::ReleaseRenderTargets();
			const auto hr = g_originalResizeBuffers(
				a_self, a_bufferCount, a_width, a_height, a_newFormat, a_flags);
			if (SUCCEEDED(hr)) {
				Overlay::RebuildRenderTargets();
			}
			return hr;
		}

		bool PatchVtableEntry(void** a_vtable, UINT a_index, void* a_hook, void** a_outOriginal)
		{
			DWORD oldProtect;
			if (!VirtualProtect(
					&a_vtable[a_index], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
				return false;
			}
			*a_outOriginal = a_vtable[a_index];
			a_vtable[a_index] = a_hook;
			VirtualProtect(&a_vtable[a_index], sizeof(void*), oldProtect, &oldProtect);
			return true;
		}

		bool InstallDXGIHooks(IDXGISwapChain* a_swapChain)
		{
			if (!a_swapChain) {
				return false;
			}

			auto** vtable = *reinterpret_cast<void***>(a_swapChain);

			if (!PatchVtableEntry(vtable, kVtablePresent,
					reinterpret_cast<void*>(HookedPresent),
					reinterpret_cast<void**>(&g_originalPresent))) {
				REX::ERROR("Hooks: failed to patch Present vtable entry");
				return false;
			}

			if (!PatchVtableEntry(vtable, kVtableResizeBuffers,
					reinterpret_cast<void*>(HookedResizeBuffers),
					reinterpret_cast<void**>(&g_originalResizeBuffers))) {
				REX::ERROR("Hooks: failed to patch ResizeBuffers vtable entry");
				return false;
			}

			REX::INFO("Hooks: Present and ResizeBuffers vtable hooks installed");
			return true;
		}

		void TryBootstrapImGui(void* a_context, void* a_descOrState, void* a_swapChainState)
		{
			struct BootstrapState
			{
				void* swapChainWrapper;
			};

			struct GameSwapChainWrapper
			{
				// Validated from sub_142A34A90: the native DXGI swapchain lives at +0x40.
				std::byte pad0[0x40];
				IDXGISwapChain* dxgiSwapChain;
			};

			struct RendererRoot
			{
				// Validated from the CreateSwapChainForHwnd setup path:
				// root + 0x28 -> queueOwnerA -> +0x08 -> queueOwnerB -> +0x60 -> ID3D12CommandQueue*
				std::byte pad0[0x28];
				void* queueOwnerA;
			};

			struct QueueOwnerA
			{
				std::byte pad0[0x08];
				void* queueOwnerB;
			};

			struct QueueOwnerB
			{
				std::byte pad0[0x60];
				ID3D12CommandQueue* commandQueue;
			};

			static std::atomic_bool loggedMissingData{ false };

			if (Overlay::IsInitialized()) {
				return;
			}

			IDXGISwapChain* swapChain = nullptr;
			ID3D12CommandQueue* commandQueue = nullptr;

			(void)a_context;
			(void)a_descOrState;

			const auto* state = static_cast<const BootstrapState*>(a_swapChainState);
			const auto* wrapper = state ? static_cast<const GameSwapChainWrapper*>(state->swapChainWrapper) : nullptr;
			swapChain = wrapper ? wrapper->dxgiSwapChain : nullptr;

			REL::Relocation<std::uintptr_t> rendererRootAddr{ REL::ID(944397) };
			const auto* rendererRoot = *reinterpret_cast<RendererRoot* const*>(rendererRootAddr.address());
			const auto* queueOwnerA = rendererRoot ? static_cast<const QueueOwnerA*>(rendererRoot->queueOwnerA) : nullptr;
			const auto* queueOwnerB = queueOwnerA ? static_cast<const QueueOwnerB*>(queueOwnerA->queueOwnerB) : nullptr;
			commandQueue = queueOwnerB ? queueOwnerB->commandQueue : nullptr;

			if (swapChain && commandQueue) {
				if (Overlay::InitializeFromSwapChain(swapChain, commandQueue)) {
					InstallDXGIHooks(swapChain);
				}
				return;
			}

			if (!loggedMissingData.exchange(true, std::memory_order_relaxed)) {
				REX::WARN(
					"SwapChainWrapperHook: bootstrap is missing data swapChain={:#x} commandQueue={:#x}",
					reinterpret_cast<std::uintptr_t>(swapChain),
					reinterpret_cast<std::uintptr_t>(commandQueue));
			}
		}

		class SwapChainWrapperHook
		{
		public:
			static bool Install()
			{
				std::scoped_lock lock(_installLock);

				if (_installed) {
					return true;
				}

				auto& trampoline = REL::GetTrampoline();

				REL::Relocation<std::uintptr_t> callsite{ REL::ID(141996), 0x130 };
				_swapChainWrapperOriginal = reinterpret_cast<func_t>(
					trampoline.write_call<5>(callsite.address(), SwapChainWrapperThunk));

				if (!_swapChainWrapperOriginal) {
					REX::ERROR(
						"SwapChainWrapperHook: failed to install hook at {:#x}",
						callsite.address());
					return false;
				}

				REX::INFO(
					"SwapChainWrapperHook: installed callsite hook at {:#x}",
					callsite.address()
				);

				_installed = true;
				return true;
			}

		private:
			using func_t = std::int64_t(__fastcall*)(
				void* a_context,
				void* a_descOrState,
				void* a_swapChainState);

			static std::int64_t __fastcall SwapChainWrapperThunk(
				void* a_context,
				void* a_descOrState,
				void* a_swapChainState)
			{
				return Invoke(_swapChainWrapperOriginal, a_context, a_descOrState, a_swapChainState);
			}

			static std::int64_t Invoke(
				func_t a_original,
				void* a_context,
				void* a_descOrState,
				void* a_swapChainState)
			{
				if (!a_original) {
					REX::ERROR("SwapChainWrapperHook: original function pointer is null");
					return 0;
				}

				const auto result = a_original(a_context, a_descOrState, a_swapChainState);

				TryBootstrapImGui(a_context, a_descOrState, a_swapChainState);

				return result;
			}

			static inline std::mutex _installLock;
			static inline func_t _swapChainWrapperOriginal{ nullptr };
			static inline bool _installed{ false };
		};
	}

	bool Install()
	{
		return SwapChainWrapperHook::Install();
	}
}

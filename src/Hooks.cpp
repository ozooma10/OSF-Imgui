#include "Hooks.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#include "Overlay.h"
#include "REL/Relocation.h"
#include "SFSE/API.h"

namespace Hooks
{
	namespace
	{
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
				Overlay::InitializeFromSwapChain(swapChain, commandQueue);
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

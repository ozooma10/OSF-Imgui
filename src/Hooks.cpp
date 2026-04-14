#include "Hooks.h"

#include <atomic>
#include <mutex>
#include <cstdint>

#include "REL/Relocation.h"
#include "SFSE/API.h"

namespace Hooks
{
	namespace
	{
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
						"SwapChainWrapperHook: failed to install secondary hook at {:#x}",
						callsite.address());
					return false;
				}

				REX::INFO(
					"SwapChainWrapperHook: installed secondary callsite hook at {:#x}",
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
				return Invoke(_swapChainWrapperOriginal, _callCount, a_context, a_descOrState, a_swapChainState);
			}

			static std::int64_t Invoke(
				func_t a_original,
				std::atomic_uint32_t& a_callCount,
				void* a_context,
				void* a_descOrState,
				void* a_swapChainState)
			{
				if (!a_original) {
					REX::ERROR("SwapChainWrapperHook: original function pointer is null");
					return 0;
				}

				const auto result = a_original(a_context, a_descOrState, a_swapChainState);
				const auto count = a_callCount.fetch_add(1, std::memory_order_relaxed) + 1;

				REX::INFO(
					"SwapChainWrapperHook call #{} ctx={:#x} desc={:#x} state={:#x} result={:#x}",
					count,
					reinterpret_cast<std::uintptr_t>(a_context),
					reinterpret_cast<std::uintptr_t>(a_descOrState),
					reinterpret_cast<std::uintptr_t>(a_swapChainState),
					static_cast<std::uint64_t>(result));

				return result;
			}

			static inline std::mutex _installLock;
			static inline std::atomic_uint32_t _callCount{ 0 };
			static inline func_t _swapChainWrapperOriginal{ nullptr };
			static inline bool _installed{ false };
		};
	}

	bool Install()
	{
		return SwapChainWrapperHook::Install();
	}
}

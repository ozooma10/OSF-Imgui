#pragma once

struct IDXGISwapChain;

namespace Hooks
{
	bool Install();

	struct SwapChainWrapperHook {
		static std::int64_t thunk(void* a_context,
				void* a_descOrState,
				void* a_swapChainState);
		static bool install();
		static inline REL::Relocation<decltype(thunk)> originalFunction;
	};

	namespace DXGIHooks
	{
		bool Install(IDXGISwapChain *a_swapChain);
	}
}

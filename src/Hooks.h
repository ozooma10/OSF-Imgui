#pragma once

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

	struct UIRenderDispatchHook {
		static void thunk(std::uintptr_t a_uiManager, std::int64_t a_param2);
		static bool install();
		static inline REL::Relocation<decltype(thunk)> originalFunction;
	};
}

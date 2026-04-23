#pragma once

#include <cstdint>

#include <Unknwn.h>

#ifdef ERROR
#undef ERROR
#endif

struct IDXGISwapChain;

namespace Hooks
{
	bool Install();

	struct SwapChainWrapperHook
	{
		static std::int64_t thunk(void *a_context,
								  void *a_descOrState,
								  void *a_swapChainState);
		static bool install();
		static inline REL::Relocation<decltype(thunk)> originalFunction;
	};

	struct FramePresentHook
	{
		static void thunk(void *a_presentBatch);
		static bool install();
		static inline REL::Relocation<decltype(thunk)> originalFunction;
	};

	struct RawInputQueueHook
	{
		static std::uintptr_t thunk(void *a_context,
									void *a_rawInputHandle,
									void *a_state,
									void *a_rawInputHandleMirror);
		static bool install();
		static inline REL::Relocation<decltype(thunk)> originalFunction;
	};

	struct InputEventReceiverHook
	{
		using func_t = void (*)(void *a_receiver, const RE::InputEvent *a_queueHead);

		static void thunkUI(void *a_receiver, const RE::InputEvent *a_queueHead);
		static bool install();

		static inline REL::Relocation<func_t> originalUIFunction;
	};
}

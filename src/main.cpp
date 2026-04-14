#include "pch.h"

#include "Hooks.h"

namespace
{
	constexpr std::size_t kTrampolineSize = 1 << 10;
}

SFSE_PLUGIN_PRELOAD(const SFSE::PreLoadInterface* a_sfse)
{
	SFSE::Init(a_sfse, {
		.trampoline = true,
		.trampolineSize = kTrampolineSize
	});
	return true;
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse, {
		.trampoline = true,
		.trampolineSize = kTrampolineSize
	});

	if (Hooks::Install()) {
		REX::INFO("OSF Menu Framework plugin loaded with DXGI hook support");
	} else {
		REX::ERROR("OSF Menu Framework plugin loaded, but the DXGI hook failed to initialize");
	}

	return true;
}

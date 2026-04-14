#include "pch.h"

#include "DebugOverlayService.h"

SFSE_PLUGIN_PRELOAD(const SFSE::PreLoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);
	return true;
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	auto& overlay = DebugOverlayService::GetSingleton();
	overlay.RegisterBuiltInPanels();

	if (overlay.Install()) {
		REX::INFO("OSF Menu Framework plugin loaded with Dear ImGui debug overlay support");
	} else {
		REX::ERROR("OSF Menu Framework plugin loaded, but the debug overlay failed to initialize: {}", overlay.GetLastError());
	}

	return true;
}

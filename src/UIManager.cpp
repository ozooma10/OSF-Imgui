#include "UIManager.h"

#include <atomic>

#ifdef ERROR
#undef ERROR
#endif

#include "RE/M/Main.h"
#include "REX/REX.h"
#include "imgui.h"

namespace UIManager
{
	namespace
	{
		std::atomic_bool g_open{ false };

		void ApplyPause(bool a_pause)
		{
			if (auto* main = RE::Main::GetSingleton()) {
				main->isGameMenuPaused = a_pause;
			}
		}
	}

	void Open()
	{
		if (!g_open.exchange(true)) {
			ApplyPause(true);
			REX::INFO("UIManager: opened");
		}
	}

	void Close()
	{
		if (g_open.exchange(false)) {
			ApplyPause(false);
			REX::INFO("UIManager: closed");
		}
	}

	void Toggle()
	{
		g_open.load() ? Close() : Open();
	}

	bool IsOpen()
	{
		return g_open.load();
	}

	void DrawFrame()
	{
		if (!IsOpen()) {
			return;
		}

		bool stillOpen = true;
		ImGui::Begin("OSF Menu", &stillOpen);
		ImGui::Text("OSF Menu Framework");
		ImGui::End();

		if (!stillOpen) {
			Close();
		}
	}
}

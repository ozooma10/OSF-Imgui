#include "Util/Freeze.h"

#include <atomic>
#include <mutex>
#include <string_view>

#include "RE/B/BSFixedString.h"
#include "RE/M/Main.h"
#include "RE/U/UI.h"
#include "REL/Relocation.h"
#include "REX/REX.h"

namespace
{
	using ConsoleHelper_t = bool (*)();

	constexpr REL::ID kToggleGamePauseID{66827};
	constexpr REL::ID kStepOneFrameID{66828};

	std::atomic_bool g_wantsFreeze{false};
	std::atomic_bool g_ownsFreeze{false};
	std::mutex g_lock;

	[[nodiscard]] bool FreezeTime()
	{
		const auto *main = RE::Main::GetSingleton();
		return main && main->freezeTime;
	}

	[[nodiscard]] bool FreezeNextFrame()
	{
		const auto *main = RE::Main::GetSingleton();
		return main && main->freezeNextFrame;
	}

	[[nodiscard]] bool IsPauseMenuOpen()
	{
		static const RE::BSFixedString pauseMenu{"PauseMenu"};
		const auto *ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(pauseMenu);
	}

	bool ToggleGamePause(std::string_view a_reason)
	{
		const bool before = FreezeTime();
		static REL::Relocation<ConsoleHelper_t> toggle{kToggleGamePauseID};
		const bool result = toggle();
		const bool after = FreezeTime();

		REX::INFO(
			"Freeze: ToggleGamePause reason={} result={} freezeTime {} -> {}",
			a_reason,
			result,
			before,
			after);
		return result;
	}

	void ReleaseOwnedFreeze(std::string_view a_reason)
	{
		if (!g_ownsFreeze.load(std::memory_order_acquire))
		{
			return;
		}

		if (IsPauseMenuOpen())
		{
			REX::INFO(
				"Freeze: release skipped reason={} PauseMenu open ownsFreeze=true freezeTime={}",
				a_reason,
				FreezeTime());
			return;
		}

		if (FreezeTime())
		{
			ToggleGamePause(a_reason);
		}

		g_ownsFreeze.store(false, std::memory_order_release);
		REX::INFO(
			"Freeze: released reason={} ownsFreeze true -> false freezeTime={}",
			a_reason,
			FreezeTime());
	}
}

void Util::Freeze::Freeze()
{
	std::scoped_lock lock(g_lock);
	g_wantsFreeze.store(true, std::memory_order_release);

	const bool before = FreezeTime();
	if (!before)
	{
		ToggleGamePause("Freeze");
		g_ownsFreeze.store(FreezeTime(), std::memory_order_release);
	}

	REX::INFO(
		"Freeze: requested freezeTime {} -> {} ownsFreeze={}",
		before,
		FreezeTime(),
		g_ownsFreeze.load(std::memory_order_relaxed));
}

void Util::Freeze::Unfreeze()
{
	std::scoped_lock lock(g_lock);
	g_wantsFreeze.store(false, std::memory_order_release);
	ReleaseOwnedFreeze("Unfreeze");
}

void Util::Freeze::StepOneFrame()
{
	std::scoped_lock lock(g_lock);

	const bool beforeFreeze = FreezeTime();
	const bool beforeStep = FreezeNextFrame();
	static REL::Relocation<ConsoleHelper_t> step{kStepOneFrameID};
	const bool result = step();

	REX::INFO(
		"Freeze: StepOneFrame result={} freezeTime {} -> {} freezeNextFrame {} -> {}",
		result,
		beforeFreeze,
		FreezeTime(),
		beforeStep,
		FreezeNextFrame());
}

void Util::Freeze::OnFrame()
{
	std::scoped_lock lock(g_lock);

	if (!g_wantsFreeze.load(std::memory_order_acquire))
	{
		if (g_ownsFreeze.load(std::memory_order_acquire) && !IsPauseMenuOpen())
		{
			ReleaseOwnedFreeze("OnFrame");
		}
		return;
	}

	if (FreezeTime() || IsPauseMenuOpen())
	{
		return;
	}

	ToggleGamePause("OnFrame");
	g_ownsFreeze.store(FreezeTime(), std::memory_order_release);
}

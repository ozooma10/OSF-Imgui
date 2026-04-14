#include "pch.h"

#include <string>
#include <variant>

namespace
{
	constexpr auto kPapyrusClass = "OSF_Anim_Native";
	constexpr auto kPlayAnimationFunction = "PlayAnimationEvent";

	void DispatchAnimationEvent(RE::TESFormID a_targetFormID, std::string a_eventName)
	{
		auto* const target = RE::TESForm::LookupByID<RE::Actor>(a_targetFormID);
		if (!target) {
			REX::WARN("OSF: actor {:08X} no longer exists for animation event '{}'", a_targetFormID, a_eventName);
			return;
		}

		const RE::BSFixedString eventName{ a_eventName.c_str() };
		const bool started = target->NotifyAnimationGraphImpl(eventName);
		if (!started) {
			REX::WARN("OSF: animation event '{}' was rejected for actor {:08X}", a_eventName, a_targetFormID);
			return;
		}

		REX::INFO("OSF: played animation event '{}' on actor {:08X}", a_eventName, a_targetFormID);
	}

	bool QueueAnimationEvent(RE::Actor* a_target, const RE::BSFixedString& a_eventName)
	{
		if (!a_target) {
			REX::WARN("OSF: PlayAnimationEvent called with a null actor");
			return false;
		}

		if (a_eventName.empty()) {
			REX::WARN("OSF: PlayAnimationEvent called with an empty animation event name");
			return false;
		}

		const auto* const taskInterface = SFSE::GetTaskInterface();
		if (!taskInterface) {
			REX::ERROR("OSF: SFSE task interface is unavailable");
			return false;
		}

		const auto targetFormID = a_target->GetFormID();
		const std::string eventName = a_eventName.c_str();

		taskInterface->AddTask([targetFormID, eventName]() {
			DispatchAnimationEvent(targetFormID, eventName);
		});

		REX::INFO("OSF: queued animation event '{}' for actor {:08X}", eventName, targetFormID);
		return true;
	}

	bool PlayAnimationEvent(std::monostate, RE::Actor* a_target, RE::BSFixedString a_eventName)
	{
		return QueueAnimationEvent(a_target, a_eventName);
	}

	void TryRegisterPapyrus()
	{
		static bool registered = false;
		if (registered) {
			return;
		}

		auto* const gameVM = RE::GameVM::GetSingleton();
		auto* const vm = gameVM ? gameVM->GetVM() : nullptr;
		if (!vm) {
			REX::WARN("OSF: game VM is not ready yet; delaying Papyrus registration");
			return;
		}

		vm->BindNativeMethod(kPapyrusClass, kPlayAnimationFunction, PlayAnimationEvent, false, false);
		registered = true;

		REX::INFO("OSF: registered Papyrus native {}.{}", kPapyrusClass, kPlayAnimationFunction);
	}

	void OnSFSEMessage(SFSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case SFSE::MessagingInterface::kPostPostLoad:
		case SFSE::MessagingInterface::kPostDataLoad:
		case SFSE::MessagingInterface::kPostPostDataLoad:
			TryRegisterPapyrus();
			break;
		default:
			break;
		}
	}
}

SFSE_PLUGIN_PRELOAD(const SFSE::PreLoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);
	return true;
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	const auto* const messaging = SFSE::GetMessagingInterface();
	if (!messaging) {
		REX::ERROR("OSF: SFSE messaging interface is unavailable");
		return false;
	}

	if (!messaging->RegisterListener(OnSFSEMessage)) {
		REX::ERROR("OSF: failed to register SFSE messaging listener");
		return false;
	}

	TryRegisterPapyrus();

	REX::INFO("OSF animation plugin loaded");
	return true;
}

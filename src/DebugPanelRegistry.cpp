#include "DebugPanelRegistry.h"

DebugPanelRegistry& DebugPanelRegistry::GetSingleton()
{
	static DebugPanelRegistry singleton;
	return singleton;
}

void DebugPanelRegistry::Register(std::string name, std::function<void()> draw)
{
	if (name.empty() || !draw) {
		return;
	}

	std::unique_lock lock(_lock);

	const auto existing = std::ranges::find(_panels, name, &DebugPanel::name);
	if (existing != _panels.end()) {
		existing->draw = std::move(draw);
		return;
	}

	_panels.push_back(DebugPanel{
		.name = std::move(name),
		.draw = std::move(draw)
	});
}

std::vector<DebugPanel> DebugPanelRegistry::Snapshot() const
{
	std::shared_lock lock(_lock);
	return _panels;
}

void RegisterDebugPanel(std::string name, std::function<void()> draw)
{
	DebugPanelRegistry::GetSingleton().Register(std::move(name), std::move(draw));
}

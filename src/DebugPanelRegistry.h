#pragma once

#include "pch.h"

struct DebugPanel
{
	std::string           name;
	std::function<void()> draw;
};

class DebugPanelRegistry
{
public:
	static DebugPanelRegistry& GetSingleton();

	void Register(std::string name, std::function<void()> draw);

	[[nodiscard]] std::vector<DebugPanel> Snapshot() const;

private:
	mutable std::shared_mutex _lock;
	std::vector<DebugPanel>   _panels;
};

void RegisterDebugPanel(std::string name, std::function<void()> draw);

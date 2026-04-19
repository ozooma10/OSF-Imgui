#pragma once

struct ImFont;
struct ImGuiIO;

namespace Fonts
{
	bool Load(ImGuiIO& a_io, float a_size = 16.0f);

	ImFont* Default();
	ImFont* Solid();
	ImFont* Regular();
	ImFont* Brands();

	void PushSolid();
	void PushRegular();
	void PushBrands();
	void Pop();
}

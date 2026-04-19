#include "Fonts.h"

#include <filesystem>

#include "REX/REX.h"
#include "imgui.h"

namespace Fonts
{
	namespace
	{
		constexpr ImWchar kIconMin = 0xe005;
		constexpr ImWchar kIconMax = 0xf8ff;
		constexpr auto kFontDir = "Data/SFSE/Plugins/Fonts";

		ImFont* g_default{ nullptr };
		ImFont* g_solid{ nullptr };
		ImFont* g_regular{ nullptr };
		ImFont* g_brands{ nullptr };

		ImFont* LoadTTF(ImGuiIO& a_io, const char* a_name, float a_size,
			const ImFontConfig* a_cfg, const ImWchar* a_ranges)
		{
			const auto path = std::filesystem::path(kFontDir) / a_name;
			if (!std::filesystem::exists(path)) {
				REX::WARN("Fonts: missing {}", path.string());
				return nullptr;
			}
			return a_io.Fonts->AddFontFromFileTTF(path.string().c_str(), a_size, a_cfg, a_ranges);
		}
	}

	bool Load(ImGuiIO& a_io, float a_size)
	{
		static const ImWchar iconRanges[] = { kIconMin, kIconMax, 0 };

		ImFontConfig defaultCfg{};
		defaultCfg.SizePixels = a_size;
		g_default = a_io.Fonts->AddFontDefault(&defaultCfg);

		ImFontConfig merge{};
		merge.MergeMode = true;
		merge.PixelSnapH = true;
		LoadTTF(a_io, "fa-solid-900.ttf", a_size, &merge, iconRanges);
		LoadTTF(a_io, "fa-regular-400.ttf", a_size, &merge, iconRanges);
		LoadTTF(a_io, "fa-brands-400.ttf", a_size, &merge, iconRanges);

		g_solid = LoadTTF(a_io, "fa-solid-900.ttf", a_size, nullptr, iconRanges);
		g_regular = LoadTTF(a_io, "fa-regular-400.ttf", a_size, nullptr, iconRanges);
		g_brands = LoadTTF(a_io, "fa-brands-400.ttf", a_size, nullptr, iconRanges);

		return g_solid && g_regular && g_brands;
	}

	ImFont* Default() { return g_default; }
	ImFont* Solid() { return g_solid; }
	ImFont* Regular() { return g_regular; }
	ImFont* Brands() { return g_brands; }

	void PushSolid()
	{
		if (g_solid) ImGui::PushFont(g_solid);
	}

	void PushRegular()
	{
		if (g_regular) ImGui::PushFont(g_regular);
	}

	void PushBrands()
	{
		if (g_brands) ImGui::PushFont(g_brands);
	}

	void Pop()
	{
		ImGui::PopFont();
	}
}

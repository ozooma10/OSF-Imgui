#pragma once

#include <imgui.h>

namespace Theme
{
    namespace Color
    {
        // Near-black with a faint blue-gray lift — matches Starfield panel blacks.
        inline constexpr ImVec4 Background   = ImVec4(0.035f, 0.047f, 0.059f, 0.96f);
        inline constexpr ImVec4 Panel        = ImVec4(0.055f, 0.070f, 0.086f, 1.00f);
        inline constexpr ImVec4 PanelRaised  = ImVec4(0.082f, 0.100f, 0.120f, 1.00f);

        // Signature Starfield amber.
        inline constexpr ImVec4 Accent       = ImVec4(1.000f, 0.580f, 0.290f, 1.00f);
        inline constexpr ImVec4 AccentBright = ImVec4(1.000f, 0.720f, 0.430f, 1.00f);
        inline constexpr ImVec4 AccentSoft   = ImVec4(1.000f, 0.580f, 0.290f, 0.22f);

        inline constexpr ImVec4 TextPrimary  = ImVec4(0.910f, 0.915f, 0.925f, 1.00f);
        inline constexpr ImVec4 TextMuted    = ImVec4(0.540f, 0.570f, 0.610f, 1.00f);
        inline constexpr ImVec4 TextDim      = ImVec4(0.355f, 0.385f, 0.420f, 1.00f);

        inline constexpr ImVec4 Border       = ImVec4(1.000f, 1.000f, 1.000f, 0.07f);
        inline constexpr ImVec4 Divider      = ImVec4(1.000f, 1.000f, 1.000f, 0.12f);
    }

    void Apply();

    // Fills a horizontal line (accent by default) across the current content region.
    void AccentDivider(float a_thickness = 1.5f, const ImVec4& a_color = Color::Accent);
    void Divider(float a_thickness = 1.0f, const ImVec4& a_color = Color::Divider);

    // Writes a_text as upper-cased, letter-spaced header text.
    void HeaderText(const char* a_text, const ImVec4& a_color = Color::TextPrimary);

    // Draws a vertical accent bar on the left edge of the last item when a_active is true.
    void ItemAccentBar(bool a_active, float a_thickness = 3.0f, const ImVec4& a_color = Color::Accent);

    ImU32 ToU32(const ImVec4& a_color);
}

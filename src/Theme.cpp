#include "Theme.h"

#include <cctype>
#include <cstring>
#include <string>

namespace Theme
{
    ImU32 ToU32(const ImVec4& a_color)
    {
        return ImGui::ColorConvertFloat4ToU32(a_color);
    }

    void Apply()
    {
        auto& style = ImGui::GetStyle();

        style.WindowPadding       = ImVec2(22.0f, 18.0f);
        style.FramePadding        = ImVec2(12.0f, 8.0f);
        style.CellPadding         = ImVec2(8.0f, 6.0f);
        style.ItemSpacing         = ImVec2(10.0f, 10.0f);
        style.ItemInnerSpacing    = ImVec2(8.0f, 6.0f);
        style.TouchExtraPadding   = ImVec2(0.0f, 0.0f);
        style.IndentSpacing       = 18.0f;
        style.ScrollbarSize       = 10.0f;
        style.GrabMinSize         = 10.0f;
        style.ColumnsMinSpacing   = 6.0f;

        style.WindowRounding      = 0.0f;
        style.ChildRounding       = 0.0f;
        style.FrameRounding       = 0.0f;
        style.PopupRounding       = 0.0f;
        style.ScrollbarRounding   = 0.0f;
        style.GrabRounding        = 0.0f;
        style.TabRounding         = 0.0f;

        style.WindowBorderSize    = 0.0f;
        style.ChildBorderSize     = 1.0f;
        style.FrameBorderSize     = 0.0f;
        style.PopupBorderSize     = 1.0f;
        style.TabBorderSize       = 0.0f;

        style.WindowTitleAlign    = ImVec2(0.0f, 0.5f);
        style.ButtonTextAlign     = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextAlign  = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding = ImVec2(18.0f, 3.0f);
        style.SeparatorTextBorderSize = 1.0f;

        style.DisabledAlpha       = 0.45f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text]                  = Color::TextPrimary;
        c[ImGuiCol_TextDisabled]          = Color::TextDim;

        c[ImGuiCol_WindowBg]              = Color::Background;
        c[ImGuiCol_ChildBg]               = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_PopupBg]               = Color::Panel;

        c[ImGuiCol_Border]                = Color::Border;
        c[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        c[ImGuiCol_FrameBg]               = ImVec4(0.090f, 0.107f, 0.128f, 1.00f);
        c[ImGuiCol_FrameBgHovered]        = ImVec4(0.118f, 0.138f, 0.162f, 1.00f);
        c[ImGuiCol_FrameBgActive]         = ImVec4(0.150f, 0.172f, 0.200f, 1.00f);

        c[ImGuiCol_TitleBg]               = Color::Panel;
        c[ImGuiCol_TitleBgActive]         = Color::Panel;
        c[ImGuiCol_TitleBgCollapsed]      = Color::Panel;

        c[ImGuiCol_MenuBarBg]             = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        c[ImGuiCol_ScrollbarBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_ScrollbarGrab]         = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
        c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(1.0f, 1.0f, 1.0f, 0.22f);
        c[ImGuiCol_ScrollbarGrabActive]   = Color::Accent;

        c[ImGuiCol_CheckMark]             = Color::Accent;
        c[ImGuiCol_SliderGrab]            = Color::Accent;
        c[ImGuiCol_SliderGrabActive]      = Color::AccentBright;

        c[ImGuiCol_Button]                = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
        c[ImGuiCol_ButtonHovered]         = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.22f);
        c[ImGuiCol_ButtonActive]          = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.40f);

        c[ImGuiCol_Header]                = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.16f);
        c[ImGuiCol_HeaderHovered]         = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.22f);
        c[ImGuiCol_HeaderActive]          = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.32f);

        c[ImGuiCol_Separator]             = Color::Border;
        c[ImGuiCol_SeparatorHovered]      = Color::Accent;
        c[ImGuiCol_SeparatorActive]       = Color::AccentBright;

        c[ImGuiCol_ResizeGrip]            = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
        c[ImGuiCol_ResizeGripHovered]     = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.35f);
        c[ImGuiCol_ResizeGripActive]      = Color::Accent;

        c[ImGuiCol_Tab]                   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TabHovered]            = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.22f);
        c[ImGuiCol_TabActive]             = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.18f);
        c[ImGuiCol_TabUnfocused]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TabUnfocusedActive]    = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.10f);

        c[ImGuiCol_PlotLines]             = Color::Accent;
        c[ImGuiCol_PlotLinesHovered]      = Color::AccentBright;
        c[ImGuiCol_PlotHistogram]         = Color::Accent;
        c[ImGuiCol_PlotHistogramHovered]  = Color::AccentBright;

        c[ImGuiCol_TextSelectedBg]        = ImVec4(Color::Accent.x, Color::Accent.y, Color::Accent.z, 0.35f);
        c[ImGuiCol_DragDropTarget]        = Color::Accent;

        c[ImGuiCol_NavHighlight]          = Color::Accent;
        c[ImGuiCol_NavWindowingHighlight] = Color::Accent;
        c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
        c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
    }

    void AccentDivider(float a_thickness, const ImVec4& a_color)
    {
        auto* list = ImGui::GetWindowDrawList();
        const auto pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + a_thickness), ToU32(a_color));
        ImGui::Dummy(ImVec2(width, a_thickness));
    }

    void Divider(float a_thickness, const ImVec4& a_color)
    {
        AccentDivider(a_thickness, a_color);
    }

    void HeaderText(const char* a_text, const ImVec4& a_color)
    {
        if (!a_text)
        {
            return;
        }

        std::string spaced;
        spaced.reserve(std::strlen(a_text) * 2);
        bool first = true;
        for (const char* p = a_text; *p != '\0'; ++p)
        {
            const auto ch = static_cast<unsigned char>(*p);
            if (!first && ch != ' ')
            {
                spaced.push_back(' ');
            }
            if (ch == ' ')
            {
                spaced.append("  ");
            }
            else
            {
                spaced.push_back(static_cast<char>(std::toupper(ch)));
            }
            first = false;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, a_color);
        ImGui::TextUnformatted(spaced.c_str());
        ImGui::PopStyleColor();
    }

    void ItemAccentBar(bool a_active, float a_thickness, const ImVec4& a_color)
    {
        if (!a_active)
        {
            return;
        }

        auto* list = ImGui::GetWindowDrawList();
        const auto rmin = ImGui::GetItemRectMin();
        const auto rmax = ImGui::GetItemRectMax();
        list->AddRectFilled(rmin, ImVec2(rmin.x + a_thickness, rmax.y), ToU32(a_color));
    }
}

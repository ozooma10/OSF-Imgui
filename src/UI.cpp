#include "UI.h"

#include <imgui.h>

#include "Fonts.h"
#include "Theme.h"
#include "WindowManager.h"

static ImGuiTextFilter filter;

UI::MenuTree *UI::RootMenu = new UI::MenuTree();

int frame = 0;

size_t item_current_idx = 0;
size_t node_id = 0;
UI::MenuTree *display_node;

static ImGuiTreeNodeFlags base_flags =
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

void DummyRenderer(std::pair<const std::string, UI::MenuTree *> &node)
{
    ++node_id;
    for (auto &item : node.second->Children)
    {
        DummyRenderer(item);
    }
}

void RenderNode(std::pair<const std::string, UI::MenuTree *> &node)
{
    ++node_id;
    ImGuiTreeNodeFlags node_flags = base_flags;
    const bool is_selected = (item_current_idx == node_id);
    if (is_selected)
    {
        node_flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (node.second->Children.size() == 0)
    {
        node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    bool node_open = ImGui::TreeNodeEx((void *)(intptr_t)node_id, node_flags, "%s", node.first.c_str());
    Theme::ItemAccentBar(is_selected);

    bool itemClicked = ImGui::IsItemClicked();
    bool itemToggledOpen = ImGui::IsItemToggledOpen();
    bool gamepadButtonPressed = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown);
    bool itemIsFocused = ImGui::IsItemFocused();

    if ((itemClicked || (gamepadButtonPressed && itemIsFocused)) && !itemToggledOpen)
    {
        if (node.second->Render)
        {
            item_current_idx = node_id;
            display_node = node.second;
        }
    }
    if (node_open && node.second->Children.size() != 0)
    {
        for (auto &item : node.second->SortedChildren)
        {
            RenderNode(item);
        }
        ImGui::TreePop();
    }
    else
    {
        for (auto &item : node.second->Children)
        {
            DummyRenderer(item);
        }
    }
}

namespace
{
    void DrawTitleStrip()
    {
        const float stripHeight = 56.0f;
        ImGui::BeginChild("##MCPTitleStrip", ImVec2(0, stripHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x, cursor.y + (stripHeight * 0.5f) - ImGui::GetTextLineHeight() * 0.5f - 2.0f));
        Theme::HeaderText("Mod Control Panel", Theme::Color::TextPrimary);

        const float closeSize = 28.0f;
        const float rightPad = 4.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - closeSize - rightPad);
        ImGui::SetCursorPosY(cursor.y + (stripHeight - closeSize) * 0.5f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(Theme::Color::Accent.x, Theme::Color::Accent.y, Theme::Color::Accent.z, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(Theme::Color::Accent.x, Theme::Color::Accent.y, Theme::Color::Accent.z, 0.45f));
        if (ImGui::Button("X", ImVec2(closeSize, closeSize)))
        {
            WindowManager::Close();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::EndChild();

        // Thin accent underline that reads as Starfield's framing.
        Theme::AccentDivider(1.5f, Theme::Color::Accent);
    }

    void DrawFilterRow(float a_leftColumnWidth)
    {
        const float rowHeight = 36.0f;
        ImGui::BeginChild("##MCPFilterRow", ImVec2(a_leftColumnWidth, rowHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, Theme::Color::TextMuted);
        filter.Draw("##MCPMenuFilter", -FLT_MIN);
        ImGui::PopStyleColor(2);

        // Underline the search field with a subtle divider for the native feel.
        auto* list = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        list->AddRectFilled(ImVec2(p.x, p.y - 2.0f), ImVec2(p.x + w, p.y - 1.0f),
                            Theme::ToU32(Theme::Color::Divider));

        ImGui::EndChild();
    }

    void DrawContentHeader(float a_rowHeight)
    {
        ImGui::BeginChild("##MCPContentHeader", ImVec2(0, a_rowHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (display_node)
        {
            const float yCenter = (a_rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
            ImGui::SetCursorPosY(yCenter);
            ImGui::SetCursorPosX(0.0f);
            Theme::HeaderText(display_node->Title.c_str(), Theme::Color::Accent);
        }

        ImGui::EndChild();
    }
}

void __stdcall UI::RenderMenuWindow()
{
    auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2{0.5f, 0.5f});
    ImGui::SetNextWindowSize(ImVec2{viewport->Size.x * 0.8f, viewport->Size.y * 0.8f}, ImGuiCond_Appearing);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 18.0f));
    ImGui::Begin("##MCPMainWindow", nullptr, window_flags);

    // Soft edge framing — a hairline accent at the top-left, Starfield HUD feel.
    {
        auto* list = ImGui::GetWindowDrawList();
        const ImVec2 wmin = ImGui::GetWindowPos();
        const ImVec2 wmax = ImVec2(wmin.x + ImGui::GetWindowWidth(), wmin.y + ImGui::GetWindowHeight());
        const ImU32 border = Theme::ToU32(Theme::Color::Border);
        list->AddRect(wmin, wmax, border, 0.0f, 0, 1.0f);
    }

    DrawTitleStrip();

    const float leftColWidth = ImGui::GetContentRegionAvail().x * 0.3f;
    const float headerHeight = 44.0f;

    DrawFilterRow(leftColWidth);

    ImGui::SameLine();

    DrawContentHeader(headerHeight);

    // Divider row between header and content/tree areas.
    Theme::Divider(1.0f, Theme::Color::Divider);

    // Left: menu tree.
    ImGui::BeginChild("##MCPTreeView", ImVec2(leftColWidth, -FLT_MIN), ImGuiChildFlags_NavFlattened);

    node_id = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
    for (const auto &item : RootMenu->Children)
    {
        const bool matches = filter.PassFilter(item.first.c_str());
        if (matches && ImGui::CollapsingHeader(std::format("{}##{}", item.first, node_id).c_str(),
                                               ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(6.0f);
            for (auto node : item.second->SortedChildren)
            {
                RenderNode(node);
            }
            ImGui::Unindent(6.0f);
        }
        else
        {
            for (auto node : item.second->Children)
            {
                DummyRenderer(node);
            }
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    // Vertical divider between the left list and right content.
    {
        auto* list = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetItemRectMax();
        const ImVec2 b = ImGui::GetItemRectMin();
        const float x = a.x + ImGui::GetStyle().ItemSpacing.x * 0.5f;
        list->AddLine(ImVec2(x, b.y), ImVec2(x, a.y), Theme::ToU32(Theme::Color::Divider), 1.0f);
    }

    ImGui::SameLine();

    // Right: content.
    ImGui::BeginChild("##MCPContentPane", ImVec2(0, -FLT_MIN), ImGuiChildFlags_None);
    if (display_node && display_node->Render)
    {
        display_node->Render();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color::TextMuted);
        ImGui::TextWrapped("Select an entry from the left to view its settings.");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar();
}

void UI::AddToTree(UI::MenuTree *node, std::vector<std::string> &path, RenderFunction render, std::string title)
{
    if (!path.empty())
    {
        auto currentName = path.front();
        path.erase(path.begin());

        auto foundItem = node->Children.find(currentName);
        if (foundItem != node->Children.end())
        {
            AddToTree(foundItem->second, path, render, title);
        }
        else
        {
            auto newItem = new UI::MenuTree();
            node->Children[currentName] = newItem;
            node->SortedChildren.push_back(std::pair<const std::string, UI::MenuTree *>(currentName, newItem));
            AddToTree(newItem, path, render, title);
        }
    }
    else
    {
        node->Render = render;
        node->Title = title;
    }
}

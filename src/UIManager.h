#pragma once

namespace UIManager
{
    void Open();
    void Close();
    void Toggle();

    [[nodiscard]] bool IsOpen();

    // Must be called each frame between ImGui::NewFrame() and ImGui::Render().
    // Handles the toggle hotkey and draws all UI panels when the menu is open.
    void DrawFrame();
}

#include "WindowManager.h"

#ifdef ERROR
#undef ERROR
#endif

#include "RE/M/Main.h"
#include "REX/REX.h"

namespace
{
    void ApplyPause(bool a_pause)
    {
        if (auto *main = RE::Main::GetSingleton())
        {
            main->isGameMenuPaused = a_pause;
        }
    }

    void RefreshPause()
    {
        ApplyPause(WindowManager::ShouldTheGameBePaused());
    }
}

Window::Window()
{
    Interface = new WindowInterface();
    Render = nullptr;
}

bool WindowManager::IsAnyWindowOpen()
{
    return std::any_of(Windows.begin(), Windows.end(),
                       [](Window *x)
                       { return x->Interface->IsOpen.load(); });
}

bool WindowManager::ShouldTheGameBePaused()
{
    return std::any_of(Windows.begin(), Windows.end(),
                       [](Window *x)
                       {
                           return x->Interface->IsOpen.load() && x->Interface->BlockUserInput.load();
                       });
}

void WindowManager::RefreshPauseState()
{
    RefreshPause();
}

void WindowManager::Open()
{
    if (!MainInterface)
    {
        return;
    }
    if (!MainInterface->IsOpen.exchange(true))
    {
        RefreshPause();
        REX::INFO("WindowManager: opened");

        if (auto *ui = RE::UI::GetSingleton(); ui && !ui->IsMenuOpen(RE::BSFixedString{"PauseMenu"}))
        {
            if (auto *queue = RE::UIMessageQueue::GetSingleton())
            {
                queue->AddMessage(RE::BSFixedString{"PauseMenu"}, RE::UI_MESSAGE_TYPE::kShow);
            }
        }
    }
}

void WindowManager::Close()
{
    bool wasOpen = false;
    for (auto *window : Windows)
    {
        if (window->Interface->IsOpen.exchange(false))
        {
            wasOpen = true;
        }
    }
    if (wasOpen)
    {
        RefreshPause();
        REX::INFO("WindowManager: closed");
    }
}

void WindowManager::Toggle()
{
    if (IsAnyWindowOpen())
    {
        Close();
    }
    else
    {
        Open();
    }
}

void WindowManager::RenderWindows()
{
    for (const auto window : Windows)
    {
        if (window->Interface->IsOpen && window->Render)
        {
            window->Render();
        }
    }
}

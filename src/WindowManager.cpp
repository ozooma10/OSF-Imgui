#include "WindowManager.h"

#ifdef ERROR
#undef ERROR
#endif

#include "RE/M/Main.h"
#include "REX/REX.h"

namespace
{
    // True while the framework is the one holding Main::isGameMenuPaused high.
    // We only touch the flag on transitions so we never clobber the pause state
    // the game itself needs during loading screens, the main menu, cinematics, etc.
    bool g_frameworkOwnsPause = false;

    void ApplyPause(bool a_pause)
    {
        if (auto *main = RE::Main::GetSingleton())
        {
            main->isGameMenuPaused = a_pause;
        }
    }

    // Called every frame from Overlay::UpdateFrameInputState.
    void RefreshPause()
    {
        const bool shouldPause = WindowManager::ShouldTheGameBePaused();

        if (shouldPause)
        {
            ApplyPause(true);
            g_frameworkOwnsPause = true;
        }
        else if (g_frameworkOwnsPause)
        {
            ApplyPause(false);
            g_frameworkOwnsPause = false;
        }
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

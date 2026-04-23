#include "SFSEMenuFramework.h"
#include <imgui.h>
#include "Application.h"
#include "Fonts.h"
#include "UI.h"
#include "TextureLoader.h"
#include "Util/Freeze.h"

void AddSectionItem(const char *path, RenderFunction rendererFunction)
{
    auto pathSplit = SplitString(path, '/');
    AddToTree(UI::RootMenu, pathSplit, rendererFunction, pathSplit.back());
}

WindowInterface *AddWindow(RenderFunction rendererFunction)
{
    auto newWindow = new Window();

    newWindow->Render = rendererFunction;

    WindowManager::Windows.push_back(newWindow);

    return newWindow->Interface;
}

void PushDefault()
{
    Fonts::PushSolid();
}
void PushBig()
{
    Fonts::PushSolid();
}
void PushSmall()
{
    Fonts::PushRegular();
}

void PushSolid()
{
    Fonts::PushSolid();
}

void PushRegular()
{
    Fonts::PushRegular();
}

void PushBrands()
{
    Fonts::PushBrands();
}

void Pop() { Fonts::Pop(); }


int64_t RegisterInpoutEvent(InputEventCallback callback) { return static_cast<int64_t>(Input::Register(callback)); }

void UnregisterInputEvent(uint64_t id) { Input::Unregister(id); }

int64_t RegisterHudElement(HudElementCallback callback) { return HudManager::Register(callback); }

void UnregisterHudElement(uint64_t id) { HudManager::Unregister(id); }

bool IsAnyBlockingWindowOpened() { return WindowManager::ShouldTheGameBePaused(); }

ImTextureID LoadTexture(const char* texturePath, ImVec2* size)
{
    if (!texturePath) {
        return ImTextureID_Invalid;
    }

    return TextureLoader::GetTexture(texturePath, size ? *size : ImVec2{ 0.0f, 0.0f });
}

void DisposeTexture(const char* texturePath)
{
    if (!texturePath) {
        return;
    }

    TextureLoader::DisposeTexture(texturePath);
}

void FreezeGame() { Util::Freeze::Freeze(); }

void UnfreezeGame() { Util::Freeze::Unfreeze(); }

void StepOneGameFrame() { Util::Freeze::StepOneFrame(); }

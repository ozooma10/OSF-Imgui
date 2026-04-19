#include "SFSEMenuFramework.h"
#include <imgui.h>
#include "Application.h"
#include "Fonts.h"
#include "UI.h"

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
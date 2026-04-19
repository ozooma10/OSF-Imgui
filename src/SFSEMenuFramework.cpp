#include "SFSEMenuFramework.h"

WindowInterface *AddWindow(RenderFunction rendererFunction)
{

    auto newWindow = new Window();

    newWindow->Render = rendererFunction;

    WindowManager::Windows.push_back(newWindow);

    return newWindow->Interface;
}
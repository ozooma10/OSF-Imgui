#pragma once
#include "HUDManager.h"
#include "Input.h"
#include "WindowManager.h"

using InputEventCallback = Input::InputEventCallback;

#define FUNCTION_PREFIX extern "C" [[maybe_unused]] __declspec(dllexport)

FUNCTION_PREFIX void AddSectionItem(const char *path, RenderFunction rendererFunction);
FUNCTION_PREFIX WindowInterface *AddWindow(RenderFunction rendererFunction);
FUNCTION_PREFIX void PushBig();
FUNCTION_PREFIX void PushDefault();
FUNCTION_PREFIX void PushSmall();
FUNCTION_PREFIX void PushSolid();
FUNCTION_PREFIX void PushRegular();
FUNCTION_PREFIX void PushBrands();
FUNCTION_PREFIX void Pop();
FUNCTION_PREFIX int64_t RegisterInpoutEvent(InputEventCallback callback);
FUNCTION_PREFIX void UnregisterInputEvent(uint64_t id);
FUNCTION_PREFIX int64_t RegisterHudElement(HudElementCallback callback);
FUNCTION_PREFIX void UnregisterHudElement(uint64_t id);
FUNCTION_PREFIX bool IsAnyBlockingWindowOpened();
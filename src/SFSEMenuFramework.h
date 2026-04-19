#pragma once
#include "WindowManager.h"

#define FUNCTION_PREFIX extern "C" [[maybe_unused]] __declspec(dllexport)

FUNCTION_PREFIX WindowInterface *AddWindow(RenderFunction rendererFunction);

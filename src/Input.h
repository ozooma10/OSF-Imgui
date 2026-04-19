#pragma once

#include <cstdint>

namespace RE
{
    class InputEvent;
}

namespace Input
{
    using InputEventCallback = bool(__stdcall *)(RE::InputEvent *);

    std::uint64_t Register(InputEventCallback a_callback);
    void Unregister(std::uint64_t a_id);

    bool Dispatch(RE::InputEvent *a_queueHead);
}

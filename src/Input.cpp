#include "Input.h"

#include <atomic>
#include <shared_mutex>
#include <unordered_map>

#include "RE/B/BSInputEventUser.h"

namespace Input
{
    namespace
    {
        std::shared_mutex g_mutex;
        std::unordered_map<std::uint64_t, InputEventCallback> g_callbacks;
        std::atomic<std::uint64_t> g_nextId{1};
    }

    std::uint64_t Register(InputEventCallback a_callback)
    {
        if (!a_callback)
        {
            return 0;
        }

        const auto id = g_nextId.fetch_add(1, std::memory_order_relaxed);

        std::unique_lock lock(g_mutex);
        g_callbacks.emplace(id, a_callback);
        return id;
    }

    void Unregister(std::uint64_t a_id)
    {
        if (a_id == 0)
        {
            return;
        }

        std::unique_lock lock(g_mutex);
        g_callbacks.erase(a_id);
    }

    bool Dispatch(RE::InputEvent *a_queueHead)
    {
        if (!a_queueHead)
        {
            return false;
        }

        std::shared_lock lock(g_mutex);
        if (g_callbacks.empty())
        {
            return false;
        }

        bool blocked = false;
        for (auto *event = a_queueHead; event != nullptr; event = event->next)
        {
            for (const auto &[id, callback] : g_callbacks)
            {
                if (callback && callback(event))
                {
                    blocked = true;
                }
            }
        }

        return blocked;
    }
}

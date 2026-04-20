#pragma once

namespace GameState
{
    enum LockState
    {
        None,
        Locked,
        Unlocked,
        Resume
    };
    extern LockState lastLockState;
    void setLockState(LockState newState);
}
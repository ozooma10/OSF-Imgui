#include "GameState.h"

#include "Util/Freeze.h"

GameState::LockState GameState::lastLockState = GameState::LockState::None;

bool didFreezeTime = false;
bool didPushBlur = false;
void GameState::setLockState(LockState newState)
{
    if (lastLockState == newState)
    {
        return;
    }
    lastLockState = newState;
    if (newState == LockState::Locked)
    {
        Util::Freeze::Freeze();
        didFreezeTime = true;
    }
    else if (didFreezeTime)
    {
        Util::Freeze::Unfreeze();
        didFreezeTime = false;
    }
}

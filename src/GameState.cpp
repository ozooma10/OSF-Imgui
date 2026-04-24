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
        REX::INFO("GameState: Locked");
        RE::UIBlurManager::GetSingleton()->IncrementBlurCount();
        Util::Freeze::Freeze();
        didFreezeTime = true;
    }
    else if (didFreezeTime)
    {
        REX::INFO("GameState: Unlocked");
        RE::UIBlurManager::GetSingleton()->DecrementBlurCount();
        Util::Freeze::Unfreeze();
        didFreezeTime = false;
    }
}

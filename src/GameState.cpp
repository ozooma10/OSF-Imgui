#include "GameState.h"

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
    const auto main = RE::Main::GetSingleton();
    // if (newState == LockState::Locked)
    // {
    //     main->freezeTime = true;
    //     didFreezeTime = true;
    // }
    // else
    // {
    //     didFreezeTime = false;
    //     main->freezeTime = false;
    // }
}
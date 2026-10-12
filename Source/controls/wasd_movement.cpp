#include "controls/wasd_movement.hpp"

#include "options.h"

namespace devilution {

namespace {

bool UpHeld;
bool DownHeld;
bool LeftHeld;
bool RightHeld;

bool *HeldFlagForKey(SDL_Keycode key)
{
	switch (key) {
	case 'w':
	case 'W':
		return &UpHeld;
	case 's':
	case 'S':
		return &DownHeld;
	case 'a':
	case 'A':
		return &LeftHeld;
	case 'd':
	case 'D':
		return &RightHeld;
	default:
		return nullptr;
	}
}

} // namespace

bool IsWasdMovementEnabled()
{
	return *GetOptions().Gameplay.wasdMovement;
}

void SetWasdMovementEnabled(bool enabled)
{
	GetOptions().Gameplay.wasdMovement.SetValue(enabled);
	ClearWasdMovementKeys();
}

bool WasdMovementKeyPressed(SDL_Keycode key)
{
	if (!IsWasdMovementEnabled())
		return false;
	bool *held = HeldFlagForKey(key);
	if (held == nullptr)
		return false;
	*held = true;
	return true;
}

bool WasdMovementKeyReleased(SDL_Keycode key)
{
	bool *held = HeldFlagForKey(key);
	if (held == nullptr)
		return false;
	// Always forget the key, even if the option was switched off while it was held.
	*held = false;
	return IsWasdMovementEnabled();
}

void ClearWasdMovementKeys()
{
	UpHeld = false;
	DownHeld = false;
	LeftHeld = false;
	RightHeld = false;
}

AxisDirection GetWasdMovementDirection()
{
	AxisDirection result { AxisDirectionX_NONE, AxisDirectionY_NONE };
	if (!IsWasdMovementEnabled())
		return result;

	if (UpHeld && !DownHeld)
		result.y = AxisDirectionY_UP;
	else if (DownHeld && !UpHeld)
		result.y = AxisDirectionY_DOWN;

	if (LeftHeld && !RightHeld)
		result.x = AxisDirectionX_LEFT;
	else if (RightHeld && !LeftHeld)
		result.x = AxisDirectionX_RIGHT;

	return result;
}

} // namespace devilution

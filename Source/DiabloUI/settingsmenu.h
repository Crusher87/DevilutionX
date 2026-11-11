#pragma once

#include "DiabloUI/diabloui.h"

namespace devilution {

/**
 * @brief The Settings menu.
 * @param inGame POE2 Mashup: opened from the in-game menu, so settings that can't change during a game are hidden.
 */
void UiSettingsMenu(bool inGame = false);

} // namespace devilution

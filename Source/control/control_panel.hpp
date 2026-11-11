#pragma once

#include <optional>

#include "engine/rectangle.hpp"
#include "panels/ui_panels.hpp"

namespace devilution {

extern int TotalSpMainPanelButtons;
extern int TotalMpMainPanelButtons;
extern int PanelPaddingHeight;
extern const char *const PanBtnStr[8];
extern const char *const PanBtnHotKey[8];
extern Rectangle SpellButtonRect;
extern Rectangle BeltRect;

void SetPanelObjectPosition(UiPanels panel, Rectangle &button);

/** @brief Where the level-up button's label is drawn on screen, while the button shows. */
std::optional<Rectangle> LevelButtonLabelArea();

} // namespace devilution

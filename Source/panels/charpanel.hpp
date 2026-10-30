#pragma once

#include <expected>
#include <string>

#include "engine/clx_sprite.hpp"
#include "engine/surface.hpp"

namespace devilution {

extern OptionalOwnedClxSpriteList pChrButtons;

void DrawChr(const Surface &);
std::expected<void, std::string> LoadCharPanel();
void FreeCharPanel();
/** @brief POE2 Mashup: hovering the resistance boxes shows chaos resistance, which has no box of its own, in the info box. */
void CheckCharPanelInfo();

} // namespace devilution

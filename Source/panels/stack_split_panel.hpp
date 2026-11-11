/**
 * @file panels/stack_split_panel.hpp
 *
 * POE2 Mashup stack split: Shift+left-click on a stack opens a slider to pick how many to take onto the cursor, like
 * POE2's (Curtis 2026-10-22). Mouse only; a gamepad still takes half.
 */
#pragma once

#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

/** @brief Where the stack being split lies. */
enum class StackSplitSource : uint8_t {
	Inventory,
	Stash,
};

bool IsStackSplitOpen();
/**
 * @brief Opens the slider for the stack at that index of the bag's or the stash's item list.
 * @return false, and nothing opens, for anything but a stack of two or more, or without a mouse.
 */
bool OpenStackSplit(StackSplitSource source, uint16_t index);
void CloseStackSplit();
/** @brief How many the slider is set to take. */
int StackSplitAmount();
void SetStackSplitAmount(int amount);
/** @brief Puts the chosen amount on the cursor and closes the slider. */
void ConfirmStackSplit();

/** @return true if the event was handled by the slider. */
bool StackSplitHandleEvent(const SDL_Event &event);
void DrawStackSplit(const Surface &out);

} // namespace devilution

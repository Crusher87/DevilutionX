/**
 * @file panels/passive_tree_panel.hpp
 *
 * POE2 Mashup passive tree panel.
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

bool IsPassiveTreeOpen();
void OpenPassiveTree();
void ClosePassiveTree();
void TogglePassiveTree();

/** @return true if the event was handled by the passive tree. */
bool PassiveTreeHandleEvent(const SDL_Event &event);
void DrawPassiveTree(const Surface &out);

/** @brief Opens the tree on a Book just read: a notice band says what it gave (with Cain's words the first time), and its shelf slot lights. */
void ShowBookRead(int book);
/** @brief Called on load: the tree was redrawn and every point refunded, or the hero's points changed when quests became Books. */
void QueueTreeNotice(bool redrawn, bool refunded, int pointChange);
/** @brief Opens the tree with the queued load notice, once the game is running. */
void ShowPendingTreeNotice();

} // namespace devilution

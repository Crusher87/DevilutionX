/**
 * @file panels/quest_reward_panel.hpp
 *
 * POE2 Mashup quest reward popup: a towner's quest reward lands at the hero's feet as a normal ground item, and a
 * small popup shows it with "Put in bag" and "Leave on ground" (Curtis 2026-10-27). The item is on the ground the
 * whole time, so closing the popup any way never loses it. Single player only; mouse first.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

/** @brief What the player did with the popup open. */
enum class QuestRewardInput : uint8_t {
	PutInBagButton,
	LeaveButton,
	/** A click on the popup, but not on a button. */
	InsidePanel,
	/** A click anywhere else. */
	OutsidePanel,
	Enter,
	Escape,
	/** The hero moved away. */
	Moved,
};

/** @brief What the popup does about it. */
enum class QuestRewardAction : uint8_t {
	None,
	PutInBag,
	/** Closes the popup; the item stays on the ground. */
	LeaveOnGround,
};

/** @brief The popup's rule: a full bag turns "Put in bag" off, and anything that leaves closes it. */
QuestRewardAction QuestRewardActionFor(QuestRewardInput input, bool bagFull);

/**
 * @brief Queues the popup for the ground item Items[itemIndex], which the towner just gave the hero. It opens once
 * nothing else is on screen, such as the towner's talk text. POE2 single player only.
 */
void OfferQuestReward(std::string_view towner, int itemIndex);

/** @brief Each game tick: opens a queued reward when the screen is free, and closes the popup if the hero walks off. */
void UpdateQuestRewardOffer();

/** @brief Whether something else on screen, like talk text, a talk menu or another popup, holds the popup back. */
bool QuestRewardHeldBack();

bool IsQuestRewardOpen();
/** @brief How many rewards wait for their popup, not counting an open one. */
size_t PendingQuestRewardCount();
/** @brief Closes the popup; the item stays on the ground. */
void CloseQuestReward();
/** @brief Forgets the popup and every queued reward. */
void ClearQuestRewards();

/** @return true if the event was handled by the popup. */
bool QuestRewardHandleEvent(const SDL_Event &event);
void DrawQuestReward(const Surface &out);

} // namespace devilution

/**
 * @file poe2/gemcutting.hpp
 *
 * POE2 Mashup gemcutting: uncut gems drop instead of skill and support gems, and are cut into the gem you choose.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Player;

/** @brief The tabs of the gemcutting panel, like POE2's weapon tabs. */
enum class GemCategory : uint8_t {
	Mace,
	Axe,
	Sword,
	Bow,
	Staff,
	Shield,
	/** Fire and lightning spells and skills. */
	Elemental,
	/** Magic spells. */
	Occult,
	/** Spirit gems: heralds, auras and minions. */
	Spirit,
	Support,
	All,
	Count,
};

/** @brief A gem that can be cut, from the item table. */
struct GemEntry {
	size_t itemIndex;
	/** The skill the gem grants; empty for a support gem. */
	SkillSlot skill;
	SupportGem support = SupportGem::None;
	/** The level an uncut gem needs to be cut into this gem. */
	int level;
};

std::string_view GemCategoryName(GemCategory category);

/** @brief Every skill, Spirit and support gem, by level. */
const std::vector<GemEntry> &GemCatalog();
bool GemInCategory(const GemEntry &gem, GemCategory category);
std::string_view GemName(const GemEntry &gem);

/** @brief The tier row a gem is listed in: 1, 3, 5, 7, 9, 11 or 13, like POE2's gem tiers. */
int GemTier(int level);
std::string_view GemTierNumeral(int tier);

bool IsUncutGem(const Item &item);
/** @brief Whether the uncut gem is cut into support gems rather than skill gems. */
bool IsUncutSupportGem(const Item &item);
/** @brief The highest gem level the uncut gem can be cut into: its item level. */
int UncutGemLevel(const Item &item);
/** @brief Whether the uncut gem can be cut into the gem. */
bool CanCutGem(const Item &uncut, const GemEntry &gem);
/** @brief Makes a fresh gem item. */
bool CreateGemItem(Item &item, const GemEntry &gem);

} // namespace devilution

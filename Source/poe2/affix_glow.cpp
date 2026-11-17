#include "poe2/affix_glow.hpp"

#include <algorithm>
#include <array>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "items.h"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/desecration.hpp"

namespace devilution {

namespace {

bool SameMod(const Poe2ItemMod &a, const Poe2ItemMod &b)
{
	return a.family == b.family && a.tier == b.tier && a.values == b.values;
}

/**
 * @brief Bit i set for each entry of @p after that no entry of @p before is like: each entry of @p before matches one
 * of @p after at most, so a second copy of a modifier is new too.
 */
template <typename T, size_t N, typename Present, typename Same>
uint32_t NewEntries(const std::array<T, N> &before, const std::array<T, N> &after, Present present, Same same)
{
	std::array<bool, N> used {};
	uint32_t fresh = 0;
	for (size_t i = 0; i < N; i++) {
		if (!present(after[i]))
			continue;
		bool found = false;
		for (size_t j = 0; j < N && !found; j++) {
			if (used[j] || !present(before[j]) || !same(before[j], after[i]))
				continue;
			used[j] = true;
			found = true;
		}
		if (!found)
			fresh |= 1U << i;
	}
	return fresh;
}

/** @brief Desecrated and unrevealed modifiers have lines of their own; the rest show among the item's modifiers. */
bool HasOwnLine(const Poe2CraftMod &mod)
{
	return mod.family == UnrevealedModFamily || (mod.family != 0 && GetDesecratedMod(mod.family) != nullptr);
}

/** @brief The texts of @p after that @p before doesn't have, a repeated one counted as often as it shows. */
void AddNewTexts(std::vector<std::string> before, const std::vector<std::string> &after, std::vector<std::string> &out)
{
	for (const std::string &text : after) {
		const auto it = std::find(before.begin(), before.end(), text);
		if (it != before.end()) {
			before.erase(it);
			continue;
		}
		out.push_back(text);
	}
}

/** @brief The glowing item and what of it was new, so the glow ends once its modifiers change again. */
struct AffixGlow {
	_item_indexes idx;
	uint32_t seed;
	uint16_t createInfo;
	std::array<Poe2ItemMod, MaxRareAffixes> poe2Mods;
	std::array<Poe2CraftMod, MaxCraftMods> craftMods;
	std::array<item_effect_type, MaxRareAffixes> rarePowers;
	item_effect_type prefixPower;
	item_effect_type suffixPower;
	uint32_t start;
	NewAffixLines lines;
};

std::optional<AffixGlow> Glow;

bool IsGlowingItem(const AffixGlow &glow, const Item &item)
{
	if (!item.keyAttributesMatch(glow.seed, glow.idx, glow.createInfo))
		return false;
	for (size_t i = 0; i < MaxRareAffixes; i++) {
		if (!SameMod(item._iPoe2Mods[i], glow.poe2Mods[i]))
			return false;
	}
	return item._iPoe2Craft.mods == glow.craftMods && item._iRarePowers == glow.rarePowers
	    && item._iPrePower == glow.prefixPower && item._iSufPower == glow.suffixPower;
}

} // namespace

bool NewAffixLines::empty() const
{
	return poe2Mods == 0 && craftMods == 0 && rarePowers == 0 && !prefixPower && !suffixPower && lines.empty();
}

bool NewAffixLines::hasLine(std::string_view text) const
{
	return std::find(lines.begin(), lines.end(), text) != lines.end();
}

NewAffixLines FindNewAffixLines(const Item &before, const Item &after)
{
	NewAffixLines fresh;
	fresh.poe2Mods = NewEntries(
	    before._iPoe2Mods, after._iPoe2Mods, [](const Poe2ItemMod &mod) { return mod.family != 0; }, SameMod);
	fresh.craftMods = NewEntries(
	    before._iPoe2Craft.mods, after._iPoe2Craft.mods, HasOwnLine, [](const Poe2CraftMod &a, const Poe2CraftMod &b) { return a == b; });
	// An item from before the POE2 modifiers shows its powers instead.
	if (after._iPoe2Mods[0].family == 0 && !after._iPoe2Craft.explicitMods) {
		fresh.rarePowers = NewEntries(
		    before._iRarePowers, after._iRarePowers, [](item_effect_type power) { return power != IPL_INVALID; },
		    [](item_effect_type a, item_effect_type b) { return a == b; });
		fresh.prefixPower = after._iPrePower != IPL_INVALID && after._iPrePower != before._iPrePower;
		fresh.suffixPower = after._iSufPower != IPL_INVALID && after._iSufPower != before._iSufPower;
	}
	// An Evil essence's mark and a rune, soul core or talisman in a socket are lines of their own.
	AddNewTexts(EssenceMarkText(before), EssenceMarkText(after), fresh.lines);
	AddNewTexts(AugmentSocketText(before), AugmentSocketText(after), fresh.lines);
	return fresh;
}

bool MarkNewAffixes(const Item &before, const Item &after, uint32_t now)
{
	NewAffixLines fresh = FindNewAffixLines(before, after);
	if (fresh.empty())
		return false;
	Glow = AffixGlow {
		after.IDidx,
		after._iSeed,
		after._iCreateInfo,
		after._iPoe2Mods,
		after._iPoe2Craft.mods,
		after._iRarePowers,
		after._iPrePower,
		after._iSufPower,
		now,
		std::move(fresh),
	};
	return true;
}

bool MarkNewAffixes(const Item &before, const Item &after)
{
	return MarkNewAffixes(before, after, static_cast<uint32_t>(SDL_GetTicks()));
}

void ClearAffixGlow()
{
	Glow = std::nullopt;
}

const NewAffixLines *GlowingAffixLines(const Item &item, uint32_t now)
{
	if (!Glow || now - Glow->start >= AffixGlowDurationMs || !IsGlowingItem(*Glow, item))
		return nullptr;
	return &Glow->lines;
}

const NewAffixLines *GlowingAffixLines(const Item &item)
{
	return GlowingAffixLines(item, static_cast<uint32_t>(SDL_GetTicks()));
}

float AffixGlowCurve(uint32_t elapsed)
{
	if (elapsed >= AffixGlowDurationMs)
		return 0.F;
	if (elapsed <= AffixGlowHoldMs)
		return 1.F;
	// Ease-out: it dims quickly at first, then lingers as it settles back to the line's own colour.
	const float t = static_cast<float>(elapsed - AffixGlowHoldMs) / static_cast<float>(AffixGlowDurationMs - AffixGlowHoldMs);
	return (1.F - t) * (1.F - t);
}

float AffixGlowStrength(uint32_t now)
{
	if (!Glow)
		return 0.F;
	return AffixGlowCurve(now - Glow->start);
}

float AffixGlowStrength()
{
	return AffixGlowStrength(static_cast<uint32_t>(SDL_GetTicks()));
}

} // namespace devilution

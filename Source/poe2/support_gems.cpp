#include "poe2/support_gems.hpp"

#include <algorithm>

#include "items.h"
#include "player.h"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

constexpr int MultipleProjectilesExtra = 2;
constexpr int EmpowerLevels = 2;
constexpr int8_t FasterCastingFrames = 3;

bool IsProjectileSpell(SpellID spell)
{
	return IsAnyOf(spell, SpellID::Firebolt, SpellID::Fireball, SpellID::HolyBolt, SpellID::BloodStar,
	    SpellID::Lightning, SpellID::ChargedBolt, SpellID::Elemental, SpellID::BoneSpirit);
}

int ManaCostPercent(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return 150;
	case SupportGem::Empower:
		return 130;
	case SupportGem::FasterCasting:
		return 120;
	case SupportGem::Efficiency:
		return 60;
	default:
		return 100;
	}
}

} // namespace

const std::array<SupportGem, MaxSupportsPerSpell> &Poe2PlayerData::supportsFor(SpellID spell) const
{
	static const std::array<SupportGem, MaxSupportsPerSpell> None {};
	const auto index = static_cast<size_t>(static_cast<int8_t>(spell));
	if (spell == SpellID::Invalid || index >= MaxPoe2Spells)
		return None;
	return spellSupports[index];
}

bool Poe2PlayerData::hasSupport(SpellID spell, SupportGem gem) const
{
	const auto &supports = supportsFor(spell);
	return std::find(supports.begin(), supports.end(), gem) != supports.end();
}

SupportGem SupportGemForMiscId(int miscId)
{
	switch (miscId) {
	case IMISC_SUPPORT_MULTIPROJ:
		return SupportGem::MultipleProjectiles;
	case IMISC_SUPPORT_EMPOWER:
		return SupportGem::Empower;
	case IMISC_SUPPORT_FASTCAST:
		return SupportGem::FasterCasting;
	case IMISC_SUPPORT_EFFICIENCY:
		return SupportGem::Efficiency;
	default:
		return SupportGem::None;
	}
}

SupportGem SupportGemForItem(const Item &item)
{
	if (item.isEmpty())
		return SupportGem::None;
	return SupportGemForMiscId(item._iMiscId);
}

std::string_view SupportGemName(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return _("Multiple Projectiles");
	case SupportGem::Empower:
		return _("Empower");
	case SupportGem::FasterCasting:
		return _("Faster Casting");
	case SupportGem::Efficiency:
		return _("Efficiency");
	default:
		return {};
	}
}

std::string_view SupportGemDescription(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return _("Bolts and balls fire 2 extra\nprojectiles in a spread. Mana +50%");
	case SupportGem::Empower:
		return _("+2 spell level. Mana +30%");
	case SupportGem::FasterCasting:
		return _("Casts much faster. Mana +20%");
	case SupportGem::Efficiency:
		return _("Mana cost -40%");
	default:
		return {};
	}
}

bool CreateSupportGemItem(Item &item, SupportGem gem)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (SupportGemForMiscId(AllItemsList[i].iMiscId) != gem)
			continue;
		InitializeItem(item, static_cast<_item_indexes>(i));
		GenerateNewSeed(item);
		item._iIdentified = true;
		return true;
	}
	return false;
}

bool SupportGemCanSupport(SupportGem gem, SpellID spell)
{
	if (gem == SupportGem::None || spell == SpellID::Invalid)
		return false;
	if (gem == SupportGem::MultipleProjectiles)
		return IsProjectileSpell(spell);
	return true;
}

SupportLinkResult LinkSupportGem(Poe2PlayerData &data, SpellID spell, SupportGem gem, SupportGem &replaced)
{
	replaced = SupportGem::None;
	const auto index = static_cast<size_t>(static_cast<int8_t>(spell));
	if (spell == SpellID::Invalid || index >= MaxPoe2Spells)
		return SupportLinkResult::NoReadiedSpell;
	if (!SupportGemCanSupport(gem, spell))
		return SupportLinkResult::NotApplicable;
	if (data.hasSupport(spell, gem))
		return SupportLinkResult::AlreadyLinked;

	auto &supports = data.spellSupports[index];
	for (SupportGem &slot : supports) {
		if (slot == SupportGem::None) {
			slot = gem;
			return SupportLinkResult::Linked;
		}
	}

	// Both slots are full: the oldest support makes room, like swapping a gem out in POE2.
	replaced = supports[0];
	std::rotate(supports.begin(), supports.begin() + 1, supports.end());
	supports.back() = gem;
	return SupportLinkResult::Replaced;
}

int SupportSpellLevelBonus(const Player &player, SpellID spell)
{
	return player.poe2.hasSupport(spell, SupportGem::Empower) ? EmpowerLevels : 0;
}

int SupportManaCostPercent(const Player &player, SpellID spell)
{
	int percent = 100;
	for (const SupportGem gem : player.poe2.supportsFor(spell))
		percent = percent * ManaCostPercent(gem) / 100;
	return percent;
}

int8_t SupportCastSkippedFrames(const Player &player, SpellID spell)
{
	return player.poe2.hasSupport(spell, SupportGem::FasterCasting) ? FasterCastingFrames : 0;
}

int SupportExtraProjectiles(const Player &player, SpellID spell)
{
	if (!IsProjectileSpell(spell))
		return 0;
	return player.poe2.hasSupport(spell, SupportGem::MultipleProjectiles) ? MultipleProjectilesExtra : 0;
}

} // namespace devilution

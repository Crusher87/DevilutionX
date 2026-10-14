#include "poe2/gemcutting.hpp"

#include <algorithm>

#include "items.h"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/table_skills.hpp"
#include "spells.h"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

bool AttackInCategory(WeaponSkill skill, GemCategory category)
{
	if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
		return category < GemCategory::Spirit && (table->tabs & (1 << static_cast<int>(category))) != 0;
	switch (category) {
	case GemCategory::Mace:
		return IsAnyOf(skill, WeaponSkill::GroundSlam, WeaponSkill::LeapSlam, WeaponSkill::BattleCry, WeaponSkill::Smite);
	case GemCategory::Axe:
		return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::LeapSlam, WeaponSkill::BattleCry, WeaponSkill::Frenzy);
	case GemCategory::Sword:
		return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::Flurry, WeaponSkill::LeapSlam, WeaponSkill::BattleCry, WeaponSkill::Riposte, WeaponSkill::ShadowStep,
		    WeaponSkill::Frenzy);
	case GemCategory::Bow:
		return IsAnyOf(skill, WeaponSkill::Barrage, WeaponSkill::RainOfArrows, WeaponSkill::LightningArrow, WeaponSkill::EscapeShot, WeaponSkill::RapidFire);
	case GemCategory::Staff:
		return IsAnyOf(skill, WeaponSkill::WhirlingStaff, WeaponSkill::LeapSlam, WeaponSkill::FistsOfThunder);
	case GemCategory::Shield:
		return IsAnyOf(skill, WeaponSkill::ShieldCharge, WeaponSkill::ShieldBash);
	case GemCategory::Elemental:
		return IsAnyOf(skill, WeaponSkill::EmberFusillade, WeaponSkill::Spark, WeaponSkill::LightningArrow, WeaponSkill::IceBolt, WeaponSkill::FrostNova,
		    WeaponSkill::FistsOfThunder);
	case GemCategory::Occult:
		return skill == WeaponSkill::AmplifyDamage;
	default:
		return false;
	}
}

} // namespace

std::string_view GemCategoryName(GemCategory category)
{
	switch (category) {
	case GemCategory::Mace:
		return _("Mace");
	case GemCategory::Axe:
		return _("Axe");
	case GemCategory::Sword:
		return _("Sword");
	case GemCategory::Bow:
		return _("Bow");
	case GemCategory::Staff:
		return _("Staff");
	case GemCategory::Shield:
		return _("Shield");
	case GemCategory::Elemental:
		return _("Elemental");
	case GemCategory::Occult:
		return _("Occult");
	case GemCategory::Spirit:
		return _("Spirit");
	case GemCategory::Support:
		return _("Support");
	default:
		return _("All Gems");
	}
}

const std::vector<GemEntry> &GemCatalog()
{
	static std::vector<GemEntry> catalog;
	static size_t catalogItemCount = 0;
	// The item table is reloaded when mods change, so rebuild when it does.
	if (catalogItemCount == AllItemsList.size() && !catalog.empty())
		return catalog;
	catalog.clear();
	catalogItemCount = AllItemsList.size();
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		GemEntry entry { i, SkillForGemData(data), SupportGemForItemData(data), std::max<int>(data.iMinMLvl, 1) };
		if (entry.skill.isEmpty() && entry.support == SupportGem::None)
			continue;
		// Lineage supports are found, never cut.
		if (IsLineageSupport(entry.support))
			continue;
		if (entry.support != SupportGem::None) {
			// One entry per tier, each cut from its own uncut support gem level.
			const int baseLevel = entry.level;
			for (int tier = 1; tier <= SupportTierCount(entry.support); tier++) {
				entry.tier = tier;
				entry.level = SupportTierGemLevel(entry.support, tier, baseLevel);
				catalog.push_back(entry);
			}
			continue;
		}
		catalog.push_back(entry);
	}
	std::stable_sort(catalog.begin(), catalog.end(), [](const GemEntry &a, const GemEntry &b) { return a.level < b.level; });
	return catalog;
}

bool GemInCategory(const GemEntry &gem, GemCategory category)
{
	if (category == GemCategory::All)
		return true;
	if (gem.support != SupportGem::None)
		return category == GemCategory::Support;
	switch (gem.skill.kind) {
	case SkillKind::Persistent:
		return category == GemCategory::Spirit;
	case SkillKind::Attack:
		return AttackInCategory(gem.skill.attack(), category);
	case SkillKind::Spell: {
		const MagicType type = GetSpellData(gem.skill.spell()).type();
		return category == (type == MagicType::Magic ? GemCategory::Occult : GemCategory::Elemental);
	}
	default:
		return false;
	}
}

std::string GemName(const GemEntry &gem)
{
	if (gem.support != SupportGem::None)
		return SupportGemTierName(gem.support, gem.tier);
	return std::string(SkillName(gem.skill));
}

int GemTier(int level)
{
	constexpr int Bounds[] = { 3, 6, 10, 15, 20, 25 };
	int tier = 1;
	for (const int bound : Bounds) {
		if (level < bound)
			break;
		tier += 2;
	}
	return tier;
}

std::string_view GemTierNumeral(int tier)
{
	switch (tier) {
	case 1:
		return "I";
	case 3:
		return "III";
	case 5:
		return "V";
	case 7:
		return "VII";
	case 9:
		return "IX";
	case 11:
		return "XI";
	default:
		return "XIII";
	}
}

bool IsUncutGem(const Item &item)
{
	return !item.isEmpty() && IsAnyOf(item._iMiscId, IMISC_UNCUT_SKILL, IMISC_UNCUT_SUPPORT, IMISC_UNCUT_SPIRIT);
}

UncutKind UncutGemKind(const Item &item)
{
	switch (item._iMiscId) {
	case IMISC_UNCUT_SUPPORT:
		return UncutKind::Support;
	case IMISC_UNCUT_SPIRIT:
		return UncutKind::Spirit;
	default:
		return UncutKind::Skill;
	}
}

UncutKind GemUncutKind(const GemEntry &gem)
{
	if (gem.support != SupportGem::None)
		return UncutKind::Support;
	return gem.skill.kind == SkillKind::Persistent ? UncutKind::Spirit : UncutKind::Skill;
}

std::string_view UncutKindName(UncutKind kind)
{
	switch (kind) {
	case UncutKind::Support:
		return _("support");
	case UncutKind::Spirit:
		return _("Spirit");
	default:
		return _("skill");
	}
}

bool IsUncutSupportGem(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_UNCUT_SUPPORT;
}

int UncutGemLevel(const Item &item)
{
	return std::max(item._iCreateInfo & CF_LEVEL, 1);
}

bool CanCutGem(const Item &uncut, const GemEntry &gem)
{
	if (!IsUncutGem(uncut))
		return false;
	// POE2: uncut skill, Spirit and support gems each cut only their own kind of gem.
	if (UncutGemKind(uncut) != GemUncutKind(gem))
		return false;
	// Any gem in the uncut gem's tier or below: a level 1 uncut gem cuts every tier I gem.
	return GemTier(gem.level) <= GemTier(UncutGemLevel(uncut));
}

bool CreateGemItem(Item &item, const GemEntry &gem, int level)
{
	if (gem.support != SupportGem::None)
		return CreateSupportGemItem(item, gem.support, gem.tier);
	return CreateSkillGemItem(item, gem.skill, level);
}

int GemLevelFromUncut(const Item &uncut, int characterLevel)
{
	return std::min({ UncutGemLevel(uncut), MaxGemLevel, MaxGemLevelForCharacter(characterLevel) });
}

} // namespace devilution

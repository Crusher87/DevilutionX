#include "poe2/gemcutting.hpp"

#include <algorithm>

#include "items.h"
#include "poe2/meta_gems.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/table_skills.hpp"
#include "spells.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

bool AttackInListedCategory(WeaponSkill skill, GemCategory category)
{
	if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
		return category < GemCategory::Spirit && (table->tabs & (1 << static_cast<int>(category))) != 0;
	switch (category) {
	case GemCategory::Mace:
		return IsAnyOf(skill, WeaponSkill::GroundSlam, WeaponSkill::LeapSlam, WeaponSkill::BattleCry, WeaponSkill::Smite);
	case GemCategory::Axe:
		return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::LeapSlam, WeaponSkill::BattleCry, WeaponSkill::Frenzy);
	case GemCategory::Sword:
		return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::Flurry, WeaponSkill::LeapSlam, WeaponSkill::BattleCry, WeaponSkill::Riposte, WeaponSkill::Frenzy);
	case GemCategory::Dagger:
		// Shadow Lunge is a dagger skill; Double Swing and Cleave work with a dagger too (the 10-22 plan).
		return IsAnyOf(skill, WeaponSkill::ShadowStep, WeaponSkill::Flurry, WeaponSkill::Cleave);
	case GemCategory::Bow:
		return IsAnyOf(skill, WeaponSkill::Barrage, WeaponSkill::RainOfArrows, WeaponSkill::LightningArrow, WeaponSkill::EscapeShot, WeaponSkill::RapidFire);
	case GemCategory::Staff:
		return IsAnyOf(skill, WeaponSkill::WhirlingStaff, WeaponSkill::LeapSlam, WeaponSkill::FistsOfThunder);
	case GemCategory::Shield:
		// Counterstrike answers a block: it shows with the shield skills too (Q41).
		return IsAnyOf(skill, WeaponSkill::ShieldCharge, WeaponSkill::ShieldBash, WeaponSkill::Riposte);
	case GemCategory::Elemental:
		return IsAnyOf(skill, WeaponSkill::EmberFusillade, WeaponSkill::Spark, WeaponSkill::LightningArrow, WeaponSkill::IceBolt, WeaponSkill::FrostNova,
		    WeaponSkill::FistsOfThunder);
	case GemCategory::Occult:
		return skill == WeaponSkill::AmplifyDamage;
	default:
		return false;
	}
}

/** @brief A weapon's skill is listed only under its weapon tabs, not Elemental or Occult too (Curtis 2026-10-17). */
bool AttackInCategory(WeaponSkill skill, GemCategory category)
{
	if (!AttackInListedCategory(skill, category))
		return false;
	if (!IsAnyOf(category, GemCategory::Elemental, GemCategory::Occult, GemCategory::Arcane))
		return true;
	return !AttackOnWeaponTab(skill);
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
	case GemCategory::Dagger:
		return _("Dagger");
	case GemCategory::Bow:
		return _("Bow");
	case GemCategory::Staff:
		// Its gems need a martial staff since the caster weapons rework (plan 6.15).
		return _("Quarterstaff");
	case GemCategory::Shield:
		return _("Shield");
	case GemCategory::Elemental:
		return _("Elemental");
	case GemCategory::Occult:
		return _("Occult");
	case GemCategory::Arcane:
		return _("Arcane");
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
		// Lineage supports are found, never cut, the old trigger supports are meta gems now, and the crossbow skills are gone.
		if (IsLineageSupport(entry.support) || IsRetiredSupport(entry.support) || IsRetiredSkill(entry.skill))
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
	case SkillKind::Spell:
		return category == SpellGemCategory(gem.skill.spell());
	default:
		return false;
	}
}

GemCategory SpellGemCategory(SpellID spell)
{
	switch (spell) {
	case SpellID::Nova:
		// A ring of lightning (Curtis 2026-11-07, Q30).
		return GemCategory::Elemental;
	case SpellID::Elemental:
		// Moved to Occult at tier XI to spread the Elemental tab (Curtis 2026-11-07, Q30).
		return GemCategory::Occult;
	case SpellID::Healing:
	case SpellID::ManaShield:
	case SpellID::Phasing:
	case SpellID::Teleport:
	case SpellID::Warp:
	case SpellID::Etherealize:
	case SpellID::RuneOfFire:
	case SpellID::RuneOfLight:
	case SpellID::RuneOfStone:
	case SpellID::RuneOfNova:
	case SpellID::RuneOfImmolation:
		// The Arcane tab (Curtis 2026-11-06, Tree Workshop).
		return GemCategory::Arcane;
	default:
		return GetSpellData(spell).type() == MagicType::Magic ? GemCategory::Occult : GemCategory::Elemental;
	}
}

bool IsRetiredSkill(SkillSlot skill)
{
	switch (skill.kind) {
	case SkillKind::Attack:
		// Rapid Fire, Impale and Ballista Barrage repeat Hail of Arrows, Guided Arrow and Arrow Storm; traps and turrets need
		// no bow. Arcing Sparks is part of Charged Bolt now, and Hellfire Darts repeats Firebolt with Sisters' Spread
		// (Curtis 2026-11-07, Q34).
		return IsAnyOf(skill.attack(), WeaponSkill::RapidFire, WeaponSkill::Impale, WeaponSkill::SpikeTrap, WeaponSkill::Sentry, WeaponSkill::SiegeVolley,
		    WeaponSkill::Spark, WeaponSkill::EmberFusillade);
	case SkillKind::Spell:
		// Guardian, Golem and Reflect copy Spirit gems (Q33), Ring of Fire is Fire Wall cast at your feet (Q34), and Berserk
		// is part of Confuse (Q36).
		return IsAnyOf(skill.spell(), SpellID::Guardian, SpellID::Golem, SpellID::Reflect, SpellID::RingOfFire, SpellID::Berserk);
	default:
		return false;
	}
}

bool AttackOnWeaponTab(WeaponSkill skill)
{
	for (int weapon = 0; weapon < static_cast<int>(GemCategory::Elemental); weapon++) {
		if (AttackInListedCategory(skill, static_cast<GemCategory>(weapon)))
			return true;
	}
	return false;
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

std::vector<GemCategory> GemcuttingTabs(std::optional<UncutKind> cutting)
{
	std::vector<GemCategory> tabs;
	for (size_t i = 0; i < static_cast<size_t>(GemCategory::Count); i++) {
		const auto category = static_cast<GemCategory>(i);
		bool shown = true;
		if (cutting) {
			switch (category) {
			case GemCategory::Spirit:
				shown = *cutting == UncutKind::Spirit;
				break;
			case GemCategory::Support:
				shown = *cutting == UncutKind::Support;
				break;
			default:
				// A Spirit or support gem's own tab holds all it can become, so it needs no All Gems tab either.
				shown = *cutting == UncutKind::Skill;
				break;
			}
		}
		if (shown)
			tabs.push_back(category);
	}
	return tabs;
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

int UncutGemLevelForArea(int areaLevel)
{
	// POE2's level 2 gem needs character level 3 and first drops at area level 4, level 3 needs 6 and drops at 7, ...
	return MaxGemLevelForCharacter(std::max(areaLevel - 1, 1));
}

void SetUncutGemLevel(Item &item, int level)
{
	item._iCreateInfo = static_cast<uint16_t>((item._iCreateInfo & ~CF_LEVEL) | std::clamp(level, 1, MaxGemLevel));
}

int GemLevelFromUncut(const Item &uncut, int characterLevel)
{
	return std::min({ UncutGemLevel(uncut), MaxGemLevel, MaxGemLevelForCharacter(characterLevel) });
}

int UncutLevelToCut(const GemEntry &gem)
{
	// The first level of the gem's tier: any uncut gem of that tier cuts it.
	int level = 1;
	while (level < gem.level && GemTier(level) < GemTier(gem.level))
		level++;
	return level;
}

std::string GemCutLabel(const GemEntry &gem, const Item *uncut, int characterLevel)
{
	if (uncut == nullptr || !CanCutGem(*uncut, gem))
		return FormatRuntime(_("Needs uncut level {:d}"), UncutLevelToCut(gem));
	// A support gem has no level of its own.
	if (gem.support != SupportGem::None)
		return std::string(_("Can cut"));
	return FormatRuntime(_("Cuts to level {:d}"), GemLevelFromUncut(*uncut, characterLevel));
}

} // namespace devilution

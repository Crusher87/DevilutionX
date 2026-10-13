#include "poe2/skill_gems.hpp"

#include "items.h"
#include "levels/gendung.h"
#include "player.h"
#include "poe2/spirit.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "utils/language.h"

namespace devilution {

SkillSlot SkillForGemData(const ItemData &data)
{
	switch (data.iMiscId) {
	case IMISC_SKILLGEM_SPELL:
		return IsValidSpell(data.iSpell) ? SkillSlot::ForSpell(data.iSpell) : SkillSlot {};
	case IMISC_SKILLGEM_CLEAVE:
		return SkillSlot::ForAttack(WeaponSkill::Cleave);
	case IMISC_SKILLGEM_GROUNDSLAM:
		return SkillSlot::ForAttack(WeaponSkill::GroundSlam);
	case IMISC_SKILLGEM_BARRAGE:
		return SkillSlot::ForAttack(WeaponSkill::Barrage);
	case IMISC_SKILLGEM_WHIRL:
		return SkillSlot::ForAttack(WeaponSkill::WhirlingStaff);
	case IMISC_SKILLGEM_FLURRY:
		return SkillSlot::ForAttack(WeaponSkill::Flurry);
	case IMISC_SKILLGEM_LEAPSLAM:
		return SkillSlot::ForAttack(WeaponSkill::LeapSlam);
	case IMISC_SKILLGEM_RAINOFARROWS:
		return SkillSlot::ForAttack(WeaponSkill::RainOfArrows);
	case IMISC_SKILLGEM_SHIELDCHARGE:
		return SkillSlot::ForAttack(WeaponSkill::ShieldCharge);
	case IMISC_SKILLGEM_BATTLECRY:
		return SkillSlot::ForAttack(WeaponSkill::BattleCry);
	case IMISC_SKILLGEM_LIGHTNINGARROW:
		return SkillSlot::ForAttack(WeaponSkill::LightningArrow);
	case IMISC_SKILLGEM_ESCAPESHOT:
		return SkillSlot::ForAttack(WeaponSkill::EscapeShot);
	case IMISC_SKILLGEM_EMBERFUSILLADE:
		return SkillSlot::ForAttack(WeaponSkill::EmberFusillade);
	case IMISC_SKILLGEM_SPARK:
		return SkillSlot::ForAttack(WeaponSkill::Spark);
	case IMISC_SPIRITGEM_HERALDASH:
		return SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh);
	case IMISC_SPIRITGEM_HERALDTHUNDER:
		return SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder);
	case IMISC_SPIRITGEM_PURITY:
		return SkillSlot::ForPersistent(PersistentSkill::PurityOfElements);
	case IMISC_SPIRITGEM_GOLEM:
		return SkillSlot::ForPersistent(PersistentSkill::SummonGolem);
	default:
		return {};
	}
}

SkillSlot SkillForGem(const Item &item)
{
	if (item.isEmpty())
		return {};
	return SkillForGemData(AllItemsList[item.IDidx]);
}

bool IsSkillGem(const Item &item)
{
	return !SkillForGem(item).isEmpty();
}

std::optional<size_t> SkillGemItemIndex(SkillSlot skill)
{
	if (skill.isEmpty())
		return std::nullopt;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (SkillForGemData(AllItemsList[i]) == skill)
			return i;
	}
	return std::nullopt;
}

bool CreateSkillGemItem(Item &item, SkillSlot skill)
{
	const std::optional<size_t> index = SkillGemItemIndex(skill);
	if (!index)
		return false;
	InitializeItem(item, static_cast<_item_indexes>(*index));
	GenerateNewSeed(item);
	item._iIdentified = true;
	return true;
}

std::string_view SkillName(SkillSlot skill)
{
	switch (skill.kind) {
	case SkillKind::Spell:
		return pgettext("spell", GetSpellData(skill.spell()).sNameText);
	case SkillKind::Attack:
		return WeaponSkillName(skill.attack());
	case SkillKind::Persistent:
		return PersistentSkillName(skill.persistent());
	default:
		return {};
	}
}

int SkillManaCost(const Player &player, SkillSlot skill)
{
	switch (skill.kind) {
	case SkillKind::Spell:
		return GetManaAmount(player, skill.spell()).whole();
	case SkillKind::Attack:
		return WeaponSkillManaCost(player, skill.attack());
	default:
		return 0;
	}
}

SkillUsability GetSkillUsability(const Player &player, SkillSlot skill)
{
	const std::optional<size_t> gemIndex = SkillGemItemIndex(skill);
	if (!gemIndex)
		return SkillUsability::Empty;

	const ItemData &gem = AllItemsList[*gemIndex];
	if (player._pStrength < gem.iMinStr || player._pMagic < gem.iMinMag || player._pDexterity < gem.iMinDex)
		return SkillUsability::Requirements;
	if (skill.kind == SkillKind::Persistent)
		return IsPersistentSkillActive(player, skill.persistent()) ? SkillUsability::Ready : SkillUsability::Spirit;
	if (skill.kind == SkillKind::Attack && !WeaponAllowsSkill(player, skill.attack()))
		return SkillUsability::Weapon;
	if (skill.kind == SkillKind::Spell && leveltype == DTYPE_TOWN && !GetSpellData(skill.spell()).isAllowedInTown())
		return SkillUsability::Town;
	if (player._pMana.whole() < SkillManaCost(player, skill))
		return SkillUsability::Mana;
	return SkillUsability::Ready;
}

std::optional<size_t> FirstFreeSkillSlot(const Player &player)
{
	return FindSkillSlot(player, {});
}

std::optional<size_t> FindSkillSlot(const Player &player, SkillSlot skill)
{
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		if (player.poe2.skillSlots[slot] == skill)
			return slot;
	}
	return std::nullopt;
}

SkillSlot SocketSkill(Player &player, size_t slot, SkillSlot skill)
{
	const SkillSlot previous = player.poe2.skillSlots[slot];
	player.poe2.skillSlots[slot] = skill;
	// Spirit gems can change stats, like Purity of Elements' resistances.
	if (previous.kind == SkillKind::Persistent || skill.kind == SkillKind::Persistent)
		CalcPlrInv(player, false);
	return previous;
}

void GrantStarterSkill(Player &player)
{
	SkillSlot skill;
	switch (player._pClass) {
	case HeroClass::Warrior:
		skill = SkillSlot::ForAttack(WeaponSkill::Cleave);
		break;
	case HeroClass::Rogue:
		skill = SkillSlot::ForAttack(WeaponSkill::Barrage);
		break;
	case HeroClass::Sorcerer:
		skill = SkillSlot::ForSpell(SpellID::Firebolt);
		break;
	case HeroClass::Monk:
		skill = SkillSlot::ForAttack(WeaponSkill::WhirlingStaff);
		break;
	case HeroClass::Bard:
		skill = SkillSlot::ForAttack(WeaponSkill::Flurry);
		break;
	case HeroClass::Barbarian:
		skill = SkillSlot::ForAttack(WeaponSkill::GroundSlam);
		break;
	default:
		return;
	}
	if (FindSkillSlot(player, skill))
		return;
	if (const std::optional<size_t> slot = FirstFreeSkillSlot(player))
		SocketSkill(player, *slot, skill);
}

} // namespace devilution

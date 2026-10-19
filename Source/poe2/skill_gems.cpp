#include "poe2/skill_gems.hpp"

#include <algorithm>
#include <array>
#include <bit>

#include "items.h"
#include "levels/gendung.h"
#include "player.h"
#include "poe2/default_attack.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/spirit.hpp"
#include "poe2/table_skills.hpp"
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
	case IMISC_SKILLGEM_ICEBOLT:
		return SkillSlot::ForAttack(WeaponSkill::IceBolt);
	case IMISC_SKILLGEM_FROSTNOVA:
		return SkillSlot::ForAttack(WeaponSkill::FrostNova);
	case IMISC_SKILLGEM_RIPOSTE:
		return SkillSlot::ForAttack(WeaponSkill::Riposte);
	case IMISC_SKILLGEM_SHADOWSTEP:
		return SkillSlot::ForAttack(WeaponSkill::ShadowStep);
	case IMISC_SKILLGEM_FRENZY:
		return SkillSlot::ForAttack(WeaponSkill::Frenzy);
	case IMISC_SKILLGEM_SMITE:
		return SkillSlot::ForAttack(WeaponSkill::Smite);
	case IMISC_SKILLGEM_FISTSOFTHUNDER:
		return SkillSlot::ForAttack(WeaponSkill::FistsOfThunder);
	case IMISC_SKILLGEM_RAPIDFIRE:
		return SkillSlot::ForAttack(WeaponSkill::RapidFire);
	case IMISC_SKILLGEM_SHIELDBASH:
		return SkillSlot::ForAttack(WeaponSkill::ShieldBash);
	case IMISC_SKILLGEM_AMPLIFYDAMAGE:
		return SkillSlot::ForAttack(WeaponSkill::AmplifyDamage);
	case IMISC_SKILLGEM_TABLE:
		if (const TableSkillData *table = FindTableSkillGem(data); table != nullptr)
			return SkillSlot::ForAttack(table->skill);
		return {};
	case IMISC_SPIRITGEM_HERALDFROST:
		return SkillSlot::ForPersistent(PersistentSkill::HeraldOfFrost);
	case IMISC_SPIRITGEM_SKELETALMAGES:
		return SkillSlot::ForPersistent(PersistentSkill::SkeletalMages);
	case IMISC_SPIRITGEM_HERALDASH:
		return SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh);
	case IMISC_SPIRITGEM_HERALDTHUNDER:
		return SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder);
	case IMISC_SPIRITGEM_PURITY:
		return SkillSlot::ForPersistent(PersistentSkill::PurityOfElements);
	case IMISC_SPIRITGEM_GOLEM:
		return SkillSlot::ForPersistent(PersistentSkill::SummonGolem);
	case IMISC_SPIRITGEM_SKELETONS:
		return SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors);
	case IMISC_SPIRITGEM_TABLE: {
		// The later Spirit gems are told apart by name ("Blood Golem Gem").
		const std::string_view name = data.iName;
		for (int i = static_cast<int>(PersistentSkill::BloodGolem); i <= static_cast<int>(PersistentSkill::Last); i++) {
			const auto skill = static_cast<PersistentSkill>(i);
			const std::string_view skillName = PersistentSkillName(skill);
			if (name.size() == skillName.size() + 4 && name.starts_with(skillName) && name.ends_with(" Gem"))
				return SkillSlot::ForPersistent(skill);
		}
		return {};
	}
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

bool CreateSkillGemItem(Item &item, SkillSlot skill, int level)
{
	const std::optional<size_t> index = SkillGemItemIndex(skill);
	if (!index)
		return false;
	InitializeItem(item, static_cast<_item_indexes>(*index));
	GenerateNewSeed(item);
	item._iIdentified = true;
	SetSkillGemItemLevel(item, level);
	return true;
}

int SkillGemItemLevel(const Item &item)
{
	constexpr int Shift = std::countr_zero(static_cast<uint32_t>(CF_POE2_GEM_LEVEL));
	return std::clamp(static_cast<int>((item.dwBuff & CF_POE2_GEM_LEVEL) >> Shift), 1, MaxGemLevel);
}

void SetSkillGemItemLevel(Item &item, int level)
{
	constexpr int Shift = std::countr_zero(static_cast<uint32_t>(CF_POE2_GEM_LEVEL));
	item.dwBuff = (item.dwBuff & ~CF_POE2_GEM_LEVEL) | (static_cast<uint32_t>(std::clamp(level, 1, MaxGemLevel)) << Shift);
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
	case SkillKind::Default:
		return DefaultAttackName(static_cast<DefaultAttack>(skill.id));
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
	// The weapon's own attack needs no gem and no mana.
	if (skill.kind == SkillKind::Default)
		return SkillUsability::Ready;
	const std::optional<size_t> gemIndex = SkillGemItemIndex(skill);
	if (!gemIndex)
		return SkillUsability::Empty;

	const ItemData &gem = AllItemsList[*gemIndex];
	if (RequirementStrength(player) < gem.iMinStr || player._pMagic < gem.iMinMag || player._pDexterity < gem.iMinDex)
		return SkillUsability::Requirements;
	if (skill.kind == SkillKind::Persistent) {
		// A minion count set to 0 is the player's choice, not a lack of Spirit.
		if (IsMinionCountSkill(skill.persistent()) && ChosenMinionCount(player, skill.persistent()) == 0)
			return SkillUsability::Ready;
		return IsPersistentSkillActive(player, skill.persistent()) ? SkillUsability::Ready : SkillUsability::Spirit;
	}
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

SkillSlot SocketSkill(Player &player, size_t slot, SkillSlot skill, int level)
{
	const SkillSlot previous = player.poe2.skillSlots[slot];
	player.poe2.skillSlots[slot] = skill;
	player.poe2.slotLevels[slot] = static_cast<uint8_t>(std::clamp(level, 1, MaxGemLevel));
	// Curtis 2026-10-19: a Spirit gem starts switched off; a right-click in the Skills panel switches it on.
	if (previous != skill)
		player.poe2.spiritGemsOn &= static_cast<uint16_t>(~(1U << slot));
	if (skill.isEmpty() || skill.kind == SkillKind::Persistent) {
		for (size_t traySlot = 0; traySlot < NumTraySlots; traySlot++) {
			if (player.poe2.tray[traySlot] == TrayBinding::ForGem(slot))
				ClearTraySlot(player, static_cast<TraySlot>(traySlot));
		}
	}
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
	if (const std::optional<size_t> slot = FirstFreeSkillSlot(player)) {
		SocketSkill(player, *slot, skill);
		AutoBindSocket(player, *slot);
	}
}

bool TraySlotTakesActions(TraySlot slot)
{
	return slot == TraySlot::LeftMouse || slot == TraySlot::RightMouse;
}

bool TrayBindingFits(TrayBinding binding, TraySlot slot)
{
	return binding.isGem() || binding.action == TrayAction::None || binding.action == TrayAction::WeaponAttack || TraySlotTakesActions(slot);
}

std::optional<TraySlot> TraySlotForSocket(const Player &player, size_t socket)
{
	for (size_t slot = 0; slot < NumTraySlots; slot++) {
		if (player.poe2.tray[slot] == TrayBinding::ForGem(socket))
			return static_cast<TraySlot>(slot);
	}
	return std::nullopt;
}

SkillSlot TraySkill(const Player &player, TraySlot slot)
{
	const TrayBinding binding = player.poe2.tray[static_cast<size_t>(slot)];
	if (!binding.isGem() || binding.socket >= NumSkillSlots)
		return {};
	return player.poe2.skillSlots[binding.socket];
}

void BindTraySlot(Player &player, TraySlot slot, TrayBinding binding)
{
	if (binding.action == TrayAction::None) {
		ClearTraySlot(player, slot);
		return;
	}
	if (!TrayBindingFits(binding, slot))
		return;
	TrayBinding &target = player.poe2.tray[static_cast<size_t>(slot)];
	if (binding.isGem()) {
		if (const std::optional<TraySlot> other = TraySlotForSocket(player, binding.socket); other && *other != slot) {
			TrayBinding &otherBinding = player.poe2.tray[static_cast<size_t>(*other)];
			otherBinding = target;
			// An empty slot, or an action a key can't take, leaves the other slot at its default.
			if (otherBinding.action == TrayAction::None || !TrayBindingFits(otherBinding, *other))
				otherBinding = DefaultTrayBinding(*other);
		}
	}
	target = binding;
}

void ClearTraySlot(Player &player, TraySlot slot)
{
	player.poe2.tray[static_cast<size_t>(slot)] = DefaultTrayBinding(slot);
}

std::optional<TraySlot> AutoBindSocket(Player &player, size_t socket)
{
	const SkillSlot skill = player.poe2.skillSlots[socket];
	if (!SkillGoesOnTray(skill))
		return std::nullopt;
	if (const std::optional<TraySlot> bound = TraySlotForSocket(player, socket))
		return bound;
	for (size_t key = 0; key <= NumTrayKeys; key++) {
		const TraySlot slot = key < NumTrayKeys ? TrayKeySlot(key) : TraySlot::MiddleMouse;
		if (player.poe2.tray[static_cast<size_t>(slot)].action == TrayAction::None) {
			player.poe2.tray[static_cast<size_t>(slot)] = TrayBinding::ForGem(socket);
			return slot;
		}
	}
	return std::nullopt;
}

bool SkillGoesOnTray(SkillSlot skill)
{
	return !skill.isEmpty() && (skill.kind != SkillKind::Persistent || IsInvocationGem(skill));
}

void MigrateOldSkillBar(Player &player)
{
	// The old bar's keys were E, R, T, F, G and V; the tray's are Q, E, R, T, F and G.
	constexpr std::array<TraySlot, OldNumSkillSlots> NewSlots { TraySlot::Key2, TraySlot::Key3, TraySlot::Key4, TraySlot::Key5, TraySlot::Key6, TraySlot::Key1 };
	for (size_t socket = 0; socket < OldNumSkillSlots; socket++) {
		const SkillSlot skill = player.poe2.skillSlots[socket];
		if (!skill.isEmpty() && skill.kind != SkillKind::Persistent)
			player.poe2.tray[static_cast<size_t>(NewSlots[socket])] = TrayBinding::ForGem(socket);
	}
}

} // namespace devilution

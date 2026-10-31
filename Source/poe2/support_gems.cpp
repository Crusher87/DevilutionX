#include "poe2/support_gems.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "engine/displacement.hpp"
#include "engine/random.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

constexpr int EmpowerLevels = 2;

bool IsProjectileSpell(SpellID spell)
{
	return IsAnyOf(spell, SpellID::Firebolt, SpellID::Fireball, SpellID::HolyBolt, SpellID::BloodStar,
	    SpellID::Lightning, SpellID::ChargedBolt, SpellID::Elemental, SpellID::BoneSpirit);
}

/** @brief Spells whose cast projectile deals the damage itself, so it can pierce, fork and chain. */
bool IsDirectProjectileSpell(SpellID spell)
{
	return spell != SpellID::Lightning && IsProjectileSpell(spell);
}

bool IsMeleeAttackSkill(WeaponSkill skill)
{
	if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
		return IsTableMeleeSkill(*table);
	return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::GroundSlam, WeaponSkill::WhirlingStaff, WeaponSkill::Flurry, WeaponSkill::LeapSlam, WeaponSkill::ShieldCharge,
	    WeaponSkill::Riposte, WeaponSkill::ShadowStep, WeaponSkill::Frenzy, WeaponSkill::Smite, WeaponSkill::FistsOfThunder, WeaponSkill::ShieldBash);
}

/** @brief Attack skills that use the normal attack animation, so they can attack faster. */
bool IsQuickenableAttackSkill(WeaponSkill skill)
{
	return IsAnyOf(skill, WeaponSkill::Cleave, WeaponSkill::GroundSlam, WeaponSkill::WhirlingStaff, WeaponSkill::Flurry,
	    WeaponSkill::Barrage, WeaponSkill::RainOfArrows, WeaponSkill::LightningArrow, WeaponSkill::Frenzy, WeaponSkill::Smite,
	    WeaponSkill::FistsOfThunder, WeaponSkill::ShieldBash);
}

bool IsProjectileSupport(SupportGem gem)
{
	return IsAnyOf(gem, SupportGem::Pierce, SupportGem::Fork, SupportGem::Chain, SupportGem::ConcentratedEffect, SupportGem::FasterProjectiles);
}

/** @brief Fires a copy of the projectile from the monster it just hit. */
Missile *SpawnFollowUp(const Missile &missile, const Monster &monster, WorldTilePosition target)
{
	const WorldTilePosition src = monster.position.tile;
	if (src == target)
		return nullptr;
	const Direction dir = GetDirection(src, target);
	const Poe2MissileData data = missile.poe2;
	Missile *followUp = AddMissile(src, target, dir, missile._mitype, missile._micaster, missile._misource, missile._midam, missile._mispllvl);
	if (followUp == nullptr)
		return nullptr;
	followUp->poe2 = data;
	followUp->poe2.lastHitMonster = static_cast<int16_t>(monster.getId());
	// Keep the supports' speed on the new projectile.
	if (data.speedPercent != 100)
		followUp->position.velocity = followUp->position.velocity * data.speedPercent / 100;
	return followUp;
}

std::optional<WorldTilePosition> FindChainTarget(const Monster &from)
{
	std::optional<WorldTilePosition> best;
	int bestDistance = ChainRange + 1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &other = Monsters[ActiveMonsters[i]];
		if (&other == &from || other.hitPoints <= 0 || other.isPlayerMinion() || !other.isPossibleToHit() || other.position.tile == GolemHoldingCell)
			continue;
		const int distance = from.position.tile.ApproxDistance(other.position.tile);
		if (distance >= bestDistance || !LineClearMissile(from.position.tile, other.position.tile))
			continue;
		bestDistance = distance;
		best = other.position.tile;
	}
	return best;
}

int ManaCostPercent(SupportGem gem, int tier)
{
	if (const SupportTierData *data = FindSupportTier(gem, std::clamp(tier, 1, SupportTierCount(gem))); data != nullptr) {
		// Efficiency and Zenith II make the skill cheaper.
		if (gem == SupportGem::Efficiency)
			return 100 - data->value[0];
		if (gem == SupportGem::BrimmingMana)
			return data->mana * (100 - data->value[1]) / 100;
		return data->mana;
	}
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return 130;
	case SupportGem::Empower:
		return 140;
	case SupportGem::FasterCasting:
		return 120;
	case SupportGem::Pierce:
		return 110;
	case SupportGem::Fork:
		return 110;
	case SupportGem::Chain:
		return 130;
	case SupportGem::ConcentratedEffect:
		return 130;
	case SupportGem::FasterProjectiles:
		return 120;
	case SupportGem::Brutality:
		return 120;
	case SupportGem::MartialTempo:
		return 120;
	case SupportGem::Knockback:
		return 110;
	case SupportGem::BattleRage:
		return 115;
	default:
		if (const SupportData *data = FindSupportData(gem); data != nullptr)
			return data->mana;
		return 100;
	}
}

} // namespace

bool IsPowerSupport(SupportGem gem)
{
	if (const SupportData *data = FindSupportData(gem); data != nullptr)
		return (data->flags & SF_Power) != 0;
	return IsAnyOf(gem, SupportGem::Empower, SupportGem::ConcentratedEffect, SupportGem::Brutality);
}

SupportGem OtherPowerSupport(const SupportSockets &supports, std::optional<size_t> exceptSocket)
{
	for (size_t i = 0; i < supports.size(); i++) {
		if (exceptSocket != i && IsPowerSupport(supports[i]))
			return supports[i];
	}
	return SupportGem::None;
}

SupportSockets Poe2PlayerData::supportsFor(SkillSlot skill) const
{
	if (skill.isEmpty())
		return {};
	// A caster weapon's granted skill is its default attack too.
	if (skill.kind == SkillKind::Default || takesDefaultSupports(skill)) {
		// The default attack keeps its supports when the weapon changes; only the ones that fit the new attack work.
		SupportSockets fitting {};
		for (size_t i = 0; i < MaxSupportSockets; i++)
			fitting[i] = SupportGemCanSupport(defaultSupports[i], skill) ? defaultSupports[i] : SupportGem::None;
		return fitting;
	}
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		if (skillSlots[slot] == skill)
			return slotSupports[slot];
	}
	// A spell inside a meta gem takes the meta gem's supports that fit it.
	if (metaSocketOf(skill))
		return MetaSupportsFor(*this, skill);
	return {};
}

bool Poe2PlayerData::hasSupport(SkillSlot skill, SupportGem gem) const
{
	if (gem == SupportGem::None)
		return false;
	const SupportSockets supports = supportsFor(skill);
	return std::find(supports.begin(), supports.end(), gem) != supports.end();
}

int Poe2PlayerData::supportTier(SkillSlot skill, SupportGem gem) const
{
	if (gem == SupportGem::None || skill.isEmpty())
		return 0;
	if (skill.kind == SkillKind::Default || takesDefaultSupports(skill)) {
		for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
			if (defaultSupports[socket] == gem)
				return socketTier(DefaultAttackRow, socket);
		}
		return 0;
	}
	std::optional<size_t> row = std::nullopt;
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		if (skillSlots[slot] == skill) {
			row = slot;
			break;
		}
	}
	if (!row) {
		if (!hasSupport(skill, gem))
			return 0;
		row = metaSocketOf(skill)->first;
	}
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		if (slotSupports[*row][socket] == gem)
			return socketTier(*row, socket);
	}
	return 0;
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
	case IMISC_SUPPORT_PIERCE:
		return SupportGem::Pierce;
	case IMISC_SUPPORT_FORK:
		return SupportGem::Fork;
	case IMISC_SUPPORT_CHAIN:
		return SupportGem::Chain;
	case IMISC_SUPPORT_CONCENTRATED:
		return SupportGem::ConcentratedEffect;
	case IMISC_SUPPORT_FASTERPROJ:
		return SupportGem::FasterProjectiles;
	case IMISC_SUPPORT_BRUTALITY:
		return SupportGem::Brutality;
	case IMISC_SUPPORT_MARTIALTEMPO:
		return SupportGem::MartialTempo;
	case IMISC_SUPPORT_KNOCKBACK:
		return SupportGem::Knockback;
	case IMISC_SUPPORT_BATTLERAGE:
		return SupportGem::BattleRage;
	default:
		return SupportGem::None;
	}
}

SupportGem SupportGemForItemData(const ItemData &data)
{
	if (data.iMiscId == IMISC_SUPPORT_TABLE)
		return SupportGemForTableItem(data);
	return SupportGemForMiscId(data.iMiscId);
}

SupportGem SupportGemForItem(const Item &item)
{
	if (item.isEmpty())
		return SupportGem::None;
	if (item._iMiscId == IMISC_SUPPORT_TABLE && item.IDidx >= 0 && static_cast<size_t>(item.IDidx) < AllItemsList.size())
		return SupportGemForTableItem(AllItemsList[item.IDidx]);
	return SupportGemForMiscId(item._iMiscId);
}

std::string_view SupportGemName(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return _("Sisters' Spread");
	case SupportGem::Empower:
		return _("Horadric Empowerment");
	case SupportGem::FasterCasting:
		return _("Vizjerei Haste");
	case SupportGem::Efficiency:
		return _("Ascetic's Discipline");
	case SupportGem::Pierce:
		return _("Piercing Attack");
	case SupportGem::Fork:
		return _("Splintering");
	case SupportGem::Chain:
		return _("Arcing");
	case SupportGem::ConcentratedEffect:
		return _("Focused Wrath");
	case SupportGem::FasterProjectiles:
		return _("Swift Flight");
	case SupportGem::Brutality:
		return _("Savagery");
	case SupportGem::MartialTempo:
		return _("Ivgorod Tempo");
	case SupportGem::Knockback:
		return _("Bash");
	case SupportGem::BattleRage:
		return _("Blood Rage");
	default:
		if (const SupportData *data = FindSupportData(gem); data != nullptr)
			return _(data->name.data());
		return {};
	}
}

std::string_view SupportGemDescription(SupportGem gem, int tier)
{
	if (const SupportTierData *data = FindSupportTier(gem, std::clamp(tier, 1, SupportTierCount(gem))); data != nullptr)
		return _(data->description.data());
	return SupportGemDescription(gem);
}

std::string_view SupportGemDescription(SupportGem gem)
{
	switch (gem) {
	case SupportGem::MultipleProjectiles:
		return _("Fires 2 extra projectiles. 35% less\ndamage, 20% slower casts. Mana +30%");
	case SupportGem::Empower:
		return _("Power: +2 spell level. 10% slower\ncasts. Mana +40%");
	case SupportGem::FasterCasting:
		return _("Casts much faster. Mana +20%");
	case SupportGem::Efficiency:
		return _("30% less mana cost");
	case SupportGem::Pierce:
		return _("Bolts pass through 2 monsters,\nthen deal 20% less damage. Mana +10%");
	case SupportGem::Fork:
		return _("Bolts split in two on hit; forks\ndeal 30% less damage. Mana +10%");
	case SupportGem::Chain:
		return _("Bolts jump to 2 more monsters.\n30% less damage. Mana +30%");
	case SupportGem::ConcentratedEffect:
		return _("Power: bolts deal 30% more damage\nbut fly 30% slower. Mana +30%");
	case SupportGem::FasterProjectiles:
		return _("Bolts fly 60% faster. Mana +20%");
	case SupportGem::Brutality:
		return _("Power: 25% more melee damage, but no\nfire or lightning from the weapon. Mana +20%");
	case SupportGem::MartialTempo:
		return _("Attack skills attack faster.\nMana +20%");
	case SupportGem::Knockback:
		return _("Melee attack skills knock enemies\nback. Mana +10%");
	case SupportGem::BattleRage:
		return _("Melee hits build Rage (up to 30):\n1% more attack damage each. Mana +15%");
	default:
		if (const SupportData *data = FindSupportData(gem); data != nullptr)
			return _(data->description.data());
		return {};
	}
}

bool CreateSupportGemItem(Item &item, SupportGem gem, int tier)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (SupportGemForItemData(AllItemsList[i]) != gem)
			continue;
		InitializeItem(item, static_cast<_item_indexes>(i));
		GenerateNewSeed(item);
		item._iIdentified = true;
		SetSupportGemItemTier(item, tier);
		return true;
	}
	return false;
}

bool CreateSupportGemDisplayItem(Item &item, SupportGem gem, int tier)
{
	const uint32_t rngState = GetLCGEngineState();
	const bool made = CreateSupportGemItem(item, gem, tier);
	SetRndSeed(rngState);
	return made;
}

int SupportGemItemTier(const Item &item)
{
	constexpr int Shift = std::countr_zero(static_cast<uint32_t>(CF_POE2_GEM_LEVEL));
	return std::clamp(static_cast<int>((item.dwBuff & CF_POE2_GEM_LEVEL) >> Shift), 1, MaxSupportTier);
}

void SetSupportGemItemTier(Item &item, int tier)
{
	const SupportGem gem = SupportGemForItem(item);
	if (gem == SupportGem::None)
		return;
	tier = std::clamp(tier, 1, SupportTierCount(gem));
	constexpr int Shift = std::countr_zero(static_cast<uint32_t>(CF_POE2_GEM_LEVEL));
	item.dwBuff = (item.dwBuff & ~CF_POE2_GEM_LEVEL) | (static_cast<uint32_t>(tier) << Shift);
	if (tier == 1 || item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size())
		return;
	// "Brutality Support" becomes "Brutality II Support".
	std::string name(_(AllItemsList[item.IDidx].iName));
	const std::string_view numeral = SupportTierNumeral(tier);
	const size_t support = name.rfind(" Support");
	if (support != std::string::npos)
		name.insert(support, StrCat(" ", numeral));
	else
		name = StrCat(name, " ", numeral);
	CopyUtf8(item._iName, name, sizeof(item._iName));
	CopyUtf8(item._iIName, name, sizeof(item._iIName));
}

bool SupportGemHasTiers(SupportGem gem)
{
	return gem != SupportGem::None && !IsLineageSupport(gem) && SupportTierCount(gem) > 1;
}

std::string_view SupportTierNumeral(int tier)
{
	switch (tier) {
	case 2:
		return "II";
	case 3:
		return "III";
	default:
		return "I";
	}
}

std::string SupportGemTierName(SupportGem gem, int tier)
{
	if (tier <= 1 || !SupportGemHasTiers(gem))
		return std::string(SupportGemName(gem));
	return StrCat(SupportGemName(gem), " ", SupportTierNumeral(tier));
}

GemColor SupportGemColor(SupportGem gem)
{
	if (const SupportData *data = FindSupportData(gem); data != nullptr)
		return data->color;
	switch (gem) {
	case SupportGem::ConcentratedEffect:
	case SupportGem::Brutality:
	case SupportGem::Knockback:
	case SupportGem::BattleRage:
		return GemColor::Strength;
	case SupportGem::MultipleProjectiles:
	case SupportGem::Pierce:
	case SupportGem::Fork:
	case SupportGem::FasterProjectiles:
	case SupportGem::MartialTempo:
		return GemColor::Dexterity;
	default:
		return GemColor::Magic;
	}
}

std::string_view GemColorName(GemColor color)
{
	switch (color) {
	case GemColor::Strength:
		return _("Str");
	case GemColor::Dexterity:
		return _("Dex");
	default:
		return _("Mag");
	}
}

bool SkillTakesSupports(SkillSlot skill)
{
	// Minion gems, heralds, whose supports change their explosions, and meta gems, whose supports change the spells inside (POE2).
	if (skill.kind == SkillKind::Persistent)
		return (SkillTags(skill) & (ST_Minion | ST_Herald)) != 0 || IsMetaGem(skill);
	return IsAnyOf(skill.kind, SkillKind::Spell, SkillKind::Attack, SkillKind::Default);
}

std::string_view SupportGemNeeds(SupportGem gem)
{
	if (const SupportData *data = FindSupportData(gem); data != nullptr)
		return data->needs.empty() ? std::string_view {} : std::string_view { _(data->needs.data()) };
	switch (gem) {
	case SupportGem::Brutality:
	case SupportGem::Knockback:
	case SupportGem::BattleRage:
		return _("Needs a melee attack");
	case SupportGem::MartialTempo:
		return _("Needs an attack");
	case SupportGem::MultipleProjectiles:
		return _("Needs a bolt or ball spell");
	case SupportGem::Pierce:
	case SupportGem::Fork:
	case SupportGem::Chain:
	case SupportGem::FasterProjectiles:
	case SupportGem::ConcentratedEffect:
		return _("Needs a bolt spell");
	case SupportGem::Empower:
	case SupportGem::FasterCasting:
		return _("Needs a spell");
	default:
		return {};
	}
}

bool SupportGemCanSupport(SupportGem gem, SkillSlot skill)
{
	if (gem == SupportGem::None || !SkillTakesSupports(skill) || IsRetiredSupport(gem))
		return false;
	if (IsMetaGem(skill))
		return MetaGemTakesSupport(skill, gem);
	if (const SupportData *data = FindSupportData(gem); data != nullptr)
		return TableSupportFits(*data, skill);
	if (skill.kind == SkillKind::Persistent)
		return false;
	if (gem == SupportGem::Efficiency)
		return skill.kind != SkillKind::Default;
	if (skill.kind == SkillKind::Default) {
		// Bow Shot and Staff Bolt fire projectiles; the rest are melee strikes.
		const bool melee = (SkillTags(skill) & ST_Melee) != 0;
		switch (gem) {
		case SupportGem::Brutality:
		case SupportGem::Knockback:
		case SupportGem::BattleRage:
			return melee;
		case SupportGem::MartialTempo:
			return true;
		default:
			return false;
		}
	}
	if (skill.kind == SkillKind::Attack) {
		const WeaponSkill attack = skill.attack();
		switch (gem) {
		case SupportGem::Brutality:
		case SupportGem::Knockback:
		case SupportGem::BattleRage:
			return IsMeleeAttackSkill(attack);
		case SupportGem::MartialTempo:
			return IsQuickenableAttackSkill(attack);
		default:
			return false;
		}
	}

	const SpellID spell = skill.spell();
	if (spell == SpellID::Invalid)
		return false;
	switch (gem) {
	case SupportGem::Brutality:
	case SupportGem::Knockback:
	case SupportGem::MartialTempo:
	case SupportGem::BattleRage:
		return false;
	case SupportGem::MultipleProjectiles:
		return IsProjectileSpell(spell);
	default:
		return !IsProjectileSupport(gem) || IsDirectProjectileSpell(spell);
	}
}

size_t SupportSocketCount(const Player &player, size_t row)
{
	if (row > DefaultAttackRow)
		return BaseSupportSockets;
	return std::min<size_t>(BaseSupportSockets + player.poe2.openedSockets[row], MaxSupportSockets);
}

uint8_t LevelOpenedSockets(int characterLevel)
{
	return static_cast<uint8_t>(std::min<size_t>(static_cast<size_t>(std::max(characterLevel, 0) / 10), MaxSupportSockets - BaseSupportSockets));
}

SupportSocketResult SocketSupportGem(Player &player, size_t slot, std::optional<size_t> socket, SupportGem gem, SupportGem &replaced, int tier, int *replacedTier)
{
	replaced = SupportGem::None;
	if (replacedTier != nullptr)
		*replacedTier = 1;
	tier = std::clamp(tier, 1, SupportTierCount(gem));
	if (slot > DefaultAttackRow || RowSkill(player, slot).isEmpty())
		return SupportSocketResult::NoSkill;
	const SkillSlot skill = RowSkill(player, slot);
	if (!SupportGemCanSupport(gem, skill))
		return SupportSocketResult::NotApplicable;

	SupportSockets &supports = player.poe2.supportsAt(slot);
	const size_t open = SupportSocketCount(player, slot);
	// A meta gem's sockets hold spells as well as supports.
	const auto holdsSpell = [&](size_t i) { return slot < NumSkillSlots && !player.poe2.metaSkills[slot][i].isEmpty(); };
	if (socket) {
		if (*socket >= open)
			return SupportSocketResult::Locked;
		if (holdsSpell(*socket))
			return SupportSocketResult::SpellInSocket;
	} else {
		for (size_t i = 0; i < open; i++) {
			if (supports[i] == SupportGem::None && !holdsSpell(i)) {
				socket = i;
				break;
			}
		}
		if (!socket)
			return SupportSocketResult::NoFreeSocket;
	}

	const SupportGem current = supports[*socket];
	const int currentTier = player.poe2.socketTier(slot, *socket);
	// Another tier of the same support takes its place.
	if (current == gem && currentTier == tier)
		return SupportSocketResult::AlreadySocketed;
	for (size_t i = 0; i < MaxSupportSockets; i++) {
		if (i != *socket && supports[i] == gem)
			return SupportSocketResult::AlreadySocketed;
	}
	if (IsPowerSupport(gem) && OtherPowerSupport(supports, socket) != SupportGem::None)
		return SupportSocketResult::PowerTaken;
	for (size_t i = 0; i < MaxSupportSockets; i++) {
		if (i != *socket && SupportsConflict(gem, supports[i]))
			return SupportSocketResult::Conflict;
	}
	if (IsLineageSupport(gem) && LineageSupportInUse(player.poe2, gem, slot, *socket))
		return SupportSocketResult::LineageInUse;

	supports[*socket] = gem;
	player.poe2.tiersAt(slot)[*socket] = static_cast<uint8_t>(tier);
	replaced = current;
	if (replacedTier != nullptr)
		*replacedTier = currentTier;
	return current == SupportGem::None ? SupportSocketResult::Socketed : SupportSocketResult::Replaced;
}

std::optional<size_t> FindSlotForSupport(const Player &player, SupportGem gem)
{
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		const size_t open = SupportSocketCount(player, slot);
		const SkillSlot skill = player.poe2.skillSlots[slot];
		const SupportSockets &supports = player.poe2.slotSupports[slot];
		if (!SupportGemCanSupport(gem, skill) || std::find(supports.begin(), supports.end(), gem) != supports.end())
			continue;
		if (IsPowerSupport(gem) && OtherPowerSupport(supports, std::nullopt) != SupportGem::None)
			continue;
		for (size_t i = 0; i < open; i++) {
			if (supports[i] == SupportGem::None && player.poe2.metaSkills[slot][i].isEmpty())
				return slot;
		}
	}
	return std::nullopt;
}

SupportGem UnsocketSupportGem(Poe2PlayerData &data, size_t slot, size_t socket, int *tier)
{
	if (slot > DefaultAttackRow || socket >= MaxSupportSockets)
		return SupportGem::None;
	if (tier != nullptr)
		*tier = data.socketTier(slot, socket);
	data.tiersAt(slot)[socket] = 1;
	return std::exchange(data.supportsAt(slot)[socket], SupportGem::None);
}

std::vector<TieredSupport> TakeSlotSupports(Poe2PlayerData &data, size_t slot)
{
	std::vector<TieredSupport> taken;
	if (slot > DefaultAttackRow)
		return taken;
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		if (data.supportsAt(slot)[socket] == SupportGem::None)
			continue;
		int tier = 1;
		const SupportGem gem = UnsocketSupportGem(data, slot, socket, &tier);
		taken.push_back({ gem, tier });
	}
	return taken;
}

int SupportSpellLevelBonus(const Player &player, SpellID spell)
{
	return player.poe2.hasSupport(spell, SupportGem::Empower) ? EmpowerLevels : 0;
}

int SupportManaCostPercent(const Player &player, SkillSlot skill, bool lifeShare)
{
	int percent = UniqueManaCostPercent(player);
	for (const SupportGem gem : player.poe2.supportsFor(skill))
		percent = percent * ManaCostPercent(gem, player.poe2.supportTier(skill, gem)) / 100;
	if (lifeShare)
		percent = percent * SupportLifeManaPercent(player, skill) / 100;
	return percent;
}

int8_t SupportCastSkippedFrames(const Player &player, SpellID spell)
{
	const SkillSlot skill = SkillSlot::ForSpell(spell);
	if (!player.poe2.hasSupport(skill, SupportGem::FasterCasting))
		return 0;
	// Rapid Casting III: faster for each different spell cast lately.
	int frames = SupportValue(player.poe2, skill, SupportGem::FasterCasting, 0);
	if (const int perSpell = SupportValue(player.poe2, skill, SupportGem::FasterCasting, 1); perSpell > 0)
		frames += perSpell * RecentSpellCount(player.poe2.supportState);
	return static_cast<int8_t>(frames);
}

int SupportCastRecoveryTicks(const Player &player, SpellID spell, int castFrames)
{
	int speed = 100;
	if (IsProjectileSpell(spell) && player.poe2.hasSupport(spell, SupportGem::MultipleProjectiles))
		speed = speed * (100 - SupportValue(player.poe2, SkillSlot::ForSpell(spell), SupportGem::MultipleProjectiles, 2)) / 100;
	if (player.poe2.hasSupport(spell, SupportGem::Empower))
		speed = speed * (100 - EmpowerSlowPercent) / 100;
	speed = speed * (100 - UniqueCastSlowPercent(player)) / 100;
	speed = speed * (100 - SupportCastSlowPercent(player, SkillSlot::ForSpell(spell))) / 100;
	if (speed >= 100 || castFrames <= 0)
		return 0;
	// At speed% of the normal speed the cast takes 100/speed as long.
	return castFrames * (100 - speed) / speed;
}

int SupportSpellDamagePercent(const Player &player, SpellID spell)
{
	// A socketed spell gem hits harder as it levels up, on top of what Diablo's spell level adds; a slower spell hits
	// harder for its cast time either way (SkillSpeedDamagePercent).
	const int gemLevel = player.poe2.skillLevel(spell);
	int percent = gemLevel > 0 ? GemDamagePercent(SkillSlot::ForSpell(spell), gemLevel) : SkillSpeedDamagePercent(SkillSlot::ForSpell(spell));
	percent = percent * UniqueSpellDamagePercent(player, spell) / 100;
	percent = percent * SpiritSpellDamagePercent(player) / 100;
	const SkillSlot skill = SkillSlot::ForSpell(spell);
	if (IsProjectileSpell(spell) && player.poe2.hasSupport(spell, SupportGem::MultipleProjectiles))
		percent = percent * (100 - SupportValue(player.poe2, skill, SupportGem::MultipleProjectiles, 1)) / 100;
	if (IsDirectProjectileSpell(spell)) {
		if (player.poe2.hasSupport(spell, SupportGem::Chain))
			percent = percent * (100 - SupportValue(player.poe2, skill, SupportGem::Chain, 1)) / 100;
		if (player.poe2.hasSupport(spell, SupportGem::ConcentratedEffect))
			percent = percent * ConcentratedEffectDamagePercent / 100;
		// Projectile Acceleration III: the speed adds to the damage.
		percent = percent * (100 + SupportValue(player.poe2, skill, SupportGem::FasterProjectiles, 2)) / 100;
	}
	return percent;
}

int SupportExtraProjectiles(const Player &player, SpellID spell)
{
	if (!IsProjectileSpell(spell))
		return 0;
	const int extra = SupportValue(player.poe2, SkillSlot::ForSpell(spell), SupportGem::MultipleProjectiles, 0);
	return extra + SupportExtraProjectileCount(player, SkillSlot::ForSpell(spell));
}

WeaponSkill CurrentWeaponSkill(const Player &player)
{
	return player.poe2.castSkill != WeaponSkill::None ? player.poe2.castSkill : player.poe2.activeSkill;
}

SkillSlot CurrentAttackSkill(const Player &player)
{
	const WeaponSkill skill = CurrentWeaponSkill(player);
	return skill != WeaponSkill::None ? SkillSlot::ForAttack(skill) : DefaultSkillSlot(player);
}

SkillSlot RowSkill(const Player &player, size_t row)
{
	if (row == DefaultAttackRow)
		return DefaultSkillSlot(player);
	return row < NumSkillSlots ? player.poe2.skillSlots[row] : SkillSlot {};
}

int SupportMeleeDamagePercent(const Player &player)
{
	const WeaponSkill skill = CurrentWeaponSkill(player);
	if (skill == WeaponSkill::None) {
		const SkillSlot base = DefaultSkillSlot(player);
		if (base.isEmpty())
			return 100;
		// The free default attack hits softer than a skill gem: costing nothing is its strength.
		return DefaultAttackDamagePercent * (100 + SupportValue(player.poe2, base, SupportGem::Brutality, 0)) / 100;
	}
	const int brutality = 100 + SupportValue(player.poe2, SkillSlot::ForAttack(skill), SupportGem::Brutality, 0);
	return GemDamagePercent(SkillSlot::ForAttack(skill), player.poe2.skillLevel(skill)) * brutality / 100;
}

bool SupportMeleeKnocksBack(const Player &player)
{
	return player.poe2.hasSupport(CurrentAttackSkill(player), SupportGem::Knockback);
}

bool SupportMeleeIsPhysicalOnly(const Player &player)
{
	const SkillSlot skill = CurrentAttackSkill(player);
	return player.poe2.hasSupport(skill, SupportGem::Brutality) || player.poe2.hasSupport(skill, SupportGem::Might);
}

int8_t SupportAttackSkippedFrames(const Player &player, WeaponSkill skill)
{
	// No weapon skill: the weapon's default attack, which takes supports too.
	const SkillSlot slot = skill != WeaponSkill::None ? SkillSlot::ForAttack(skill) : DefaultSkillSlot(player);
	if (slot.isEmpty())
		return 0;
	int frames = SupportValue(player.poe2, slot, SupportGem::MartialTempo, 0) + SupportAttackFrameBonus(player, slot);
	// Rage III: faster while not at full Rage.
	if (player.poe2.rage < MaxRage)
		frames += SupportValue(player.poe2, slot, SupportGem::BattleRage, 1);
	return static_cast<int8_t>(std::clamp(frames, -4, 8));
}

WorldTilePosition RotateSpellTarget(WorldTilePosition src, WorldTilePosition dst, int degrees)
{
	// cos and sin of 0, 15, 30 and 45 degrees, scaled by 1024.
	constexpr int Cos[] = { 1024, 989, 887, 724 };
	constexpr int Sin[] = { 0, 265, 512, 724 };
	const int steps = std::min(std::abs(degrees) / 15, 3);
	const int sign = degrees < 0 ? -1 : 1;

	int dx = dst.x - src.x;
	int dy = dst.y - src.y;
	if (dx == 0 && dy == 0)
		return dst;
	// Aim far enough away that rounding to whole tiles keeps the spread visible.
	int scale = 1;
	while (((dx * scale) * (dx * scale)) + ((dy * scale) * (dy * scale)) < 100)
		scale++;
	dx *= scale;
	dy *= scale;

	const int rx = ((dx * Cos[steps]) - (sign * dy * Sin[steps])) / 1024;
	const int ry = ((sign * dx * Sin[steps]) + (dy * Cos[steps])) / 1024;
	return { static_cast<WorldTileCoord>(std::clamp(src.x + rx, 0, MAXDUNX - 1)), static_cast<WorldTileCoord>(std::clamp(src.y + ry, 0, MAXDUNY - 1)) };
}

void ApplySupportsToMissile(Missile &missile, const Player &player, SpellID spell)
{
	missile.poe2.damagePercent = static_cast<int16_t>(SupportSpellDamagePercent(player, spell));
	SetMissileSkill(missile, player, SkillSlot::ForSpell(spell));
	if (!IsDirectProjectileSpell(spell))
		return;
	const Poe2PlayerData &data = player.poe2;
	const SupportSockets supports = data.supportsFor(spell);
	if (!std::any_of(supports.begin(), supports.end(), IsProjectileSupport))
		return;

	missile.poe2.spell = spell;
	const SkillSlot skill = SkillSlot::ForSpell(spell);
	if (data.hasSupport(skill, SupportGem::Pierce)) {
		missile.poe2.piercesLeft = static_cast<int8_t>(SupportValue(data, skill, SupportGem::Pierce, 0));
		missile.poe2.pierceLessPercent = static_cast<int8_t>(SupportValue(data, skill, SupportGem::Pierce, 1));
		missile.poe2.pierceEachLessPercent = static_cast<int8_t>(SupportValue(data, skill, SupportGem::Pierce, 2));
	}
	// Projectile Acceleration II: a chance to pierce a monster.
	if (const int chance = SupportValue(data, skill, SupportGem::FasterProjectiles, 1); chance > 0 && missile.poe2.piercesLeft == 0 && GenerateRnd(100) < chance)
		missile.poe2.piercesLeft = 1;
	if (data.hasSupport(skill, SupportGem::Chain))
		missile.poe2.chainsLeft = static_cast<int8_t>(SupportValue(data, skill, SupportGem::Chain, 0));
	missile.poe2.canFork = data.hasSupport(spell, SupportGem::Fork);
	int speed = 100;
	if (data.hasSupport(spell, SupportGem::ConcentratedEffect))
		speed = speed * ConcentratedEffectSpeedPercent / 100;
	if (data.hasSupport(skill, SupportGem::FasterProjectiles))
		speed = speed * (100 + SupportValue(data, skill, SupportGem::FasterProjectiles, 0)) / 100;
	missile.poe2.speedPercent = static_cast<int16_t>(speed);
	if (speed != 100)
		missile.position.velocity = missile.position.velocity * speed / 100;
}

void ScaleSupportedMissileDamage(const Missile &missile, int &minDamage, int &maxDamage)
{
	if (missile.poe2.damagePercent == 100)
		return;
	minDamage = minDamage * missile.poe2.damagePercent / 100;
	maxDamage = maxDamage * missile.poe2.damagePercent / 100;
}

bool SupportedMissileIgnoresMonster(const Missile &missile, int monsterId)
{
	// Only set by a projectile that hit and flew on (see OnSupportedMissileHit).
	return missile.poe2.lastHitMonster == monsterId;
}

bool OnSupportedMissileHit(Missile &missile, const Monster &monster)
{
	// Arrows have no spell but can pierce (pierce skills, Two Fallen, One Arrow).
	if (missile.poe2.spell == SpellID::Invalid && missile.poe2.piercesLeft <= 0)
		return false;
	missile.poe2.lastHitMonster = static_cast<int16_t>(monster.getId());

	// Like POE2: a projectile pierces first, then forks, then chains.
	if (missile.poe2.piercesLeft > 0) {
		// Once it has pierced, it hits for less (Pierce I), or less again for each monster (Pierce III).
		if (missile.poe2.pierceLessPercent > 0) {
			missile.poe2.damagePercent = static_cast<int16_t>(missile.poe2.damagePercent * (100 - missile.poe2.pierceLessPercent) / 100);
			missile.poe2.pierceLessPercent = 0;
		}
		if (missile.poe2.pierceEachLessPercent > 0)
			missile.poe2.damagePercent = static_cast<int16_t>(missile.poe2.damagePercent * (100 - missile.poe2.pierceEachLessPercent) / 100);
		missile.poe2.piercesLeft--;
		return true;
	}

	if (missile.poe2.canFork) {
		missile.poe2.canFork = false;
		const WorldTilePosition hit = monster.position.tile;
		const WorldTilePosition ahead = hit + (hit - missile.position.start);
		for (const int degrees : { -ForkDegrees, ForkDegrees }) {
			if (Missile *fork = SpawnFollowUp(missile, monster, RotateSpellTarget(hit, ahead, degrees)); fork != nullptr) {
				fork->poe2.canFork = false;
				fork->poe2.damagePercent = static_cast<int16_t>(fork->poe2.damagePercent * ForkDamagePercent / 100);
			}
		}
		return false;
	}

	if (missile.poe2.chainsLeft > 0) {
		if (const std::optional<WorldTilePosition> target = FindChainTarget(monster); target) {
			if (Missile *chain = SpawnFollowUp(missile, monster, *target); chain != nullptr)
				chain->poe2.chainsLeft = static_cast<int8_t>(missile.poe2.chainsLeft - 1);
		}
	}
	return false;
}

} // namespace devilution

#include "poe2/meta_gems.hpp"

#include <algorithm>
#include <array>
#include <string>

#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "panels/item_spawner.hpp"
#include "panels/skill_bar.hpp"
#include "player.h"
#include "levels/gendung.h"
#include "monster.h"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "utils/is_of.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** @brief Energy is kept in hundredths, like POE2's centienergy, so a channelled second can lose 25 a tick at a time. */
constexpr int CentiPerEnergy = 100;
constexpr int TicksPerSecond = 20;

std::optional<size_t> RowOf(const Poe2PlayerData &data, SkillSlot skill)
{
	for (size_t row = 0; row < NumSkillSlots; row++) {
		if (data.skillSlots[row] == skill)
			return row;
	}
	return std::nullopt;
}

/** @brief The row of a meta gem whose Spirit is reserved and that has a spell inside, so it can gain Energy. */
std::optional<size_t> ActiveMetaRow(const Player &player, PersistentSkill meta)
{
	const std::optional<size_t> row = RowOf(player.poe2, SkillSlot::ForPersistent(meta));
	if (!row || !IsPersistentSkillActive(player, meta) || MetaSkillCount(player.poe2, *row) == 0)
		return std::nullopt;
	return row;
}

int EnergyGainPercent(const Player &player, size_t row)
{
	return 100 + (MetaEnergyPercentPerLevel * (player.poe2.gemLevel(row) - 1));
}

/** @brief Pays for a triggered skill as POE2 does: it costs its mana, and doesn't go off without it. */
bool PayTriggeredSkill(Player &player, SkillSlot skill)
{
	const int cost = SkillManaCost(player, skill);
	if (cost <= 0)
		return true;
	if (player._pMana.whole() < cost)
		return false;
	Fixed26_6 paid = Fixed26_6::fromInt(cost);
	if (skill.kind == SkillKind::Spell) {
		paid = PaySpellCostWithEnergyShield(player, paid);
		PaySupportLifeCost(player, skill, GetManaAmount(player, skill.spell(), false).whole());
	} else {
		NoteResourceSpent(player, paid);
		PaySupportLifeCost(player, skill, WeaponSkillManaCost(player, skill.attack(), false));
	}
	player._pMana -= paid;
	player._pManaBase -= paid;
	RedrawComponent(PanelDrawComponent::Mana);
	return true;
}

std::string_view EventWord(PersistentSkill meta, int events)
{
	switch (meta) {
	case PersistentSkill::CastOnDodge:
		return events == 1 ? "roll" : "rolls";
	case PersistentSkill::CastWhenDamageTaken:
		return events == 1 ? "hit taken" : "hits taken";
	case PersistentSkill::ReapersInvocation:
		return events == 1 ? "kill" : "kills";
	default:
		return events == 1 ? "second" : "seconds";
	}
}

/** @brief Casts every skill inside the row's meta gem and empties its Energy. */
void CastMetaSkills(Player &player, size_t row, Point target)
{
	SupportRuntime &state = player.poe2.supportState;
	const PersistentSkill meta = player.poe2.skillSlots[row].persistent();
	std::string cast;
	for (const SkillSlot skill : player.poe2.metaSkills[row]) {
		if (skill.isEmpty())
			continue;
		// A gem the character can't use yet, an attack without its weapon and a spell barred from town stay quiet.
		if (IsAnyOf(GetSkillUsability(player, skill), SkillUsability::Requirements, SkillUsability::Weapon, SkillUsability::Town)) {
			StrAppend(cast, cast.empty() ? "" : ", ", "can't use ", SkillName(skill));
			continue;
		}
		if (!PayTriggeredSkill(player, skill)) {
			StrAppend(cast, cast.empty() ? "" : ", ", "no mana for ", SkillName(skill));
			continue;
		}
		UseTriggeredSkill(player, skill, target);
		StrAppend(cast, cast.empty() ? "" : ", ", SkillName(skill));
	}
	const int events = state.metaEvents[row];
	TelemetryLog("meta", StrCat(PersistentSkillName(meta), " cast ", cast, " after ", events, " ", EventWord(meta, events)));
	// An invocation spends what the cast takes and keeps the rest.
	const int spent = IsInvocationGem(SkillSlot::ForPersistent(meta)) ? MetaMaxEnergy(player.poe2, row) * CentiPerEnergy : state.metaEnergy[row];
	state.metaEnergy[row] = static_cast<uint16_t>(std::max(state.metaEnergy[row] - spent, 0));
	state.metaEvents[row] = 0;
}

/** @brief Adds Energy to the meta gem, in hundredths before its gem level; at full it casts. */
void GainEnergy(Player &player, PersistentSkill meta, int centiEnergy, Point target, bool countEvent)
{
	const std::optional<size_t> row = ActiveMetaRow(player, meta);
	if (!row)
		return;
	SupportRuntime &state = player.poe2.supportState;
	const int gained = centiEnergy * EnergyGainPercent(player, *row) / 100;
	const int cap = MetaEnergyCap(player.poe2, *row) * CentiPerEnergy;
	state.metaEnergy[*row] = static_cast<uint16_t>(std::clamp(state.metaEnergy[*row] + gained, 0, cap));
	if (countEvent)
		state.metaEvents[*row] = static_cast<uint8_t>(std::min(state.metaEvents[*row] + 1, 255));
	// An invocation stores its Energy until you use it.
	if (!IsInvocationGem(SkillSlot::ForPersistent(meta)) && state.metaEnergy[*row] >= MetaMaxEnergy(player.poe2, *row) * CentiPerEnergy)
		CastMetaSkills(player, *row, target);
}

} // namespace

bool IsMetaGem(PersistentSkill skill)
{
	return IsAnyOf(skill, PersistentSkill::CastOnDodge, PersistentSkill::CastWhenDamageTaken, PersistentSkill::CastWhileChannelling, PersistentSkill::ReapersInvocation);
}

bool IsMetaGem(SkillSlot skill)
{
	return IsMetaGem(skill.persistent());
}

bool IsInvocationGem(SkillSlot skill)
{
	return skill.persistent() == PersistentSkill::ReapersInvocation;
}

bool CanGoInMetaGem(SkillSlot skill)
{
	if (!IsAnyOf(skill.kind, SkillKind::Spell, SkillKind::Attack))
		return false;
	const uint32_t tags = SkillTags(skill);
	if ((tags & ST_Spell) == 0 || (tags & (ST_Movement | ST_Channelling | ST_Minion)) != 0)
		return false;
	// Spells for towns, items and other players have nothing to cast at.
	return !IsAnyOf(skill.spell(), SpellID::TownPortal, SpellID::Resurrect, SpellID::HealOther, SpellID::Identify, SpellID::ItemRepair, SpellID::StaffRecharge,
	    SpellID::Telekinesis, SpellID::Infravision, SpellID::Search);
}

bool SupportFitsTriggeredSpells(SupportGem gem)
{
	// Triggered spells can't set off more supports, so supports that act when you use a skill do nothing on them.
	return !IsRetiredSupport(gem)
	    && !IsAnyOf(gem, SupportGem::SpellEcho, SupportGem::HoradricSeals, SupportGem::BaalsEcho, SupportGem::Cascade, SupportGem::RuneMastery, SupportGem::ClusterRunes,
	        SupportGem::NaKrulsPrison, SupportGem::TalRashasPrism, SupportGem::ZharsMadness, SupportGem::CastOnKill, SupportGem::HoradricTotem, SupportGem::Decoy,
	        SupportGem::FasterCasting, SupportGem::DeliberateCasting);
}

bool MetaGemTakesSupport(const Poe2PlayerData &data, size_t row, SupportGem gem)
{
	if (row >= NumSkillSlots || !SupportFitsTriggeredSpells(gem))
		return false;
	return std::any_of(data.metaSkills[row].begin(), data.metaSkills[row].end(),
	    [gem](SkillSlot skill) { return !skill.isEmpty() && SupportGemCanSupport(gem, skill); });
}

bool MetaGemTakesSupport(SkillSlot meta, SupportGem gem)
{
	if (MyPlayer == nullptr)
		return false;
	const std::optional<size_t> row = RowOf(MyPlayer->poe2, meta);
	return row && MetaGemTakesSupport(MyPlayer->poe2, *row, gem);
}

const SupportSockets &MetaSupportsFor(const Poe2PlayerData &data, SkillSlot skill)
{
	static const SupportSockets None {};
	// One list for each meta socket, so a caller still going through one isn't changed by a lookup for another skill.
	static std::array<SupportSockets, NumSkillSlots * MaxSupportSockets> fitting {};
	const auto meta = data.metaSocketOf(skill);
	if (!meta)
		return None;
	SupportSockets &result = fitting[(meta->first * MaxSupportSockets) + meta->second];
	const SupportSockets &supports = data.slotSupports[meta->first];
	for (size_t i = 0; i < MaxSupportSockets; i++) {
		const SupportGem gem = supports[i];
		result[i] = gem != SupportGem::None && SupportFitsTriggeredSpells(gem) && SupportGemCanSupport(gem, skill) ? gem : SupportGem::None;
	}
	return result;
}

int MetaSkillCount(const Poe2PlayerData &data, size_t row)
{
	if (row >= NumSkillSlots)
		return 0;
	return static_cast<int>(std::count_if(data.metaSkills[row].begin(), data.metaSkills[row].end(), [](SkillSlot skill) { return !skill.isEmpty(); }));
}

int MetaMaxEnergy(const Poe2PlayerData &data, size_t row)
{
	return MetaEnergyPerSpell * MetaSkillCount(data, row);
}

int MetaEnergyCap(const Poe2PlayerData &data, size_t row)
{
	if (row < NumSkillSlots && IsInvocationGem(data.skillSlots[row]))
		return ReaperMaxEnergy;
	return MetaMaxEnergy(data, row);
}

int MetaEnergy(const Player &player, size_t row)
{
	return row < NumSkillSlots ? player.poe2.supportState.metaEnergy[row] / CentiPerEnergy : 0;
}

PersistentSkill SpiritGemForRetiredSupport(SupportGem gem)
{
	switch (gem) {
	case SupportGem::CastOnKill:
		return PersistentSkill::ReapersInvocation;
	case SupportGem::Decoy:
		return PersistentSkill::MirageArcher;
	case SupportGem::CastOnDodge:
		return PersistentSkill::CastOnDodge;
	case SupportGem::CastWhenHit:
		return PersistentSkill::CastWhenDamageTaken;
	case SupportGem::CastWhileChannelling:
		return PersistentSkill::CastWhileChannelling;
	default:
		return PersistentSkill::None;
	}
}

bool IsRetiredSupport(SupportGem gem)
{
	return SpiritGemForRetiredSupport(gem) != PersistentSkill::None;
}

MetaSocketResult SocketSkillInMeta(Player &player, size_t row, size_t socket, SkillSlot skill, int level, SkillSlot &replaced, int &replacedLevel)
{
	replaced = {};
	replacedLevel = 1;
	if (row >= NumSkillSlots || !IsMetaGem(player.poe2.skillSlots[row]))
		return MetaSocketResult::NotMeta;
	if (socket >= SupportSocketCount(player, row))
		return MetaSocketResult::Locked;
	if (!CanGoInMetaGem(skill))
		return MetaSocketResult::NotTriggerable;
	if (const auto inside = player.poe2.metaSocketOf(skill); RowOf(player.poe2, skill) || (inside && *inside != std::make_pair(row, socket)))
		return MetaSocketResult::AlreadySocketed;

	MetaSocketResult result = MetaSocketResult::Socketed;
	// A support in the socket comes out as a gem.
	if (const SupportGem support = player.poe2.slotSupports[row][socket]; support != SupportGem::None) {
		int tier = 1;
		UnsocketSupportGem(player.poe2, row, socket, &tier);
		GiveSupportGems(player, { { support, tier } });
		result = MetaSocketResult::Replaced;
	}
	if (!player.poe2.metaSkills[row][socket].isEmpty()) {
		replaced = UnsocketMetaSkill(player.poe2, row, socket, &replacedLevel);
		result = MetaSocketResult::Replaced;
	}
	player.poe2.metaSkills[row][socket] = skill;
	player.poe2.metaSkillLevels[row][socket] = static_cast<uint8_t>(std::clamp(level, 1, MaxGemLevel));
	return result;
}

SkillSlot UnsocketMetaSkill(Poe2PlayerData &data, size_t row, size_t socket, int *level)
{
	if (row >= NumSkillSlots || socket >= MaxSupportSockets)
		return {};
	const SkillSlot skill = data.metaSkills[row][socket];
	if (level != nullptr)
		*level = std::clamp<int>(data.metaSkillLevels[row][socket], 1, MaxGemLevel);
	data.metaSkills[row][socket] = {};
	data.metaSkillLevels[row][socket] = 1;
	// The bar is as long as the spells inside, so what's left can't be more than that.
	data.supportState.metaEnergy[row] = static_cast<uint16_t>(std::min<int>(data.supportState.metaEnergy[row], MetaEnergyCap(data, row) * CentiPerEnergy));
	return skill;
}

void ReturnMetaSkills(Player &player, size_t row)
{
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		int level = 1;
		const SkillSlot skill = UnsocketMetaSkill(player.poe2, row, socket, &level);
		Item gem;
		if (!skill.isEmpty() && CreateSkillGemItem(gem, skill, level))
			GiveItemToPlayer(player, gem);
	}
	player.poe2.supportState.metaEnergy[row] = 0;
	player.poe2.supportState.metaEvents[row] = 0;
}

void OnMetaDodge(Player &player, int tiles)
{
	if (!gbIsPoe2 || &player != MyPlayer || tiles <= 0)
		return;
	GainEnergy(player, PersistentSkill::CastOnDodge, CastOnDodgeEnergyPerTile * CentiPerEnergy * tiles, NearestMonsterTarget(player, player.position.tile, 8), true);
}

void OnMetaDamageTaken(Player &player, Point attacker)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	GainEnergy(player, PersistentSkill::CastWhenDamageTaken, CastWhenDamageTakenEnergy * CentiPerEnergy, attacker, true);
}

void OnMetaKill(Player &player, const Monster &monster, SkillSlot skill)
{
	if (!gbIsPoe2 || &player != MyPlayer || (SkillTags(skill) & ST_Melee) == 0)
		return;
	int energy = ReaperEnergyNormal;
	if (monster.isUnique()) {
		energy = ReaperEnergyUnique;
	} else {
		switch (GetMonsterAffixes(monster).rarity) {
		case MonsterRarity::Magic:
			energy = ReaperEnergyMagic;
			break;
		case MonsterRarity::Rare:
			energy = ReaperEnergyRare;
			break;
		default:
			break;
		}
	}
	GainEnergy(player, PersistentSkill::ReapersInvocation, energy * CentiPerEnergy, monster.position.tile, true);
}

InvokeResult InvokeMetaGem(Player &player, size_t row, Point target)
{
	if (row >= NumSkillSlots || !IsInvocationGem(player.poe2.skillSlots[row]) || !IsPersistentSkillActive(player, player.poe2.skillSlots[row].persistent()))
		return InvokeResult::Off;
	if (MetaSkillCount(player.poe2, row) == 0)
		return InvokeResult::NoSpells;
	if (leveltype == DTYPE_TOWN)
		return InvokeResult::Town;
	if (MetaEnergy(player, row) < MetaMaxEnergy(player.poe2, row))
		return InvokeResult::NotEnoughEnergy;
	CastMetaSkills(player, row, target);
	return InvokeResult::Cast;
}

void ProcessMetaGems(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	SupportRuntime &state = player.poe2.supportState;
	// A meta gem that is off, or out of Spirit, loses its Energy (POE2).
	for (size_t row = 0; row < NumSkillSlots; row++) {
		const PersistentSkill meta = player.poe2.skillSlots[row].persistent();
		if (state.metaEnergy[row] > 0 && (!IsMetaGem(meta) || !IsPersistentSkillActive(player, meta))) {
			state.metaEnergy[row] = 0;
			state.metaEvents[row] = 0;
		}
	}
	const std::optional<size_t> row = ActiveMetaRow(player, PersistentSkill::CastWhileChannelling);
	if (!row)
		return;
	if (player.poe2.volleyShotsLeft > 0) {
		// Counts seconds of channelling: one each time a second's worth of ticks has passed.
		state.metaChannelTicks = static_cast<uint8_t>((state.metaChannelTicks + 1) % TicksPerSecond);
		GainEnergy(player, PersistentSkill::CastWhileChannelling, ChannellingEnergyPerSecond * CentiPerEnergy / TicksPerSecond,
		    { player.poe2.volleyTargetX, player.poe2.volleyTargetY }, state.metaChannelTicks == 0);
	} else if (state.metaEnergy[*row] > 0) {
		const int drain = ChannellingDrainPerSecond * CentiPerEnergy / TicksPerSecond;
		state.metaEnergy[*row] = static_cast<uint16_t>(std::max(state.metaEnergy[*row] - drain, 0));
	}
}

void MigrateTriggerSupports(Player &player)
{
	Poe2PlayerData &data = player.poe2;
	for (size_t row = 0; row < NumSkillSlots; row++) {
		for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
			const PersistentSkill meta = SpiritGemForRetiredSupport(data.slotSupports[row][socket]);
			if (meta == PersistentSkill::None)
				continue;
			const SkillSlot gem = SkillSlot::ForPersistent(meta);
			// The new gem goes in a free row of the Skills panel, switched off, or else the inventory.
			if (!RowOf(data, gem)) {
				if (const std::optional<size_t> free = RowOf(data, {})) {
					data.skillSlots[*free] = gem;
					data.slotLevels[*free] = 1;
					data.spiritGemsOn &= static_cast<uint16_t>(~(1U << *free));
				} else {
					Item item;
					if (!CreateSkillGemItem(item, gem, 1) || !AutoPlaceItemInInventory(player, item, true))
						continue; // No room anywhere: the old support stays where it is, doing nothing.
				}
			}
			data.slotSupports[row][socket] = SupportGem::None;
			data.tiersAt(row)[socket] = 1;
		}
	}
}

} // namespace devilution

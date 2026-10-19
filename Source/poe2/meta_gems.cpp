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
#include "poe2/skill_resources.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** @brief Energy is kept in hundredths, like POE2's centienergy, so a channelled second can lose 25 a tick at a time. */
constexpr int CentiPerEnergy = 100;
constexpr int TicksPerSecond = 20;

/** @brief A meta gem is casting: what its spells do fills no meta gem, as in POE2, so one can't set itself or another off again. */
bool MetaCasting = false;

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
	case PersistentSkill::CastOnCritical:
		return events == 1 ? "critical hit" : "critical hits";
	case PersistentSkill::CastOnElementalAilment:
	case PersistentSkill::ElementalInvocation:
		return events == 1 ? "ailment" : "ailments";
	case PersistentSkill::CastOnMinionDeath:
		return events == 1 ? "minion death" : "minion deaths";
	case PersistentSkill::BarrierInvocation:
		return events == 1 ? "hit on energy shield" : "hits on energy shield";
	default:
		return events == 1 ? "second" : "seconds";
	}
}

/** @brief The damage of the spells a meta gem casts, in percent: Cast on Critical's deal 20% less. */
int MetaDamagePercent(PersistentSkill meta)
{
	return meta == PersistentSkill::CastOnCritical ? CastOnCriticalDamagePercent : 100;
}

/** @brief Casts every skill inside the row's meta gem and empties its Energy. */
void CastMetaSkills(Player &player, size_t row, Point target)
{
	SupportRuntime &state = player.poe2.supportState;
	const PersistentSkill meta = player.poe2.skillSlots[row].persistent();
	const int events = state.metaEvents[row];
	// An invocation spends what the cast takes and keeps the rest.
	const int spent = IsInvocationGem(SkillSlot::ForPersistent(meta)) ? MetaMaxEnergy(player.poe2, row) * CentiPerEnergy : state.metaEnergy[row];
	state.metaEnergy[row] = static_cast<uint16_t>(std::max(state.metaEnergy[row] - spent, 0));
	state.metaEvents[row] = 0;
	MetaCasting = true;
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
		UseTriggeredSkill(player, skill, target, MetaDamagePercent(meta));
		StrAppend(cast, cast.empty() ? "" : ", ", SkillName(skill));
	}
	MetaCasting = false;
	TelemetryLog("meta", StrCat(PersistentSkillName(meta), " cast ", cast, " after ", events, " ", EventWord(meta, events)));
}

/** @brief Adds Energy to the meta gem, in hundredths before its gem level; at full it casts. */
void GainEnergy(Player &player, PersistentSkill meta, int centiEnergy, Point target, bool countEvent)
{
	const std::optional<size_t> row = ActiveMetaRow(player, meta);
	if (!row || MetaCasting)
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

/** @brief The hit in progress comes from a skill inside a meta gem, so it can't fill one: a meta gem can't set itself off again (POE2). */
bool FromMetaSkill(const Player &player)
{
	const SkillSlot skill = SupportHitSkill();
	return !skill.isEmpty() && player.poe2.metaSocketOf(skill).has_value();
}

bool IsBlasphemyCurse(SkillSlot skill)
{
	if (skill.kind != SkillKind::Attack)
		return false;
	if (skill.attack() == WeaponSkill::AmplifyDamage)
		return true;
	const TableSkillData *data = FindTableSkill(skill.attack());
	// Curses that take control of an enemy would never let go of it, and Haunt and Locust Swarm are damage, not hexes.
	return data != nullptr && IsAnyOf(data->curse, TableCurse::Weaken, TableCurse::IronMaiden, TableCurse::Decrepify, TableCurse::LowerResist, TableCurse::DeathMark);
}

/** @brief Blasphemy: every half second the curses inside reach the enemies around you, at part strength. */
void ProcessBlasphemy(Player &player)
{
	SupportRuntime &state = player.poe2.supportState;
	const std::optional<size_t> row = ActiveMetaRow(player, PersistentSkill::Blasphemy);
	if (!row || leveltype == DTYPE_TOWN || player.hasNoLife()) {
		state.blasphemyTicks = 0;
		return;
	}
	if (state.blasphemyTicks > 0 && --state.blasphemyTicks > 0)
		return;
	state.blasphemyTicks = BlasphemyPulseTicks;
	const int strength = BlasphemyStrength(player.poe2.gemLevel(*row));
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		const SkillSlot curse = player.poe2.metaSkills[*row][socket];
		if (!IsBlasphemyCurse(curse) || IsAnyOf(GetSkillUsability(player, curse), SkillUsability::Requirements, SkillUsability::Weapon))
			continue;
		const int radius = SupportAreaRadius(player, curse, BlasphemyRadius);
		const int level = player.poe2.metaSkillLevels[*row][socket];
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			Monster &monster = Monsters[ActiveMonsters[i]];
			if (monster.hasNoLife() || monster.isPlayerMinion() || !monster.isPossibleToHit() || player.position.tile.ExactDistance(monster.position.tile) > radius)
				continue;
			if (curse.attack() == WeaponSkill::AmplifyDamage)
				ApplyAuraAmplifyDamage(player, monster, level, strength, BlasphemyCurseTicks);
			else
				ApplyAuraCurse(player, monster, curse.attack(), strength, BlasphemyCurseTicks);
		}
	}
}

} // namespace

bool IsMetaGem(PersistentSkill skill)
{
	return IsAnyOf(skill, PersistentSkill::CastOnDodge, PersistentSkill::CastWhenDamageTaken, PersistentSkill::CastWhileChannelling, PersistentSkill::ReapersInvocation,
	    PersistentSkill::Blasphemy, PersistentSkill::HoradricTotem, PersistentSkill::CastOnCritical, PersistentSkill::CastOnElementalAilment,
	    PersistentSkill::CastOnMinionDeath, PersistentSkill::BarrierInvocation, PersistentSkill::ElementalInvocation);
}

bool IsMetaGem(SkillSlot skill)
{
	return IsMetaGem(skill.persistent());
}

bool IsInvocationGem(SkillSlot skill)
{
	return IsAnyOf(skill.persistent(), PersistentSkill::ReapersInvocation, PersistentSkill::BarrierInvocation, PersistentSkill::ElementalInvocation);
}

bool UsesMetaEnergy(SkillSlot skill)
{
	return IsMetaGem(skill) && !IsAnyOf(skill.persistent(), PersistentSkill::Blasphemy, PersistentSkill::HoradricTotem);
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

bool CanGoInMetaGem(PersistentSkill meta, SkillSlot skill)
{
	if (meta == PersistentSkill::Blasphemy)
		return IsBlasphemyCurse(skill);
	return CanGoInMetaGem(skill);
}

std::string_view MetaGemTakesText(PersistentSkill meta)
{
	if (meta == PersistentSkill::Blasphemy)
		return _("Takes curses: Amplify Damage, Weaken, Iron Maiden, Decrepify, Lower Resist and Death Mark");
	return _("Takes spells, but not movement, channelled or minion spells");
}

std::string_view MetaSkillWord(PersistentSkill meta)
{
	return meta == PersistentSkill::Blasphemy ? _("curse") : _("spell");
}

std::string_view MetaEmptyHint(PersistentSkill meta)
{
	switch (meta) {
	case PersistentSkill::Blasphemy:
		return _("Put curse gems in its sockets: they curse every enemy around you.");
	case PersistentSkill::HoradricTotem:
		return _("Put spell gems in its sockets, then use it from the skill tray to place the totem.");
	case PersistentSkill::ReapersInvocation:
	case PersistentSkill::BarrierInvocation:
	case PersistentSkill::ElementalInvocation:
		return _("Put spell gems in its sockets: use it from the skill tray once it has the Energy.");
	default:
		return _("Put spell gems in its sockets: they cast when its Energy is full.");
	}
}

std::string MetaInsideText(const Player &player, size_t row)
{
	if (row >= NumSkillSlots)
		return {};
	const PersistentSkill meta = player.poe2.skillSlots[row].persistent();
	const std::string_view name = PersistentSkillName(meta);
	switch (meta) {
	case PersistentSkill::Blasphemy:
		return FormatRuntime(_("Curses the enemies around you at {:d}% strength"), BlasphemyStrength(player.poe2.gemLevel(row)));
	case PersistentSkill::HoradricTotem:
		return FormatRuntime(_("The totems you place with {:s} cast it"), name);
	case PersistentSkill::ReapersInvocation:
	case PersistentSkill::BarrierInvocation:
	case PersistentSkill::ElementalInvocation:
		return FormatRuntime(_("Casts when you use {:s}"), name);
	default:
		return FormatRuntime(_("Casts when {:s} is full of Energy"), name);
	}
}

int BlasphemyStrength(int gemLevel)
{
	const int level = std::clamp(gemLevel, 1, MaxGemLevel);
	return BlasphemyStrengthLevel1 + ((BlasphemyStrengthLevel20 - BlasphemyStrengthLevel1) * (level - 1) / (MaxGemLevel - 1));
}

int BlasphemyCurseCount(const Player &player)
{
	const std::optional<size_t> row = RowOf(player.poe2, SkillSlot::ForPersistent(PersistentSkill::Blasphemy));
	if (!row)
		return 0;
	return static_cast<int>(std::count_if(player.poe2.metaSkills[*row].begin(), player.poe2.metaSkills[*row].end(), IsBlasphemyCurse));
}

int HoradricTotemManaCost(int gemLevel)
{
	// POE2's Spell Totem, gem levels 1 to 20.
	constexpr std::array<int, MaxGemLevel> Cost { 18, 20, 23, 27, 30, 34, 38, 42, 47, 51, 57, 62, 68, 74, 81, 88, 95, 103, 112, 121 };
	return Cost[static_cast<size_t>(std::clamp(gemLevel, 1, MaxGemLevel) - 1)];
}

int HoradricTotemLimit(int gemLevel)
{
	return gemLevel >= HoradricTotemSecondAtLevel ? 2 : 1;
}

int MonsterPower(const Monster &monster)
{
	if (monster.isUnique())
		return 20;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Magic:
		return 2;
	case MonsterRarity::Rare:
		return 5;
	default:
		return 1;
	}
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
		return InvocationMaxEnergy;
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
	case SupportGem::CurseOnHit:
		return PersistentSkill::Blasphemy;
	case SupportGem::HoradricTotem:
		return PersistentSkill::HoradricTotem;
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
	if (!CanGoInMetaGem(player.poe2.skillSlots[row].persistent(), skill))
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
	GainEnergy(player, PersistentSkill::ReapersInvocation, ReaperEnergyNormal * MonsterPower(monster) * CentiPerEnergy, monster.position.tile, true);
}

void OnMetaCrit(Player &player, const Monster &monster, Fixed26_6 dealt)
{
	if (!gbIsPoe2 || &player != MyPlayer || FromMetaSkill(player))
		return;
	// A hit that takes all of a monster's life counts fully, and the smallest still counts a little.
	const int64_t life = std::max<int64_t>(monster.maxHitPoints.raw(), 1);
	const int percent = static_cast<int>(std::clamp<int64_t>(std::max<int64_t>(dealt.raw(), 0) * 100 / life, CritEnergyLeastPerPower, 100));
	const int energy = MonsterPower(monster) * CritEnergyPerPower * percent / 100;
	GainEnergy(player, PersistentSkill::CastOnCritical, energy * CentiPerEnergy, monster.position.tile, true);
}

void OnMetaAilment(Player &player, const Monster &monster, MetaAilment ailment)
{
	// Siphon Elements works for every skill, the meta gems' own included.
	OnSpiritElementalAilment(player, monster, ailment == MetaAilment::Ignite ? DamageType::Fire : ailment == MetaAilment::Shock ? DamageType::Lightning : DamageType::Magic);
	if (!gbIsPoe2 || &player != MyPlayer || FromMetaSkill(player))
		return;
	int energy = IgniteEnergyPerPower;
	if (ailment == MetaAilment::Freeze)
		energy = FreezeEnergyPerPower;
	else if (ailment == MetaAilment::Shock)
		energy = ShockEnergyPerPower;
	energy *= MonsterPower(monster);
	GainEnergy(player, PersistentSkill::CastOnElementalAilment, energy * CentiPerEnergy, monster.position.tile, true);
	GainEnergy(player, PersistentSkill::ElementalInvocation, energy * CentiPerEnergy, monster.position.tile, true);
}

void OnMetaMinionDied(Player &player, const Monster &minion)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	GainEnergy(player, PersistentSkill::CastOnMinionDeath, MinionDeathEnergy * CentiPerEnergy, NearestMonsterTarget(player, minion.position.tile, 8), true);
}

void OnMetaShieldLost(Player &player, Fixed26_6 lost)
{
	if (!gbIsPoe2 || &player != MyPlayer || lost <= 0)
		return;
	// In hundredths of Energy: a raw value is 1/64 of energy shield.
	const int centi = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(lost.raw()) * CentiPerEnergy / (64 * BarrierShieldPerEnergy), UINT16_MAX));
	GainEnergy(player, PersistentSkill::BarrierInvocation, centi, player.position.tile, true);
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

InvokeResult PlaceHoradricTotem(Player &player, size_t row, Point target)
{
	if (row >= NumSkillSlots || player.poe2.skillSlots[row].persistent() != PersistentSkill::HoradricTotem)
		return InvokeResult::Off;
	if (MetaSkillCount(player.poe2, row) == 0)
		return InvokeResult::NoSpells;
	if (leveltype == DTYPE_TOWN)
		return InvokeResult::Town;
	const int gemLevel = player.poe2.gemLevel(row);
	const Fixed26_6 cost = Fixed26_6::fromInt(HoradricTotemManaCost(gemLevel));
	if (player._pMana < cost)
		return InvokeResult::NoMana;
	const Fixed26_6 paid = PaySpellCostWithEnergyShield(player, cost);
	player._pMana -= paid;
	player._pManaBase -= paid;
	RedrawComponent(PanelDrawComponent::Mana);
	const SkillSlot totem = SkillSlot::ForPersistent(PersistentSkill::HoradricTotem);
	AddHoradricTotem(player, row, target, SupportZoneLimit(player, totem, HoradricTotemLimit(gemLevel)));
	TelemetryLog("meta", StrCat("Horadric Totem placed for ", cost.whole(), " mana"));
	return InvokeResult::Cast;
}

SkillSlot NextTotemSpell(const Player &player, size_t row, uint8_t &next)
{
	if (row >= NumSkillSlots || player.poe2.skillSlots[row].persistent() != PersistentSkill::HoradricTotem)
		return {};
	for (size_t tried = 0; tried < MaxSupportSockets; tried++) {
		const size_t socket = (next + tried) % MaxSupportSockets;
		const SkillSlot skill = player.poe2.metaSkills[row][socket];
		if (skill.isEmpty() || IsAnyOf(GetSkillUsability(player, skill), SkillUsability::Requirements, SkillUsability::Weapon))
			continue;
		next = static_cast<uint8_t>((socket + 1) % MaxSupportSockets);
		return skill;
	}
	return {};
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
	ProcessBlasphemy(player);
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

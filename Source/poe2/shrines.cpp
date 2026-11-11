#include "poe2/shrines.hpp"

#include <algorithm>
#include <ctime>
#include <expected>
#include <optional>

#include <magic_enum/magic_enum.hpp>

#include "automap.h"
#include "control/control.hpp"
#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "effects.h"
#include "engine/palette.h"
#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/vfx.hpp"
#include "poe2/waystones.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr int TicksPerSecond = 20;
/** @brief The first byte of the "poe2shrines" entry. */
constexpr uint8_t ShrineSaveVersion = 1;

std::vector<ShrineData> Shrines;
const ShrineData NoShrine {};

std::expected<ShrineEffect, std::string> ParseEffect(std::string_view text)
{
	if (const std::optional<ShrineEffect> effect = magic_enum::enum_cast<ShrineEffect>(text); effect && *effect != ShrineEffect::Count)
		return *effect;
	return std::unexpected("unknown shrine effect");
}

std::expected<uint8_t, std::string> ParseGlow(std::string_view text)
{
	if (const std::optional<uint8_t> glow = ParseShrineGlow(text))
		return *glow;
	return std::unexpected("unknown glow colour");
}

std::expected<uint8_t, std::string> ParseIcon(std::string_view text)
{
	if (const std::optional<SpellIcon> icon = magic_enum::enum_cast<SpellIcon>(text))
		return static_cast<uint8_t>(*icon);
	return std::unexpected("unknown spell icon");
}

bool LoadShrines()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\shrines.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the shrines: {}", Filename);
		return false;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		ShrineData shrine;
		std::string id;
		reader.readString("id", id);
		const std::optional<ShrineKind> kind = magic_enum::enum_cast<ShrineKind>(id);
		// Rows are in the order of Diablo's shrines, which the shrine objects keep.
		if (!kind || static_cast<size_t>(*kind) != Shrines.size()) {
			LogError("{}: \"{}\" is not the next shrine, {}", Filename, id, magic_enum::enum_name(static_cast<ShrineKind>(Shrines.size())));
			return false;
		}
		shrine.kind = *kind;
		reader.readString("name", shrine.name);
		reader.readString("power", shrine.power);
		reader.read("effect", shrine.effect, ParseEffect);
		reader.readInt("seconds", shrine.seconds);
		reader.read("glow", shrine.glow, ParseGlow);
		reader.read("icon", shrine.icon, ParseIcon);
		reader.readBool("refresh", shrine.refresh);
		reader.readBool("spawns", shrine.spawns);
		reader.readString("text", shrine.text);
		shrine.seconds = std::clamp(shrine.seconds, 0, 3000);
		Shrines.push_back(std::move(shrine));
	}
	if (Shrines.size() != static_cast<size_t>(ShrineKind::Count)) {
		LogError("{}: rows for {} of the {} shrines", Filename, Shrines.size(), static_cast<size_t>(ShrineKind::Count));
		return false;
	}
	return true;
}

/** @brief Whether the shrine gives a timed buff of its own (not another shrine's, nor the Town shrine's portal). */
bool GivesBuff(const ShrineData &shrine)
{
	return shrine.seconds > 0 && IsNoneOf(shrine.effect, ShrineEffect::None, ShrineEffect::Random, ShrineEffect::RandomCursed, ShrineEffect::TownPortal);
}

ShrineEffect EffectOf(const ShrineBuff &buff)
{
	return buff.active() ? GetShrineData(buff.kind).effect : ShrineEffect::None;
}

void AddEffect(ShrineBonuses &b, ShrineEffect effect, uint8_t variant)
{
	switch (effect) {
	case ShrineEffect::Echoing:
		b.echoing = true;
		break;
	case ShrineEffect::Gloom:
		b.gloom = true;
		break;
	case ShrineEffect::Hexing:
		b.hexing = true;
		break;
	case ShrineEffect::Replenishing:
		b.lifeRegenPermille += ReplenishLifePermillePerSecond;
		b.manaRegenPermille += ReplenishManaPermillePerSecond;
		break;
	case ShrineEffect::Impenetrable:
		b.armourPercent += ImpenetrableDefencesPercent;
		b.evasionPercent += ImpenetrableDefencesPercent;
		b.energyShieldPercent += ImpenetrableDefencesPercent;
		break;
	case ShrineEffect::Resistance:
		b.resistances += ResistanceShrineResistances;
		b.maxResistances += ResistanceShrineMaxResistances;
		break;
	case ShrineEffect::Diamond:
		b.alwaysCrit = true;
		break;
	case ShrineEffect::Covetous:
		b.rarityPercent += CovetousRarityPercent;
		b.quantityPercent += CovetousQuantityPercent;
		b.damageTakenPercent += CovetousDamageTakenPercent;
		break;
	case ShrineEffect::Meteoric:
		b.meteoric = true;
		break;
	case ShrineEffect::Tempest:
		b.tempest = true;
		break;
	case ShrineEffect::Freezing:
		b.freezing = true;
		break;
	case ShrineEffect::Divine:
		b.invulnerable = true;
		break;
	case ShrineEffect::Acceleration:
		b.movePercent += AccelerationMovePercent;
		b.skillSpeedPercent += AccelerationSkillSpeedPercent;
		break;
	case ShrineEffect::TempestArcs:
		b.arcs = true;
		break;
	case ShrineEffect::Greed:
		b.goldPercent += GreedGoldPercent;
		b.noEquipmentDrops = true;
		break;
	case ShrineEffect::Skeletal:
		b.skeletons += SkeletalShrineSkeletons;
		break;
	case ShrineEffect::Brutal:
		b.knockback = true;
		b.moreDamagePercent += BrutalMoreDamagePercent;
		b.stunDurationPercent += BrutalStunDurationPercent;
		break;
	case ShrineEffect::Thorned:
		b.reflectPercent += ThornedReflectPercent;
		break;
	case ShrineEffect::Enduring:
		b.lifePercent += EnduringLifePercent;
		b.areaPercent += EnduringAreaPercent;
		break;
	case ShrineEffect::EnduringII:
		b.lifePercent += EnduringIILifePercent;
		b.armourPercent += EnduringIIDefencesPercent;
		b.evasionPercent += EnduringIIDefencesPercent;
		b.energyShieldPercent += EnduringIIDefencesPercent;
		break;
	case ShrineEffect::Seeking:
		b.rarityPercent += SeekingRarityPercent;
		break;
	case ShrineEffect::AccelerationI:
		b.projectileSpeedPercent += AccelerationIProjectileSpeedPercent;
		b.skillSpeedPercent += AccelerationISkillSpeedPercent;
		break;
	case ShrineEffect::Tainted:
		b.corruptDrops = true;
		break;
	case ShrineEffect::ExtraFire:
		b.extraFirePercent += ExtraElementPercent;
		break;
	case ShrineEffect::Enlightening:
		b.experiencePercent += EnlighteningExperiencePercent;
		break;
	case ShrineEffect::GreedII:
		b.equipmentToCurrency = true;
		break;
	case ShrineEffect::ExtraLightning:
		b.extraLightningPercent += ExtraElementPercent;
		break;
	case ShrineEffect::ExtraCold:
		b.extraColdPercent += ExtraElementPercent;
		break;
	case ShrineEffect::Solar:
		if (variant == SolarNight) {
			b.coldDamagePercent += SolarElementDamagePercent;
			b.extraProjectiles += SolarNightProjectiles;
		} else {
			b.fireDamagePercent += SolarElementDamagePercent;
			b.avoidAilments = true;
		}
		break;
	default:
		break;
	}
}

void AddCurse(ShrineBonuses &b, ShrineCurse curse)
{
	switch (curse) {
	case ShrineCurse::Vulnerability:
		b.damageTakenPercent += VulnerabilityDamageTakenPercent;
		break;
	case ShrineCurse::ElementalWeakness:
		b.resistances += ElementalWeaknessResistances;
		break;
	case ShrineCurse::TemporalChains:
		b.skillSpeedPercent -= TemporalChainsSlowPercent;
		b.movePercent -= TemporalChainsSlowPercent;
		break;
	default:
		break;
	}
}

std::string SecondsText(int ticks)
{
	return StrCat((ticks + TicksPerSecond - 1) / TicksPerSecond, "s");
}

/** @brief The buffs for the play log: "Echoing 20s, Tempest 31s". */
std::string BuffsLine(const ShrineBuffState &state)
{
	std::string line;
	for (const ShrineBuff &buff : state.buffs) {
		if (!buff.active())
			continue;
		if (!line.empty())
			line += ", ";
		line += StrCat(GetShrineData(buff.kind).power, " ", SecondsText(buff.ticksLeft));
	}
	if (state.curse != ShrineCurse::None)
		line += StrCat(line.empty() ? "" : ", ", "curse ", ShrineCurseName(state.curse), " ", SecondsText(state.curseTicks));
	return line.empty() ? std::string("none") : line;
}

/** @brief Stats change with the buffs; a life buff ending keeps the share of life the hero had, so it never kills. */
void RecalcShrineStats(Player &player)
{
	const int64_t oldMax = player._pMaxHP.raw();
	const int64_t life = player._pHitPoints.raw();
	CalcPlrInv(player, true);
	const int64_t newMax = player._pMaxHP.raw();
	if (!player.hasNoLife() && oldMax > 0 && newMax < oldMax) {
		const int64_t kept = std::clamp<int64_t>(life * newMax / oldMax, 64, std::max<int64_t>(newMax, 64));
		SetPlayerHitPoints(player, Fixed26_6::fromRaw(static_cast<int32_t>(kept)));
	}
	if (&player == MyPlayer)
		RedrawEverything();
}

std::vector<Monster *> MonstersNear(Point center, int range)
{
	std::vector<Monster *> found;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.hasNoLife() || monster.isPlayerMinion() || !monster.isPossibleToHit() || monster.mode == MonsterMode::Death)
			continue;
		if (center.ExactDistance(monster.position.tile) <= range)
			found.push_back(&monster);
	}
	std::sort(found.begin(), found.end(), [&](const Monster *a, const Monster *b) {
		return center.WalkingDistance(a->position.tile) < center.WalkingDistance(b->position.tile);
	});
	return found;
}

/** @brief A storm's strike: Meteoric, Tempest, Freezing and the arcs. Local hero only. */
void Strike(Player &player, ShrineBuff &buff, ShrineEffect effect)
{
	if (buff.pulseTicks > 0) {
		buff.pulseTicks--;
		return;
	}
	const int areaLevel = std::max(CurrentAreaLevel(), 1);
	const Point hero = player.position.tile;
	switch (effect) {
	case ShrineEffect::Meteoric: {
		buff.pulseTicks = MeteorTicks;
		const std::vector<Monster *> near = MonstersNear(hero, MeteorRange);
		if (near.empty())
			return;
		const Point target = near[static_cast<size_t>(RandomIntLessThan(static_cast<int>(near.size())))]->position.tile;
		vfx::SkyStrike(target, vfx::Element::Fire);
		PlaySfxLoc(SfxID::SpellFireHit, target);
		AddPlayerBurst(target, 1, MeteorFallTicks, DamageType::Fire, MissileGraphicID::BigExplosion, Fixed26_6::fromRaw(ShrineStrikeRaw(areaLevel, MeteorLifePercent)));
		break;
	}
	case ShrineEffect::Tempest:
		buff.pulseTicks = TempestTicks;
		if (MonstersNear(hero, TempestRadius + 1).empty())
			return;
		vfx::Nova(hero, TempestRadius, vfx::Element::Lightning);
		PlaySfxLoc(SfxID::SpellLightningHit, hero);
		AddPlayerBurst(hero, TempestRadius, 0, DamageType::Lightning, MissileGraphicID::Lightning, Fixed26_6::fromRaw(ShrineStrikeRaw(areaLevel, TempestLifePercent)));
		break;
	case ShrineEffect::Freezing: {
		buff.pulseTicks = FreezingTicks;
		// Each shard flies a step further round the spiral: eight directions, one to five tiles out.
		const auto dir = static_cast<Direction>(buff.pulses % 8);
		const int distance = 1 + (buff.pulses / 2) % 5;
		buff.pulses++;
		const Point target = hero + Displacement { dir } * distance;
		if (!InDungeonBounds(target))
			return;
		vfx::Impact(target, vfx::Element::Cold, 0.8F);
		AddPlayerBurst(target, 1, 0, DamageType::Cold, MissileGraphicID::BlueFlareExplosion, Fixed26_6::fromRaw(ShrineStrikeRaw(areaLevel, FreezingLifePercent)));
		break;
	}
	case ShrineEffect::TempestArcs: {
		buff.pulseTicks = ArcTicks;
		std::vector<Monster *> near = MonstersNear(hero, ArcRange);
		if (near.size() > ArcTargets)
			near.resize(ArcTargets);
		if (!near.empty())
			PlaySfxLoc(SfxID::SpellLightning, hero);
		for (Monster *monster : near) {
			vfx::Arc(hero, monster->position.tile, vfx::Element::Lightning);
			AddPlayerBurst(monster->position.tile, 0, 0, DamageType::Lightning, MissileGraphicID::ThinLightning, Fixed26_6::fromRaw(ShrineStrikeRaw(areaLevel, ArcLifePercent)));
		}
		break;
	}
	default:
		break;
	}
}

void Replenish(Player &player, const ShrineBonuses &bonuses)
{
	if (bonuses.lifeRegenPermille > 0 && player._pHitPoints < player._pMaxHP) {
		const int32_t raw = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(player._pMaxHP.raw()) * bonuses.lifeRegenPermille / 1000 / TicksPerSecond), 1);
		player._pHitPoints = std::min(player._pHitPoints + Fixed26_6::fromRaw(raw), player._pMaxHP);
		player._pHPBase = std::min(player._pHPBase + Fixed26_6::fromRaw(raw), player._pMaxHPBase);
		RedrawComponent(PanelDrawComponent::Health);
	}
	if (bonuses.manaRegenPermille > 0 && player._pMana < player._pMaxMana) {
		const int32_t raw = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(player._pMaxMana.raw()) * bonuses.manaRegenPermille / 1000 / TicksPerSecond), 1);
		player._pMana = std::min(player._pMana + Fixed26_6::fromRaw(raw), player._pMaxMana);
		player._pManaBase = std::min(player._pManaBase + Fixed26_6::fromRaw(raw), player._pMaxManaBase);
		RedrawComponent(PanelDrawComponent::Mana);
	}
}

/** @brief The shrines Mysterious and Murphy's can give: every other buff, but not Divine. */
std::vector<ShrineKind> RandomShrinePool()
{
	std::vector<ShrineKind> pool;
	for (const ShrineData &shrine : Shrines) {
		if (GivesBuff(shrine) && shrine.effect != ShrineEffect::Divine && shrine.kind != ShrineKind::Magical2)
			pool.push_back(shrine.kind);
	}
	return pool;
}

} // namespace

std::optional<uint8_t> ParseShrineGlow(std::string_view name)
{
	if (name == "red")
		return PAL16_RED + 2;
	if (name == "orange")
		return PAL16_ORANGE + 2;
	if (name == "yellow")
		return PAL16_YELLOW + 2;
	if (name == "gold")
		return PAL16_YELLOW + 5;
	if (name == "blue")
		return PAL16_BLUE + 4;
	if (name == "iceblue")
		return PAL16_BLUE + 1;
	if (name == "white")
		return PAL16_GRAY;
	if (name == "beige")
		return PAL16_BEIGE + 2;
	if (name == "gray")
		return PAL16_GRAY + 6;
	return std::nullopt;
}

void LoadShrineData()
{
	Shrines.clear();
	if (!gbIsPoe2)
		return;
	if (!LoadShrines())
		Shrines.clear();
}

bool HasShrineData()
{
	return !Shrines.empty();
}

const ShrineData &GetShrineData(ShrineKind kind)
{
	const auto index = static_cast<size_t>(kind);
	return index < Shrines.size() ? Shrines[index] : NoShrine;
}

bool AddShrineBuff(ShrineBuffState &state, ShrineKind kind, uint8_t variant, int ticks, bool refresh)
{
	if (kind >= ShrineKind::Count || ticks <= 0)
		return false;
	const auto clamped = static_cast<uint16_t>(std::min(ticks, static_cast<int>(UINT16_MAX)));
	auto &buffs = state.buffs;
	const auto same = std::find_if(buffs.begin(), buffs.end(), [kind](const ShrineBuff &buff) { return buff.active() && buff.kind == kind; });
	if (same != buffs.end() && !refresh)
		return false;
	// The buff to drop: the same shrine's, else an empty slot, else the oldest (the first).
	auto slot = same;
	if (slot == buffs.end())
		slot = std::find_if(buffs.begin(), buffs.end(), [](const ShrineBuff &buff) { return !buff.active(); });
	if (slot == buffs.end())
		slot = buffs.begin();
	// Close the gap, oldest first, and put the new one last.
	std::rotate(slot, slot + 1, buffs.end());
	// Empty slots go before the active ones, so the newest is always last.
	std::stable_partition(buffs.begin(), buffs.end() - 1, [](const ShrineBuff &buff) { return !buff.active(); });
	ShrineBuff &fresh = buffs.back();
	fresh = {};
	fresh.kind = kind;
	fresh.variant = variant;
	fresh.ticksLeft = clamped;
	fresh.ticksTotal = clamped;
	return true;
}

bool RemoveShrineBuff(ShrineBuffState &state, ShrineKind kind)
{
	bool removed = false;
	for (ShrineBuff &buff : state.buffs) {
		if (buff.active() && buff.kind == kind) {
			buff = {};
			removed = true;
		}
	}
	if (removed)
		std::stable_partition(state.buffs.begin(), state.buffs.end(), [](const ShrineBuff &buff) { return !buff.active(); });
	return removed;
}

std::vector<ShrineKind> TickShrineBuffState(ShrineBuffState &state, bool *curseEnded)
{
	std::vector<ShrineKind> ended;
	for (ShrineBuff &buff : state.buffs) {
		if (!buff.active())
			continue;
		if (--buff.ticksLeft == 0) {
			ended.push_back(buff.kind);
			buff = {};
		}
	}
	if (!ended.empty()) {
		std::stable_partition(state.buffs.begin(), state.buffs.end(), [](const ShrineBuff &buff) { return !buff.active(); });
		state.fadeSound = true;
	}
	if (curseEnded != nullptr)
		*curseEnded = false;
	if (state.curse != ShrineCurse::None && (state.curseTicks == 0 || --state.curseTicks == 0)) {
		state.curse = ShrineCurse::None;
		state.curseTotal = 0;
		state.fadeSound = true;
		if (curseEnded != nullptr)
			*curseEnded = true;
	}
	return ended;
}

const ShrineBuff *NewestShrineBuff(const ShrineBuffState &state)
{
	for (auto it = state.buffs.rbegin(); it != state.buffs.rend(); ++it) {
		if (it->active())
			return &*it;
	}
	return nullptr;
}

size_t ShrineBuffCount(const ShrineBuffState &state)
{
	return static_cast<size_t>(std::count_if(state.buffs.begin(), state.buffs.end(), [](const ShrineBuff &buff) { return buff.active(); }));
}

std::vector<uint8_t> PackShrineBuffs(const ShrineBuffState &state)
{
	std::vector<uint8_t> data;
	const auto put16 = [&data](uint16_t value) {
		data.push_back(static_cast<uint8_t>(value & 0xFF));
		data.push_back(static_cast<uint8_t>(value >> 8));
	};
	data.push_back(ShrineSaveVersion);
	data.push_back(static_cast<uint8_t>(ShrineBuffCount(state)));
	for (const ShrineBuff &buff : state.buffs) {
		if (!buff.active())
			continue;
		data.push_back(static_cast<uint8_t>(buff.kind));
		data.push_back(buff.variant);
		put16(buff.ticksLeft);
		put16(buff.ticksTotal);
	}
	data.push_back(static_cast<uint8_t>(state.curse));
	put16(state.curseTicks);
	put16(state.curseTotal);
	return data;
}

bool UnpackShrineBuffs(ShrineBuffState &state, std::span<const uint8_t> data)
{
	state = {};
	if (data.size() < 2 || data[0] == 0)
		return false;
	// A newer build's entry is read as far as this one knows: its fields come first.
	size_t pos = 1;
	const size_t count = data[pos++];
	const auto get16 = [&](size_t at) { return static_cast<uint16_t>(data[at] | (data[at + 1] << 8)); };
	if (count > 8 || data.size() < pos + (count * 6) + 5) {
		state = {};
		return false;
	}
	for (size_t i = 0; i < count; i++) {
		const auto kind = static_cast<ShrineKind>(data[pos]);
		const uint8_t variant = data[pos + 1];
		const uint16_t left = get16(pos + 2);
		const uint16_t total = get16(pos + 4);
		pos += 6;
		if (kind >= ShrineKind::Count || left == 0)
			continue;
		if (AddShrineBuff(state, kind, variant, left))
			state.buffs.back().ticksTotal = std::max(total, left);
	}
	const auto curse = static_cast<ShrineCurse>(data[pos]);
	const uint16_t curseTicks = get16(pos + 1);
	const uint16_t curseTotal = get16(pos + 3);
	if (curse != ShrineCurse::None && curse < ShrineCurse::Count && curseTicks > 0) {
		state.curse = curse;
		state.curseTicks = curseTicks;
		state.curseTotal = std::max(curseTotal, curseTicks);
	}
	return true;
}

int ShrineCapForLevel(int level)
{
	return level <= EarlyShrineFloors ? EarlyFloorShrineCap : DeepFloorShrineCap;
}

bool IsShrineBossFloor(int level, bool isSetLevel, bool inMap)
{
	return isSetLevel || inMap || level == 16 || level == 24;
}

bool OnShrineBossFloor()
{
	return IsShrineBossFloor(currlevel, setlevel, ActiveMapTier() > 0);
}

std::vector<ShrineKind> SpawnableShrines(int level, bool bossFloor, int shrineCount)
{
	std::vector<ShrineKind> kinds;
	for (int i = 0; i < shrineCount && i < static_cast<int>(Shrines.size()); i++) {
		const ShrineData &shrine = Shrines[static_cast<size_t>(i)];
		if (!shrine.spawns)
			continue;
		if (shrine.effect == ShrineEffect::Divine && (level <= EarlyShrineFloors || bossFloor))
			continue;
		kinds.push_back(shrine.kind);
	}
	return kinds;
}

ShrineGrant ResolveShrine(ShrineKind touched, DiabloGenerator &rng, bool night)
{
	ShrineGrant grant;
	const ShrineData &shrine = GetShrineData(touched);
	ShrineKind kind = touched;
	if (IsAnyOf(shrine.effect, ShrineEffect::Random, ShrineEffect::RandomCursed)) {
		const std::vector<ShrineKind> pool = RandomShrinePool();
		if (pool.empty())
			return grant;
		kind = pool[static_cast<size_t>(rng.randomIntLessThan(static_cast<int32_t>(pool.size())))];
		if (shrine.effect == ShrineEffect::RandomCursed)
			grant.curse = static_cast<ShrineCurse>(1 + rng.randomIntLessThan(static_cast<int32_t>(ShrineCurse::Count) - 1));
	}
	if (!GivesBuff(GetShrineData(kind)))
		return grant;
	grant.kind = kind;
	if (GetShrineData(kind).effect == ShrineEffect::Solar)
		grant.variant = night ? SolarNight : SolarDay;
	return grant;
}

bool ShrineClockSaysNight()
{
	const time_t now = time(nullptr);
	const std::tm *local = localtime(&now);
	const int hour = local != nullptr ? local->tm_hour : 20;
	return hour >= 20 || hour < 6;
}

void ApplyShrineGrant(Player &player, const ShrineGrant &grant)
{
	if (!gbIsPoe2)
		return;
	ShrineBuffState &state = player.poe2.shrines;
	bool changed = false;
	if (grant.kind < ShrineKind::Count) {
		const ShrineData &shrine = GetShrineData(grant.kind);
		changed = AddShrineBuff(state, grant.kind, grant.variant, shrine.seconds * TicksPerSecond, shrine.refresh);
		if (shrine.effect == ShrineEffect::Seeking && &player == MyPlayer) {
			// It still reveals the map, as Diablo's Secluded shrine did.
			for (int x = 0; x < DMAXX; x++)
				for (int y = 0; y < DMAXY; y++)
					UpdateAutomapExplorer({ x, y }, MAP_EXP_SHRINE);
		}
	}
	if (grant.curse != ShrineCurse::None && grant.curse < ShrineCurse::Count) {
		state.curse = grant.curse;
		state.curseTicks = ShrineCurseTicks;
		state.curseTotal = ShrineCurseTicks;
		changed = true;
	}
	if (&player == MyPlayer) {
		const std::string power = grant.kind < ShrineKind::Count ? GetShrineData(grant.kind).power : std::string("nothing");
		TelemetryLog("shrine", StrCat("took ", power, changed ? "" : " (no change)", grant.curse != ShrineCurse::None ? StrCat(" and the curse ", ShrineCurseName(grant.curse)) : std::string(), "; buffs: ", BuffsLine(state)));
	}
	if (changed)
		RecalcShrineStats(player);
}

std::string ShrineGrantLine(const ShrineGrant &grant)
{
	if (grant.kind >= ShrineKind::Count)
		return {};
	const ShrineData &shrine = GetShrineData(grant.kind);
	std::string line = StrCat(shrine.power, ": ", shrine.text);
	if (shrine.effect == ShrineEffect::Solar)
		line = StrCat(shrine.power, grant.variant == SolarNight ? _(" (night): +50% cold damage and +2 projectiles") : _(" (day): +50% fire damage and avoid ailments"));
	if (grant.curse != ShrineCurse::None)
		line += StrCat(". ", _("Cursed with"), " ", ShrineCurseName(grant.curse));
	return line;
}

std::string ShrineHoverText(ShrineKind kind)
{
	const ShrineData &shrine = GetShrineData(kind);
	if (shrine.kind >= ShrineKind::Count)
		return {};
	if (shrine.seconds <= 0)
		return StrCat(shrine.power, ": ", shrine.text);
	return StrCat(shrine.power, ": ", shrine.text, " (", shrine.seconds, "s)");
}

std::pair<std::string, std::string> ShrineHoverLines(ShrineKind kind)
{
	const ShrineData &shrine = GetShrineData(kind);
	if (shrine.kind >= ShrineKind::Count)
		return {};
	std::string title = shrine.seconds <= 0 ? std::string(shrine.power) : StrCat(shrine.power, " (", shrine.seconds, "s)");
	return { std::move(title), std::string(shrine.text) };
}

void ProcessShrineBuffs(Player &player)
{
	if (!gbIsPoe2)
		return;
	ShrineBuffState &state = player.poe2.shrines;
	if (ShrineBuffCount(state) == 0 && state.curse == ShrineCurse::None)
		return;
	const bool local = &player == MyPlayer;
	if (local && !player.hasNoLife()) {
		Replenish(player, ShrineStateBonuses(state));
		if (leveltype != DTYPE_TOWN) {
			for (ShrineBuff &buff : state.buffs) {
				const ShrineEffect effect = EffectOf(buff);
				if (IsAnyOf(effect, ShrineEffect::Meteoric, ShrineEffect::Tempest, ShrineEffect::Freezing, ShrineEffect::TempestArcs))
					Strike(player, buff, effect);
			}
		}
	}
	bool curseEnded = false;
	const std::vector<ShrineKind> ended = TickShrineBuffState(state, &curseEnded);
	if (ended.empty() && !curseEnded)
		return;
	if (local) {
		for (const ShrineKind kind : ended)
			TelemetryLog("shrine", StrCat(GetShrineData(kind).power, " ended after its ", GetShrineData(kind).seconds, "s; buffs: ", BuffsLine(state)));
		if (curseEnded)
			TelemetryLog("shrine", StrCat("the curse ended; buffs: ", BuffsLine(state)));
		if (state.fadeSound)
			PlaySFX(SfxID::SpellEnd);
	}
	state.fadeSound = false;
	RecalcShrineStats(player);
}

void OnShrineLevelEntered(Player &player)
{
	if (!gbIsPoe2 || !OnShrineBossFloor())
		return;
	bool removed = false;
	for (const ShrineBuff &buff : player.poe2.shrines.buffs) {
		if (EffectOf(buff) == ShrineEffect::Divine)
			removed = true;
	}
	if (!removed)
		return;
	for (size_t i = 0; i < static_cast<size_t>(ShrineKind::Count); i++) {
		if (GetShrineData(static_cast<ShrineKind>(i)).effect == ShrineEffect::Divine)
			RemoveShrineBuff(player.poe2.shrines, static_cast<ShrineKind>(i));
	}
	if (&player == MyPlayer)
		TelemetryLog("shrine", StrCat("Divine ended on a boss floor; buffs: ", BuffsLine(player.poe2.shrines)));
	RecalcShrineStats(player);
}

ShrineBonuses ShrineStateBonuses(const ShrineBuffState &state)
{
	ShrineBonuses bonuses;
	for (const ShrineBuff &buff : state.buffs)
		AddEffect(bonuses, EffectOf(buff), buff.variant);
	AddCurse(bonuses, state.curse);
	return bonuses;
}

ShrineBonuses GetShrineBonuses(const Player &player)
{
	if (!gbIsPoe2)
		return {};
	return ShrineStateBonuses(player.poe2.shrines);
}

bool HasShrineEffect(const Player &player, ShrineEffect effect)
{
	if (!gbIsPoe2)
		return false;
	return std::any_of(player.poe2.shrines.buffs.begin(), player.poe2.shrines.buffs.end(), [effect](const ShrineBuff &buff) { return EffectOf(buff) == effect; });
}

int ShrineSkeletonLevel(const Player &player)
{
	return std::clamp(static_cast<int>(player.getCharacterLevel()) / 3, 1, 20);
}

int32_t ShrineStrikeRaw(int areaLevel, int lifePercent)
{
	int life = MonsterLevelLife(std::max(areaLevel, 1));
	if (life <= 0)
		life = 20 + (6 * std::max(areaLevel, 1));
	return static_cast<int32_t>(std::clamp<int64_t>(static_cast<int64_t>(life) * lifePercent / 100, 1, INT32_MAX / 128) * 64);
}

void OnShrineKill(Player &player, const Monster &monster)
{
	if (&player != MyPlayer || !GetShrineBonuses(player).gloom || monster.isPlayerMinion())
		return;
	if (GenerateRnd(100) >= GloomChancePercent)
		return;
	const int64_t raw = static_cast<int64_t>(monster.maxHitPoints.raw()) * GloomLifePercent / 100;
	const Point tile = monster.position.tile;
	vfx::Impact(tile, vfx::Element::Blood, 1.6F);
	PlaySfxLoc(SfxID::SpellBloodStarHit, tile);
	AddPlayerBurst(tile, GloomRadius, 0, DamageType::Physical, MissileGraphicID::BloodStarExplosion, Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(raw, 64, INT32_MAX / 2))));
}

void ShrineReflectHit(Player &player, Monster &monster, int32_t damageTakenRaw)
{
	if (&player != MyPlayer || damageTakenRaw <= 0 || monster.hasNoLife() || monster.mode == MonsterMode::Death)
		return;
	const int percent = GetShrineBonuses(player).reflectPercent;
	if (percent <= 0)
		return;
	const Fixed26_6 reflected = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(damageTakenRaw) * percent / 100));
	if (reflected <= 0)
		return;
	TelemetryNoteDamageSource("Thorned shrine");
	ApplyMonsterDamage(DamageType::Physical, monster, reflected);
	vfx::ThornsHit(monster);
	monster.tag(player);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
}

void OnShrineHit(Player &player, Monster &monster)
{
	if (&player != MyPlayer || monster.hasNoLife() || !GetShrineBonuses(player).hexing)
		return;
	if (GenerateRnd(100) >= HexingChancePercent)
		return;
	constexpr WeaponSkill Curses[] = { WeaponSkill::Weaken, WeaponSkill::IronMaiden, WeaponSkill::Decrepify, WeaponSkill::LowerResist, WeaponSkill::Despair };
	ApplyTableCurse(player, monster, Curses[RandomIntLessThan(static_cast<int>(std::size(Curses)))], 100);
}

std::optional<uint8_t> ShrineGlowColor(const Player &player)
{
	if (!gbIsPoe2)
		return std::nullopt;
	const ShrineBuff *newest = NewestShrineBuff(player.poe2.shrines);
	if (newest == nullptr)
		return std::nullopt;
	// Two shades lighter and back, about twice a second, from the base colour of the glow's ramp.
	constexpr uint8_t Pulse[] = { 0, 1, 2, 1 };
	const uint8_t base = GetShrineData(newest->kind).glow;
	const uint8_t shade = Pulse[(newest->ticksLeft / 3) % 4];
	return static_cast<uint8_t>((base & 0x0F) >= shade ? base - shade : base);
}

std::string ShrineBuffTooltip(const ShrineBuff &buff)
{
	const ShrineData &shrine = GetShrineData(buff.kind);
	std::string text = shrine.text;
	if (shrine.effect == ShrineEffect::Solar)
		text = buff.variant == SolarNight ? _("Night: +50% cold damage and +2 projectiles") : _("Day: +50% fire damage and avoid ailments");
	return StrCat(text, " (", SecondsText(buff.ticksLeft), ")");
}

std::string ShrineCurseName(ShrineCurse curse)
{
	switch (curse) {
	case ShrineCurse::Vulnerability:
		return std::string(_("Vulnerability"));
	case ShrineCurse::ElementalWeakness:
		return std::string(_("Elemental Weakness"));
	case ShrineCurse::TemporalChains:
		return std::string(_("Temporal Chains"));
	default:
		return {};
	}
}

std::string ShrineCurseTooltip(ShrineCurse curse, int ticksLeft)
{
	std::string text;
	switch (curse) {
	case ShrineCurse::Vulnerability:
		text = _("You take 20% more damage");
		break;
	case ShrineCurse::ElementalWeakness:
		text = _("-20% to elemental resistances");
		break;
	case ShrineCurse::TemporalChains:
		text = _("20% less skill and movement speed");
		break;
	default:
		break;
	}
	return StrCat(text, " (", SecondsText(ticksLeft), ")");
}

} // namespace devilution

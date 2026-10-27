#include "poe2/attack_speed.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/skill_looks.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "tables/playerdat.hpp"
#include "tables/spelldat.h"
#include "utils/is_of.hpp"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr std::string_view Filename = "txtdata\\poe2\\skill_speed.tsv";
constexpr int TicksPerSecond = 20;

/** @brief The most flat time a row may add, in milliseconds. */
constexpr int MaxAddedMs = 3000;

std::unordered_map<std::string, SkillSpeedRow> SkillSpeeds;
std::vector<std::string> UnknownKeys;
bool Loaded = false;

std::string_view StepName(SkillStep step)
{
	switch (step) {
	case SkillStep::Backhand:
		return "backhand";
	case SkillStep::Finisher:
		return "finisher";
	default:
		return {};
	}
}

/** @brief Whether the skill strikes in that step: Double Swing's backhand, Fists of Thunder's finisher. */
bool SkillHasStep(SkillSlot skill, SkillStep step)
{
	if (skill.kind != SkillKind::Attack)
		return false;
	switch (step) {
	case SkillStep::Backhand:
		return skill.attack() == WeaponSkill::Flurry;
	case SkillStep::Finisher:
		return skill.attack() == WeaponSkill::FistsOfThunder;
	default:
		return false;
	}
}

/** @brief Seconds with up to three decimals, like 0.75, in milliseconds; -1 if it isn't one. */
int ParseMilliseconds(std::string_view text)
{
	int ms = 0;
	int decimals = -1;
	for (const char c : text) {
		if (c == '.' && decimals < 0) {
			decimals = 0;
			continue;
		}
		if (c < '0' || c > '9' || decimals >= 3 || ms > 1000000)
			return -1;
		ms = (ms * 10) + (c - '0');
		if (decimals >= 0)
			decimals++;
	}
	for (int i = std::max(decimals, 0); i < 3; i++)
		ms *= 10;
	return ms;
}

/** @brief A column of seconds in milliseconds: 0 when empty, logged when it isn't a time. */
int ReadSeconds(std::string_view text, std::string_view name, std::string_view column)
{
	if (text.empty())
		return 0;
	const int ms = ParseMilliseconds(text);
	if (ms <= 0) {
		LogError("Bad {} in {} for {}: {}", column, Filename, name, text);
		return 0;
	}
	return ms;
}

void Load()
{
	SkillSpeeds.clear();
	UnknownKeys.clear();
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		// Not marked loaded: the mod's files may not be mounted yet, so try again next time.
		LogError("Couldn't load the skill speeds: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string name;
		std::string castTime;
		std::string attackTime;
		SkillSpeedRow speed;
		reader.readString("name", name);
		reader.readOptionalInt("attack_speed_pct", speed.attackPercent);
		reader.readString("cast_time_s", castTime);
		reader.readString("attack_time_s", attackTime);
		reader.readOptionalInt("added_ms", speed.addedMs);
		reader.readOptionalInt("distance_ms", speed.distanceMs);
		if (name.empty())
			continue;
		speed.attackPercent = std::clamp(speed.attackPercent, 10, 1000);
		speed.castMs = ReadSeconds(castTime, name, "cast time");
		speed.attackMs = ReadSeconds(attackTime, name, "attack time");
		speed.addedMs = std::clamp(speed.addedMs, 0, MaxAddedMs);
		speed.distanceMs = std::clamp(speed.distanceMs, 0, MaxAddedMs);
		if (!IsKnownSkillSpeedKey(name)) {
			LogError("{} has a row for {}, which is no skill", Filename, name);
			UnknownKeys.push_back(name);
		}
		SkillSpeeds[name] = speed;
	}
	Loaded = true;
}

const SkillSpeedRow *FindSkillSpeed(const std::string &key)
{
	if (!Loaded && gbIsPoe2)
		Load();
	const auto it = SkillSpeeds.find(key);
	return it != SkillSpeeds.end() ? &it->second : nullptr;
}

/** @brief Milliseconds in 1/65536 game ticks. */
uint64_t MillisecondsToTime(uint64_t ms)
{
	return ((ms * TicksPerSecond * ActionTimeOneTick) + 500) / 1000;
}

/** @brief AttackTime before it's held to MinActionTicks and MaxActionTicks. */
uint64_t RawAttackTime(const Player &player, SkillSlot skill, SkillStep step)
{
	const SkillSpeedRow *row = FindSkillSpeedRow(skill, step);
	const uint64_t increased = static_cast<uint64_t>(std::max(100 + IncreasedAttackSpeedPercent(player, skill), 10));
	uint64_t time;
	if (row != nullptr && row->attackMs > 0) {
		// A fixed attack time, as POE2's shield skills have: the weapon doesn't change it, increased attack speed does.
		const uint64_t divisor = 1000 * increased;
		time = ((static_cast<uint64_t>(row->attackMs) * TicksPerSecond * ActionTimeOneTick * 100) + (divisor / 2)) / divisor;
	} else {
		// aps x (1 + increased) x skill speed, as hundredths x three percents: attacks a second x 10^8.
		const uint64_t weapon = static_cast<uint64_t>(std::max(WeaponAttacksPerSecond(player), 10));
		const uint64_t skillSpeed = static_cast<uint64_t>(row != nullptr ? row->attackPercent : 100);
		const uint64_t rate = weapon * 100 * increased * skillSpeed;
		constexpr uint64_t Numerator = static_cast<uint64_t>(TicksPerSecond) * 100000000ULL * ActionTimeOneTick;
		time = (Numerator + (rate / 2)) / rate;
	}
	// POE2 adds flat time after the speed, so no speed shortens it.
	if (row != nullptr)
		time += MillisecondsToTime(static_cast<uint64_t>(row->addedMs));
	return time;
}

/** @brief CastTime before it's held to MinActionTicks and MaxActionTicks. */
uint64_t RawCastTime(const Player &player, SkillSlot skill, bool learnedSpell)
{
	const SkillSpeedRow *row = FindSkillSpeedRow(skill);
	int baseMs = row != nullptr ? row->castMs : 0;
	if (baseMs == 0) {
		// A held spell recast on its cast frame (the time to it); a spell-like skill played its whole animation.
		const PlayerAnimData &data = GetPlayerAnimDataForClass(player._pClass);
		const int ticks = skill.kind == SkillKind::Spell ? data.castingActionFrame : data.castingFrames - 1;
		baseMs = std::max(ticks, 1) * 1000 / TicksPerSecond;
	}
	const uint64_t increased = static_cast<uint64_t>(std::max(100 + IncreasedCastSpeedPercent(player, skill, learnedSpell), 10));
	const uint64_t time = static_cast<uint64_t>(baseMs) * TicksPerSecond * ActionTimeOneTick * 100;
	const uint64_t divisor = 1000 * increased;
	uint64_t cast = (time + (divisor / 2)) / divisor;
	// Comet's extra second comes after cast speed, as in POE2.
	if (row != nullptr)
		cast += MillisecondsToTime(static_cast<uint64_t>(row->addedMs));
	return cast;
}

/** @brief SkillUseTime before it's held to MinActionTicks and MaxActionTicks. */
uint64_t RawSkillUseTime(const Player &player, SkillSlot skill, bool castAnimation, SkillStep step, int distancePercent)
{
	uint64_t time = IsCastTimed(skill, castAnimation) ? RawCastTime(player, skill, true) : RawAttackTime(player, skill, step);
	if (const SkillSpeedRow *row = FindSkillSpeedRow(skill, step); row != nullptr && row->distanceMs > 0)
		time += MillisecondsToTime(static_cast<uint64_t>(row->distanceMs) * static_cast<uint64_t>(std::clamp(distancePercent, 0, 100)) / 100);
	return time;
}

bool IsWeapon(const Item &item)
{
	return !item.isEmpty() && IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe, ItemType::Mace, ItemType::Bow, ItemType::Staff);
}

/** @brief The weapon the hero attacks with, as HeldWeapon finds it, or nullptr when unarmed. */
const Item *AttackWeapon(const Player &player)
{
	for (const inv_body_loc hand : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
		if (IsWeapon(player.InvBody[hand]))
			return &player.InvBody[hand];
	}
	return nullptr;
}

/** @brief The weapon's own increased attack speed, as POE2 applies it to the weapon alone. */
int ItemLocalAttackSpeedPercent(const Item &item)
{
	if (item._iMagical != ITEM_QUALITY_NORMAL && !item._iIdentified)
		return 0;
	return item._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::AttackSpeed)];
}

/** @brief The weapon's own increased attack speed if the hero's stats count it (CalcPlrItemVals: usable and identified). */
int CountedLocalAttackSpeedPercent(const Player &player)
{
	const Item *weapon = AttackWeapon(player);
	return weapon != nullptr && weapon->_iStatFlag ? ItemLocalAttackSpeedPercent(*weapon) : 0;
}

/** @brief Diablo's quick to fastest attack, in percent; Hellfire's bows never had them. */
int ItemEffectAttackSpeedPercent(const Player &player)
{
	const ItemSpecialEffect flags = player._pIFlags;
	int frames = 0;
	if (player.UsesRangedWeapon()) {
		if (!gbIsHellfire)
			frames = HasAnyOf(flags, ItemSpecialEffect::FastAttack) ? 2 : (HasAnyOf(flags, ItemSpecialEffect::QuickAttack) ? 1 : 0);
	} else if (HasAnyOf(flags, ItemSpecialEffect::FastestAttack)) {
		frames = 4;
	} else if (HasAnyOf(flags, ItemSpecialEffect::FasterAttack)) {
		frames = 3;
	} else if (HasAnyOf(flags, ItemSpecialEffect::FastAttack)) {
		frames = 2;
	} else if (HasAnyOf(flags, ItemSpecialEffect::QuickAttack)) {
		frames = 1;
	}
	return frames * AttackSpeedPercentPerFrame;
}

uint32_t ClampActionTime(uint64_t time)
{
	return static_cast<uint32_t>(std::clamp<uint64_t>(time, MinActionTicks * ActionTimeOneTick, MaxActionTicks * ActionTimeOneTick));
}

} // namespace

int ItemBaseAttacksPerSecond(const Item &item)
{
	if (!IsWeapon(item))
		return 0;
	if (const BaseMeta *base = ItemBaseMeta(item); base != nullptr && base->aps > 0)
		return base->aps;
	const bool twoHanded = item._iLoc == ILOC_TWOHAND;
	switch (item._itype) {
	case ItemType::Sword:
		if (item._iCurs == ICURS_DAGGER)
			return DefaultDaggerAps;
		return twoHanded ? DefaultTwoHandSwordAps : DefaultOneHandSwordAps;
	case ItemType::Axe:
		return twoHanded ? DefaultTwoHandAxeAps : DefaultOneHandAxeAps;
	case ItemType::Mace:
		return twoHanded ? DefaultTwoHandMaceAps : DefaultOneHandMaceAps;
	case ItemType::Bow:
		return DefaultBowAps;
	default:
		return DefaultStaffAps;
	}
}

int ItemAttacksPerSecond(const Item &item)
{
	const int base = ItemBaseAttacksPerSecond(item);
	const int increased = std::max(ItemLocalAttackSpeedPercent(item), -90);
	return ((base * (100 + increased)) + 50) / 100;
}

int WeaponAttacksPerSecond(const Player &player)
{
	const Item *weapon = AttackWeapon(player);
	if (weapon == nullptr)
		return UnarmedAps;
	const int increased = std::max(CountedLocalAttackSpeedPercent(player), -90);
	return ((ItemBaseAttacksPerSecond(*weapon) * (100 + increased)) + 50) / 100;
}

SkillSlot AttackSkillSlot(const Player &player, WeaponSkill skill)
{
	return skill != WeaponSkill::None ? SkillSlot::ForAttack(skill) : DefaultSkillSlot(player);
}

int IncreasedAttackSpeedPercent(const Player &player, SkillSlot skill)
{
	// The tree and the gear, less the weapon's own (WeaponAttacksPerSecond holds it); the uniques', War Banner's and
	// the auras'; then the old frame sources.
	int percent = player.poe2.attackSpeedPercent - CountedLocalAttackSpeedPercent(player);
	percent += UniqueAttackSpeedPercent(player) + BannerAttackSpeedPercent(player) + SpiritAuraSpeedPercent(player);
	percent += ItemEffectAttackSpeedPercent(player);
	percent += player.poe2.frenzyCharges * FrenzyChargeAttackSpeedPercent;
	if (skill.kind == SkillKind::Attack || skill.kind == SkillKind::Default)
		percent += SupportAttackSkippedFrames(player, skill.attack()) * AttackSpeedPercentPerFrame;
	return percent;
}

int IncreasedCastSpeedPercent(const Player &player, SkillSlot skill, bool learnedSpell)
{
	// Focused bases' reduced cast speed only cancels speed from elsewhere, as before.
	int percent = std::max<int>(player.poe2.castSpeedPercent, 0) + SpiritAuraSpeedPercent(player);
	if (learnedSpell && skill.kind == SkillKind::Spell)
		percent += SupportCastSkippedFrames(player, skill.spell()) * AttackSpeedPercentPerFrame;
	return percent;
}

int SkillAttackSpeedPercent(SkillSlot skill, SkillStep step)
{
	const SkillSpeedRow *speed = FindSkillSpeedRow(skill, step);
	return speed != nullptr ? speed->attackPercent : 100;
}

int SkillCastTimeMs(SkillSlot skill)
{
	const SkillSpeedRow *speed = FindSkillSpeedRow(skill);
	return speed != nullptr ? speed->castMs : 0;
}

const SkillSpeedRow *FindSkillSpeedRow(SkillSlot skill, SkillStep step)
{
	if (step != SkillStep::Main) {
		if (const SkillSpeedRow *row = FindSkillSpeed(SkillSpeedKey(skill, step)); row != nullptr)
			return row;
	}
	return FindSkillSpeed(SkillSpeedKey(skill));
}

std::string SkillSpeedKey(SkillSlot skill, SkillStep step)
{
	std::string key = skill.kind == SkillKind::Default ? StrCat("Default ", magic_enum::enum_name(static_cast<DefaultAttack>(skill.id))) : SkillLookKey(skill);
	if (step != SkillStep::Main)
		StrAppend(key, ":", StepName(step));
	return key;
}

uint32_t AttackTime(const Player &player, SkillSlot skill, SkillStep step)
{
	return ClampActionTime(RawAttackTime(player, skill, step));
}

uint32_t CastTime(const Player &player, SkillSlot skill, bool learnedSpell)
{
	return ClampActionTime(RawCastTime(player, skill, learnedSpell));
}

bool IsCastTimed(SkillSlot skill, bool castAnimation)
{
	const SkillSpeedRow *row = FindSkillSpeedRow(skill);
	if (row != nullptr && row->attackMs > 0)
		return false;
	if ((row != nullptr && row->castMs > 0) || skill.kind == SkillKind::Spell)
		return true;
	// A row with only an attack speed times an attack, whatever it plays.
	return row == nullptr && castAnimation;
}

uint32_t SkillUseTime(const Player &player, SkillSlot skill, bool castAnimation, SkillStep step, int distancePercent)
{
	return ClampActionTime(RawSkillUseTime(player, skill, castAnimation, step, distancePercent));
}

int ShortestActionPercent(const Player &player, SkillSlot skill, bool castAnimation, SkillStep step)
{
	const uint64_t raw = std::max<uint64_t>(RawSkillUseTime(player, skill, castAnimation, step, 0), 1);
	const uint64_t held = ClampActionTime(raw);
	return held > raw ? static_cast<int>(((held * 100) + (raw / 2)) / raw) : 100;
}

int TakeActionTicks(Player &player, uint32_t time)
{
	const uint32_t total = player.poe2.actionTimeRemainder + time;
	int ticks = static_cast<int>(total >> ActionTimeFractionBits);
	player.poe2.actionTimeRemainder = static_cast<uint16_t>(total & (ActionTimeOneTick - 1));
	if (ticks < MinActionTicks || ticks > MaxActionTicks) {
		ticks = std::clamp(ticks, MinActionTicks, MaxActionTicks);
		player.poe2.actionTimeRemainder = 0;
	}
	return ticks;
}

int ActionTicksToActionFrame(int ticks, int actionFrame, int frames)
{
	const int span = std::max(frames - 1, 1);
	const int toAction = ((2 * ticks * actionFrame) + span) / (2 * span);
	return std::clamp(toAction, 2, std::max(ticks - 1, 2));
}

double ActionSeconds(uint32_t time)
{
	return static_cast<double>(time) / ActionTimeOneTick / TicksPerSecond;
}

void SetSkillSpeed(const std::string &key, int attackPercent, int castMs)
{
	SkillSpeedRow row;
	row.attackPercent = attackPercent;
	row.castMs = castMs;
	SetSkillSpeed(key, row);
}

void SetSkillSpeed(const std::string &key, const SkillSpeedRow &row)
{
	if (!Loaded && gbIsPoe2)
		Load();
	SkillSpeedRow &set = SkillSpeeds[key];
	set = row;
	set.attackPercent = std::clamp(row.attackPercent, 10, 1000);
	set.castMs = std::max(row.castMs, 0);
	set.attackMs = std::max(row.attackMs, 0);
	set.addedMs = std::clamp(row.addedMs, 0, MaxAddedMs);
	set.distanceMs = std::clamp(row.distanceMs, 0, MaxAddedMs);
}

std::vector<std::string> UnknownSkillSpeedKeys()
{
	if (!Loaded && gbIsPoe2)
		Load();
	return UnknownKeys;
}

bool IsKnownSkillSpeedKey(std::string_view key)
{
	std::string_view base = key;
	SkillStep step = SkillStep::Main;
	if (const size_t colon = key.find(':'); colon != std::string_view::npos) {
		base = key.substr(0, colon);
		const std::string_view stepName = key.substr(colon + 1);
		step = stepName == StepName(SkillStep::Backhand) ? SkillStep::Backhand : (stepName == StepName(SkillStep::Finisher) ? SkillStep::Finisher : SkillStep::Main);
		if (step == SkillStep::Main)
			return false;
	}
	const auto matches = [&](SkillSlot skill) {
		return SkillSpeedKey(skill) == base && (step == SkillStep::Main || SkillHasStep(skill, step));
	};
	for (int i = 1; i <= static_cast<int>(WeaponSkill::Last); i++) {
		if (matches(SkillSlot::ForAttack(static_cast<WeaponSkill>(i))))
			return true;
	}
	for (const DefaultAttack attack : magic_enum::enum_values<DefaultAttack>()) {
		if (matches(SkillSlot::ForDefault(static_cast<uint8_t>(attack))))
			return true;
	}
	constexpr std::string_view SpellPrefix = "Spell ";
	if (step == SkillStep::Main && base.starts_with(SpellPrefix)) {
		const auto spell = magic_enum::enum_cast<SpellID>(base.substr(SpellPrefix.size()));
		return spell.has_value() && IsNoneOf(*spell, SpellID::Null, SpellID::Invalid);
	}
	return false;
}

void LoadSkillSpeedData()
{
	SkillSpeeds.clear();
	Loaded = false;
	if (gbIsPoe2)
		Load();
}

} // namespace devilution

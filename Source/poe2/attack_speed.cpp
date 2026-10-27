#include "poe2/attack_speed.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

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
#include "utils/is_of.hpp"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr std::string_view Filename = "txtdata\\poe2\\skill_speed.tsv";
constexpr int TicksPerSecond = 20;

struct SkillSpeed {
	int attackPercent = 100;
	/** @brief 0 for the default cast time. */
	int castMs = 0;
};

std::unordered_map<std::string, SkillSpeed> SkillSpeeds;
bool Loaded = false;

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

void Load()
{
	SkillSpeeds.clear();
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
		SkillSpeed speed;
		reader.readString("name", name);
		reader.readOptionalInt("attack_speed_pct", speed.attackPercent);
		reader.readString("cast_time_s", castTime);
		if (name.empty())
			continue;
		speed.attackPercent = std::clamp(speed.attackPercent, 10, 1000);
		if (!castTime.empty()) {
			speed.castMs = ParseMilliseconds(castTime);
			if (speed.castMs <= 0) {
				LogError("Bad cast time in {} for {}: {}", Filename, name, castTime);
				speed.castMs = 0;
			}
		}
		SkillSpeeds[name] = speed;
	}
	Loaded = true;
}

const SkillSpeed *FindSkillSpeed(SkillSlot skill)
{
	if (!Loaded && gbIsPoe2)
		Load();
	const auto it = SkillSpeeds.find(SkillSpeedKey(skill));
	return it != SkillSpeeds.end() ? &it->second : nullptr;
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

int SkillAttackSpeedPercent(SkillSlot skill)
{
	const SkillSpeed *speed = FindSkillSpeed(skill);
	return speed != nullptr ? speed->attackPercent : 100;
}

int SkillCastTimeMs(SkillSlot skill)
{
	const SkillSpeed *speed = FindSkillSpeed(skill);
	return speed != nullptr ? speed->castMs : 0;
}

std::string SkillSpeedKey(SkillSlot skill)
{
	if (skill.kind == SkillKind::Default)
		return StrCat("Default ", magic_enum::enum_name(static_cast<DefaultAttack>(skill.id)));
	return SkillLookKey(skill);
}

uint32_t AttackTime(const Player &player, SkillSlot skill)
{
	// aps x (1 + increased) x skill speed, as hundredths x three percents: attacks a second x 10^8.
	const uint64_t weapon = static_cast<uint64_t>(std::max(WeaponAttacksPerSecond(player), 10));
	const uint64_t increased = static_cast<uint64_t>(std::max(100 + IncreasedAttackSpeedPercent(player, skill), 10));
	const uint64_t skillSpeed = static_cast<uint64_t>(SkillAttackSpeedPercent(skill));
	const uint64_t rate = weapon * 100 * increased * skillSpeed;
	constexpr uint64_t Numerator = static_cast<uint64_t>(TicksPerSecond) * 100000000ULL * ActionTimeOneTick;
	return ClampActionTime((Numerator + (rate / 2)) / rate);
}

uint32_t CastTime(const Player &player, SkillSlot skill, bool learnedSpell)
{
	int baseMs = SkillCastTimeMs(skill);
	if (baseMs == 0) {
		// A held spell recast on its cast frame (the time to it); a spell-like skill played its whole animation.
		const PlayerAnimData &data = GetPlayerAnimDataForClass(player._pClass);
		const int ticks = skill.kind == SkillKind::Spell ? data.castingActionFrame : data.castingFrames - 1;
		baseMs = std::max(ticks, 1) * 1000 / TicksPerSecond;
	}
	const uint64_t increased = static_cast<uint64_t>(std::max(100 + IncreasedCastSpeedPercent(player, skill, learnedSpell), 10));
	const uint64_t time = static_cast<uint64_t>(baseMs) * TicksPerSecond * ActionTimeOneTick * 100;
	const uint64_t divisor = 1000 * increased;
	return ClampActionTime((time + (divisor / 2)) / divisor);
}

uint32_t FlurryStrikeTime(uint32_t useTime, int attackFrames, bool backhand)
{
	// The strike plays every frame; the backhand starts FlurryBackhandSkippedFrames in.
	const uint64_t strikeFrames = static_cast<uint64_t>(std::max(attackFrames - 1, 1));
	const uint64_t backhandFrames = static_cast<uint64_t>(std::max(attackFrames - 1 - FlurryBackhandSkippedFrames, 1));
	const auto strike = static_cast<uint32_t>(useTime * strikeFrames / (strikeFrames + backhandFrames));
	return backhand ? useTime - strike : strike;
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
	if (!Loaded && gbIsPoe2)
		Load();
	SkillSpeeds[key] = SkillSpeed { std::clamp(attackPercent, 10, 1000), std::max(castMs, 0) };
}

void LoadSkillSpeedData()
{
	SkillSpeeds.clear();
	Loaded = false;
	if (gbIsPoe2)
		Load();
}

} // namespace devilution

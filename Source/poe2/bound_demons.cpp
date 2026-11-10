#include "poe2/bound_demons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <string_view>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "effects.h"
#include "engine/clx_sprite.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile.hpp"
#include "monster.h"
#include "msg.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/defenses.hpp"
#include "poe2/mix.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/vfx.hpp"
#include "sound_effect_enums.h"
#include "tables/itemdat.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** Salts for the demon's hashes, so they don't follow its other rolls from the same seed. */
constexpr uint32_t EssenceSalt = 0x424F554EU;
constexpr uint32_t EvilSalt = 0x4556494CU;
/** How many rolls the demon's second essence gets to come up different from its first. */
constexpr uint32_t DifferentEssenceTries = 8;
/** A better essence gives more of its power: this many percent more for each tier above Lesser. */
constexpr int PowerPercentPerTier = 25;
/** The hero's resistances can't go past this against its added element. */
constexpr int MaxPlayerResistance = 75;
/** The outline of a demon out of its crystal shows each of its essences' glows in turn, this long each. */
constexpr uint32_t GlowCycleMs = 700;
/** The crystal's heartbeat: one cycle this long, two quick beats at its start. */
constexpr uint32_t PulseCycleMs = 1200;

std::array<BoundDemonPower, static_cast<size_t>(EssenceKind::Count)> Powers {};

template <typename E>
std::expected<E, std::string> ParseName(std::string_view text)
{
	if (const std::optional<E> value = magic_enum::enum_cast<E>(text); value && *value != E::Count)
		return *value;
	return std::unexpected("unknown name");
}

/** The essences a monster holds are worked out once for its seed, act and area level (several times a tick otherwise). */
struct HeldCache {
	uint32_t seed = 0;
	int act = -1;
	int areaLevel = -1;
	std::vector<uint8_t> held;
};
std::array<HeldCache, MaxMonsters> Cache;

const std::vector<uint8_t> &Held(const Monster &monster)
{
	static const std::vector<uint8_t> None;
	if (!IsEssenceDemon(monster) || monster.getId() >= MaxMonsters)
		return None;
	HeldCache &cache = Cache[monster.getId()];
	const int act = CurrentAct();
	const int areaLevel = CurrentAreaLevel();
	if (cache.seed != monster.rndItemSeed || cache.act != act || cache.areaLevel != areaLevel) {
		cache.seed = monster.rndItemSeed;
		cache.act = act;
		cache.areaLevel = areaLevel;
		cache.held = EssencesForSeed(monster.rndItemSeed, act, areaLevel);
	}
	return cache.held;
}

/** The total of one power over the demon's essences, each scaled by its tier. */
int SumPower(const Monster &monster, BoundPower power)
{
	int total = 0;
	for (const uint8_t variant : Held(monster)) {
		const BoundDemonPower &row = GetBoundDemonPower(static_cast<uint8_t>(EssenceVariantKind(variant)));
		if (row.power != power)
			continue;
		total += row.value * (100 + (PowerPercentPerTier * static_cast<int>(EssenceVariantTier(variant)))) / 100;
	}
	return total;
}

std::optional<size_t> EssenceItemIndex()
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == IMISC_ESSENCE)
			return i;
	}
	return std::nullopt;
}

std::string_view TierPrefix(EssenceTier tier)
{
	switch (tier) {
	case EssenceTier::Lesser:
		return "Lesser ";
	case EssenceTier::Greater:
		return "Greater ";
	case EssenceTier::Perfect:
		return "Perfect ";
	default:
		return "";
	}
}

uint8_t GlowBase(BoundGlow glow)
{
	switch (glow) {
	case BoundGlow::Orange:
		return PAL16_ORANGE;
	case BoundGlow::Yellow:
		return PAL16_YELLOW;
	case BoundGlow::Blue:
		return PAL16_BLUE;
	case BoundGlow::Beige:
		return PAL16_BEIGE;
	case BoundGlow::Gray:
		return PAL16_GRAY;
	default:
		return PAL16_RED;
	}
}

int PlayerResistance(const Player &player, DamageType type)
{
	int resistance = 0;
	switch (type) {
	case DamageType::Fire:
		resistance = player._pFireResist;
		break;
	case DamageType::Lightning:
		resistance = player._pLghtResist;
		break;
	case DamageType::Cold:
		resistance = Poe2ColdResistance(player);
		break;
	default:
		break;
	}
	return std::min(resistance, MaxPlayerResistance);
}

/** How bright the crystal is now, 0 (dim) to 6 (a beat): two quick beats, then a rest. */
int PulseNow()
{
	const uint32_t phase = SDL_GetTicks() % PulseCycleMs;
	const auto beat = [](uint32_t at, uint32_t start) {
		if (at < start || at >= start + 180)
			return 0;
		const uint32_t into = at - start;
		return static_cast<int>(into < 60 ? into / 10 : (180 - into) / 20);
	};
	return std::clamp(std::max(beat(phase, 0), beat(phase, 260)), 0, 6);
}

/** Shade of a palette colour, 0 (light) to 15 (dark): the place in its ramp. */
int Shade(uint8_t color)
{
	if (color >= 128 && color < PAL16_BEIGE)
		return (color % 8) * 2;
	return color % 16;
}

void Line(const Surface &out, Point from, Point to, uint8_t color, bool halfTransparent, int dash = 0)
{
	int x = from.x;
	int y = from.y;
	const int dx = std::abs(to.x - from.x);
	const int dy = -std::abs(to.y - from.y);
	const int sx = from.x < to.x ? 1 : -1;
	const int sy = from.y < to.y ? 1 : -1;
	int error = dx + dy;
	// The longest line is the screen's width and height together, so this always ends.
	for (int step = 0; step <= dx - dy; step++) {
		const bool gap = dash > 0 && (step / dash) % 2 == 1;
		if (out.InBounds({ x, y })) {
			if (halfTransparent)
				SetHalfTransparentPixel(out, { x, y }, color);
			else
				out.SetPixel({ x, y }, gap ? static_cast<uint8_t>(color + 5) : color);
		}
		if (x == to.x && y == to.y)
			break;
		const int twice = 2 * error;
		if (twice >= dy) {
			error += dy;
			x += sx;
		}
		if (twice <= dx) {
			error += dx;
			y += sy;
		}
	}
}

Point OnEllipse(Point centre, int rx, int ry, int degrees)
{
	const double angle = degrees * 3.14159265358979 / 180.0;
	return { centre.x + static_cast<int>(rx * std::cos(angle)), centre.y + static_cast<int>(ry * std::sin(angle)) };
}

void DrawEllipse(const Surface &out, Point centre, int rx, int ry, uint8_t color, bool halfTransparent)
{
	Point last = OnEllipse(centre, rx, ry, 0);
	for (int degrees = 10; degrees <= 360; degrees += 10) {
		const Point next = OnEllipse(centre, rx, ry, degrees);
		Line(out, last, next, color, halfTransparent);
		last = next;
	}
}

/** The Horadric sigil under the crystal: two rings and a six-pointed star, laid flat on the floor. */
void DrawSigil(const Surface &out, Point centre, int pulse)
{
	constexpr int OuterX = 46;
	constexpr int OuterY = 23;
	constexpr int InnerX = 36;
	constexpr int InnerY = 18;
	const uint8_t ring = static_cast<uint8_t>(PAL16_RED + 9 - (pulse / 2));
	const uint8_t glyph = static_cast<uint8_t>(PAL16_RED + 6 - (pulse / 2));
	DrawEllipse(out, centre, OuterX, OuterY, ring, false);
	DrawEllipse(out, centre, InnerX, InnerY, glyph, true);
	for (const int first : { 90, 30 }) {
		const Point a = OnEllipse(centre, InnerX, InnerY, first);
		const Point b = OnEllipse(centre, InnerX, InnerY, first + 120);
		const Point c = OnEllipse(centre, InnerX, InnerY, first + 240);
		Line(out, a, b, glyph, true);
		Line(out, b, c, glyph, true);
		Line(out, c, a, glyph, true);
	}
	// Ticks round the outer ring, like the Horadrim's script.
	for (int degrees = 0; degrees < 360; degrees += 30)
		Line(out, OnEllipse(centre, OuterX - 4, OuterY - 2, degrees), OnEllipse(centre, OuterX, OuterY, degrees), ring, false);
}

} // namespace

void LoadBoundDemonPowers()
{
	Powers = {};
	for (HeldCache &cache : Cache)
		cache = {};
	if (!gbIsPoe2)
		return;
	constexpr std::string_view Filename = "txtdata\\poe2\\bound_demons.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the bound demons' powers: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		EssenceKind kind = EssenceKind::None;
		BoundDemonPower power;
		reader.read("essence", kind, ParseName<EssenceKind>);
		reader.read("power", power.power, ParseName<BoundPower>);
		reader.readInt("value", power.value);
		reader.read("glow", power.glow, ParseName<BoundGlow>);
		Powers[static_cast<size_t>(kind)] = power;
	}
}

const BoundDemonPower &GetBoundDemonPower(uint8_t essenceKind)
{
	static const BoundDemonPower None;
	return essenceKind < Powers.size() ? Powers[essenceKind] : None;
}

bool IsBoundDemon(const Monster &monster)
{
	return (monster.flags & MFLAG_POE2_BOUND) != 0;
}

bool IsEssenceDemon(const Monster &monster)
{
	return (monster.flags & MFLAG_POE2_ESSENCE_DEMON) != 0;
}

std::vector<uint8_t> EssencesForSeed(uint32_t seed, int act, int areaLevel)
{
	std::vector<uint8_t> held;
	const uint32_t hash = Mix(seed ^ EssenceSalt);
	// Act 1: one Lesser essence (area level 1 leaves only the Lesser tier, act 1 leaves the Evil ones out).
	if (act <= 1) {
		if (const uint8_t variant = RollEssenceDrop(Mix(hash + 1), 1, 1); variant != 0)
			held.push_back(variant);
		return held;
	}
	const size_t count = 1 + ((hash >> 4) & 1);
	for (uint32_t i = 0; held.size() < count && i < DifferentEssenceTries; i++) {
		// Act 2's essences, at most: the Evil ones come below, one demon in six.
		const uint8_t variant = RollEssenceDrop(Mix(hash + 1 + (i * 0x9E3779B9U)), areaLevel, std::min(act, EvilBoundDemonAct - 1));
		if (variant == 0)
			break;
		const bool repeats = std::any_of(held.begin(), held.end(), [variant](uint8_t other) { return EssenceVariantKind(other) == EssenceVariantKind(variant); });
		if (!repeats)
			held.push_back(variant);
	}
	const uint32_t evilHash = Mix(seed ^ EvilSalt);
	if (act >= EvilBoundDemonAct && !held.empty() && evilHash % EvilBoundDemonOneIn == 0) {
		constexpr uint32_t EvilKinds = static_cast<uint32_t>(EssenceKind::Count) - static_cast<uint32_t>(FirstEvilEssence);
		const auto kind = static_cast<EssenceKind>(static_cast<uint32_t>(FirstEvilEssence) + ((evilHash >> 8) % EvilKinds));
		if (GetEssence(kind).kind == kind)
			held.back() = EssenceVariant(kind, EssenceTier::Normal);
	}
	return held;
}

std::vector<uint8_t> BoundDemonEssences(const Monster &monster)
{
	return Held(monster);
}

std::string BoundDemonEssenceText(const Monster &monster)
{
	std::string text;
	for (const uint8_t variant : Held(monster)) {
		const EssenceKind kind = EssenceVariantKind(variant);
		const std::string_view prefix = IsEvilEssence(kind) ? "" : TierPrefix(EssenceVariantTier(variant));
		StrAppend(text, text.empty() ? "" : ", ", prefix, "Essence of ", GetEssence(kind).name);
	}
	return text;
}

bool BindDemon(Monster &monster)
{
	if (!gbIsPoe2)
		return false;
	// A magic one can't be made rare as it is.
	if (GetMonsterAffixes(monster).rarity == MonsterRarity::Magic)
		DemoteMonsterRarity(monster);
	PromoteMonsterRarity(monster, MonsterRarity::Rare);
	if (GetMonsterAffixes(monster).rarity != MonsterRarity::Rare)
		return false;
	monster.flags |= MFLAG_POE2_BOUND | MFLAG_POE2_ESSENCE_DEMON;
	if (Held(monster).empty()) {
		monster.flags &= ~(MFLAG_POE2_BOUND | MFLAG_POE2_ESSENCE_DEMON);
		return false;
	}
	const int life = SumPower(monster, BoundPower::Life) + SumPower(monster, BoundPower::Evil);
	if (life > 0) {
		const int64_t raw = static_cast<int64_t>(monster.maxHitPoints.raw()) * (100 + life) / 100;
		monster.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(raw, INT32_MAX / 2)));
		monster.hitPoints = monster.maxHitPoints;
	}
	return true;
}

void BreakBoundCrystal(Monster &monster)
{
	if (!IsBoundDemon(monster))
		return;
	monster.flags &= ~MFLAG_POE2_BOUND;
	monster.poe2.boundWakeTicks = BoundDemonWakeTicks;
	const Point tile = monster.position.tile;
	PlaySfxLoc(SfxID::ItemPotionFlip, tile, false);
	PlaySfxLoc(SfxID::SpellStoneCurse, tile, false);
	vfx::Impact(tile, vfx::Element::Blood, 1.4F);
}

void ReleaseBoundDemonOnLoad(Monster &monster)
{
	monster.flags &= ~MFLAG_POE2_BOUND;
	monster.poe2.boundWakeTicks = 0;
}

void RequestBreakBoundCrystal(const Player &player, const Monster &monster)
{
	if (&player != MyPlayer || !IsBoundDemon(monster))
		return;
	NetSendCmdBreakCrystal(monster);
}

bool ProcessBoundDemon(Monster &monster)
{
	if (IsBoundDemon(monster)) {
		// The heartbeat is this machine's alone, and takes nothing from the game's random numbers.
		if (++monster.poe2.boundPulse >= HeartbeatTicks) {
			monster.poe2.boundPulse = 0;
			if (MyPlayer != nullptr && MyPlayer->isOnActiveLevel()) {
				const Displacement apart = MyPlayer->position.tile - monster.position.tile;
				if (std::max(std::abs(apart.deltaX), std::abs(apart.deltaY)) <= HeartbeatTiles)
					PlaySfxLoc(SfxID::ItemBodyPartFlip, monster.position.tile, false);
			}
		}
		return true;
	}
	if (monster.poe2.boundWakeTicks == 0)
		return false;
	// Killed in its second out of the crystal: it dies at once, with no roar.
	if (monster.hasNoLife() || monster.mode == MonsterMode::Death) {
		monster.poe2.boundWakeTicks = 0;
		return false;
	}
	monster.poe2.boundWakeTicks--;
	if (monster.poe2.boundWakeTicks > 0) {
		monster.animInfo.processAnimation();
		return true;
	}
	// Free: it roars and comes for the hero.
	PlayEffect(monster, MonsterSound::Attack);
	monster.activeForTicks = UINT8_MAX;
	return false;
}

bool BoundDemonExtraTick(Monster &monster)
{
	if (!IsEssenceDemon(monster) || IsBoundDemon(monster) || monster.poe2.boundWakeTicks > 0)
		return false;
	const int speed = SumPower(monster, BoundPower::Speed);
	if (speed <= 0)
		return false;
	const int carry = monster.poe2.boundHasteCarry + std::min(speed, 100);
	if (carry >= 100) {
		monster.poe2.boundHasteCarry = static_cast<uint8_t>(carry - 100);
		return true;
	}
	monster.poe2.boundHasteCarry = static_cast<uint8_t>(carry);
	return false;
}

int BoundDemonDamagePercent(const Monster &monster)
{
	if (!IsEssenceDemon(monster))
		return 0;
	return SumPower(monster, BoundPower::Damage) + SumPower(monster, BoundPower::Evil);
}

int BoundDemonResistance(const Monster &monster, DamageType type)
{
	if (!IsEssenceDemon(monster))
		return 0;
	switch (type) {
	case DamageType::Fire:
		return SumPower(monster, BoundPower::FireResist) + SumPower(monster, BoundPower::ElementalResist);
	case DamageType::Cold:
		return SumPower(monster, BoundPower::ColdResist) + SumPower(monster, BoundPower::ElementalResist);
	case DamageType::Lightning:
		return SumPower(monster, BoundPower::LightningResist) + SumPower(monster, BoundPower::ElementalResist);
	case DamageType::Chaos:
		return SumPower(monster, BoundPower::ChaosResist);
	default:
		return 0;
	}
}

int BoundDemonArmourPercent(const Monster &monster)
{
	return IsEssenceDemon(monster) ? SumPower(monster, BoundPower::Armour) : 0;
}

Fixed26_6 BoundDemonExtraHitDamage(const Monster &monster, const Player &player, Fixed26_6 hit)
{
	if (!IsEssenceDemon(monster) || hit <= 0)
		return Fixed26_6::fromInt(0);
	int64_t total = 0;
	const auto add = [&](BoundPower power, DamageType type) {
		const int percent = SumPower(monster, power);
		if (percent > 0)
			total += static_cast<int64_t>(hit.raw()) * percent / 100 * (100 - PlayerResistance(player, type)) / 100;
	};
	add(BoundPower::FireHit, DamageType::Fire);
	add(BoundPower::ColdHit, DamageType::Cold);
	add(BoundPower::LightningHit, DamageType::Lightning);
	return Fixed26_6::fromRaw(static_cast<int32_t>(std::clamp<int64_t>(total, 0, INT32_MAX / 2)));
}

void DropBoundDemonEssences(const Monster &monster, bool sendmsg)
{
	if (!IsEssenceDemon(monster) || MyPlayer == nullptr)
		return;
	const std::optional<size_t> index = EssenceItemIndex();
	if (!index)
		return;
	const std::vector<uint8_t> held = Held(monster);
	for (const uint8_t variant : held) {
		if (ActiveItemCount >= MAXITEMS)
			return;
		const int ii = AllocateItem();
		Item &item = Items[ii];
		GetSuperItemSpace(monster.position.tile, static_cast<int8_t>(ii));
		SetupAllItems(*MyPlayer, item, static_cast<_item_indexes>(*index), AdvanceRndSeed(), static_cast<int>(monster.level(sgGameInitInfo.nDifficulty)), 1, false, false);
		SetCraftingItemVariant(item, variant);
		FinishCraftingItem(item);
		SetupItem(item);
		if (sendmsg)
			NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	}
}

void DrawBoundDemon(const Surface &out, const Monster &monster, Point tileBufferPosition, Point spritePosition, ClxSprite sprite)
{
	const int pulse = PulseNow();
	const Point centre = tileBufferPosition + Displacement { TILE_WIDTH / 2, -TILE_HEIGHT / 2 };
	DrawSigil(out, centre, pulse);

	// The demon inside, all in dark blood red, brighter on each beat.
	std::array<uint8_t, 256> red;
	red[0] = 0;
	for (size_t i = 1; i < red.size(); i++)
		red[i] = static_cast<uint8_t>(PAL16_RED + std::clamp(7 + (Shade(static_cast<uint8_t>(i)) / 2) - (pulse / 2), 3, 15));
	ClxDrawTRN(out, spritePosition, sprite, red.data());

	// The crystal: a six-sided prism standing over it, its glass tinting what is behind it.
	const int width = static_cast<int>(sprite.width());
	const int height = static_cast<int>(sprite.height());
	const int cx = spritePosition.x + (width / 2);
	const int base = centre.y + 10;
	const int tall = std::clamp(height - 16, 64, 120);
	const int top = base - tall;
	const int half = std::clamp(width / 4, 22, 38);
	const auto extent = [&](int y) {
		if (y < top + half)
			return y - top;
		if (y < base - (half / 2))
			return half;
		return (base - y) * 2;
	};
	for (int y = top; y <= base; y++) {
		const int e = extent(y);
		for (int x = cx - e; x <= cx + e; x++) {
			if (!out.InBounds({ x, y }))
				continue;
			// Three facets: dark on the left, the face in the middle beating, light on the right.
			int shade = 6 - (pulse / 2);
			if (x < cx - (e / 3))
				shade = 10;
			else if (x > cx + (e / 3))
				shade = 4;
			SetHalfTransparentPixel(out, { x, y }, static_cast<uint8_t>(PAL16_RED + shade));
		}
	}
	const uint8_t edge = static_cast<uint8_t>(PAL16_RED + 3 - (pulse / 3));
	const Point apex { cx, top };
	const Point leftUpper { cx - half, top + half };
	const Point rightUpper { cx + half, top + half };
	const Point leftLower { cx - half, base - (half / 2) };
	const Point rightLower { cx + half, base - (half / 2) };
	const Point bottom { cx, base };
	Line(out, apex, leftUpper, edge, false);
	Line(out, apex, rightUpper, edge, false);
	Line(out, leftUpper, leftLower, edge, false);
	Line(out, rightUpper, rightLower, edge, false);
	Line(out, leftLower, bottom, edge, false);
	Line(out, rightLower, bottom, edge, false);
	// The facets' ridges, and a glint on the light side.
	const int ridge = half / 3;
	Line(out, { cx - ridge, top + ridge }, { cx - ridge, base - (ridge * 2) }, static_cast<uint8_t>(PAL16_RED + 5), true);
	Line(out, { cx + ridge, top + ridge }, { cx + ridge, base - (ridge * 2) }, static_cast<uint8_t>(PAL16_RED + 2), true);
	Line(out, { cx + ridge + 3, top + half + 4 }, { cx + half - 4, top + half + 14 }, static_cast<uint8_t>(PAL16_YELLOW + 1), false);

	// Chains from the crystal down to the sigil's ring.
	const uint8_t iron = static_cast<uint8_t>(PAL16_GRAY + 6);
	constexpr int Dash = 3;
	Line(out, { leftUpper.x, leftUpper.y + 8 }, { centre.x - 46, centre.y }, iron, false, Dash);
	Line(out, { rightUpper.x, rightUpper.y + 8 }, { centre.x + 46, centre.y }, iron, false, Dash);
	Line(out, { leftLower.x + 4, leftLower.y }, { centre.x - 26, centre.y + 19 }, iron, false, Dash);
	Line(out, { rightLower.x - 4, rightLower.y }, { centre.x + 26, centre.y + 19 }, iron, false, Dash);
}

std::optional<uint8_t> BoundDemonGlowColor(const Monster &monster)
{
	if (!IsEssenceDemon(monster) || IsBoundDemon(monster))
		return std::nullopt;
	const std::vector<uint8_t> &held = Held(monster);
	if (held.empty())
		return std::nullopt;
	const uint32_t now = SDL_GetTicks();
	const uint8_t variant = held[(now / GlowCycleMs) % held.size()];
	const BoundDemonPower &power = GetBoundDemonPower(static_cast<uint8_t>(EssenceVariantKind(variant)));
	// It brightens and fades over each glow.
	const int fade = static_cast<int>((now % GlowCycleMs) * 8 / GlowCycleMs);
	return static_cast<uint8_t>(GlowBase(power.glow) + 1 + std::abs(fade - 4));
}

} // namespace devilution

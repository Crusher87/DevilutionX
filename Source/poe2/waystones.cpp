#include "poe2/waystones.hpp"

#include <algorithm>
#include <array>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "interfac.h"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "lighting.h"
#include "monster.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/craft_state.hpp"
#include "poe2/currency.hpp"
#include "poe2/endgame.hpp"
#include "poe2/floors.hpp"
#include "poe2/loot.hpp"
#include "poe2/map_play.hpp"
#include "poe2/mix.hpp"
#include "poe2/monster_mods.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

MapEffects CurrentEffects;
/** Where the local player came into the map, for revives (not saved; the level's entrance after a load). */
std::optional<Point> MapEntrance;

int SideCount(const Poe2Craft &craft, bool prefix)
{
	return static_cast<int>(std::count_if(craft.mods.begin(), craft.mods.end(), [prefix](const Poe2CraftMod &mod) {
		return mod.family != 0 && GetWaystoneMod(mod.family).prefix == prefix;
	}));
}

/** @brief Adds a modifier for the waystone's tier band; false if none fits. */
bool AddWaystoneMod(Poe2Craft &craft, int tier, uint32_t seed, int forcedSide = 0)
{
	const int count = CraftModCount(craft);
	const int perSide = craft.rarity == ITEM_QUALITY_MAGIC ? 1 : WaystoneModsPerSide;
	if (count >= perSide * 2)
		return false;
	const uint8_t id = RollWaystoneMod(craft.mods, perSide, seed, forcedSide);
	if (id == 0)
		return false;
	craft.mods[static_cast<size_t>(count)] = { id, static_cast<uint8_t>(WaystoneBand(tier) + 1), static_cast<uint8_t>(1 + (Mix(seed ^ 0x5A17U) % MaxCraftSalt)) };
	std::stable_partition(craft.mods.begin(), craft.mods.begin() + count + 1, [](const Poe2CraftMod &mod) { return mod.family != 0 && GetWaystoneMod(mod.family).prefix; });
	return true;
}

void RemoveWaystoneMod(Poe2Craft &craft, uint32_t seed)
{
	const int count = CraftModCount(craft);
	if (count > 0)
		RemoveCraftMod(craft, Mix(seed) % static_cast<uint32_t>(count));
}

uint32_t WaystoneCraftSeed(const Item &item, CurrencyOrb orb)
{
	uint32_t hash = Mix(static_cast<uint32_t>(item._iSeed) ^ 0x3A75U);
	for (const uint8_t byte : PackCraftState(item._iPoe2Craft))
		hash = Mix(hash ^ byte);
	return Mix(hash ^ (static_cast<uint32_t>(orb) * 0x9E3779B9U));
}

void Rebuild(const Player &player, Item &item, const Poe2Craft &craft, uint32_t dwBuff)
{
	item = RebuildWithCraft(player, item, dwBuff, craft);
	EventPlrMsg(FormatRuntime(_("Crafted: {:s}"), item.getName().str()));
}

/** @brief Natural rare waystones have 3 to 6 modifiers, weighted 2:5:3:1. */
int RareWaystoneModCount(uint32_t seed)
{
	const int roll = static_cast<int>(seed % 11);
	return roll < 2 ? 3 : roll < 7 ? 4
	    : roll < 10                ? 5
	                               : 6;
}

} // namespace

bool IsWaystone(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_WAYSTONE;
}

int WaystoneTierForLevel(int itemLevel)
{
	return std::clamp((itemLevel / 5) - 2, 1, MaxWaystoneTier);
}

int WaystoneTierForItemLevel(int poe2ItemLevel)
{
	return std::clamp(poe2ItemLevel - MapAreaLevelBase - 1, 1, MaxWaystoneTier);
}

int WaystoneTier(const Item &item)
{
	if (item._iPoe2Craft.explicitMods)
		return item._iPoe2Craft.catalyst + 1;
	return WaystoneTierForLevel(item._iCreateInfo & CF_LEVEL);
}

int WaystoneModCount(const Item &item)
{
	return CraftModCount(item._iPoe2Craft);
}

int WaystoneRevives(const Item &item)
{
	return std::max(0, MaxMapRevives - WaystoneModCount(item));
}

int WaystoneTabletSlots(const Item &item)
{
	const int mods = WaystoneModCount(item);
	return mods >= 6 ? 3 : mods / 2;
}

void RollWaystone(Item &item, int tier)
{
	Poe2Craft craft;
	craft.explicitMods = true;
	craft.catalyst = static_cast<uint8_t>(std::clamp(tier, 1, MaxWaystoneTier) - 1);
	const uint32_t seed = Mix(static_cast<uint32_t>(item._iSeed) ^ 0x3A7E5701U);
	// Normal 55%, magic 30%, rare 15%.
	const uint32_t rarityRoll = seed % 100;
	craft.rarity = rarityRoll < 55 ? ITEM_QUALITY_NORMAL : rarityRoll < 85 ? ITEM_QUALITY_MAGIC
	                                                                       : ITEM_QUALITY_RARE;
	const int count = craft.rarity == ITEM_QUALITY_RARE ? RareWaystoneModCount(Mix(seed ^ 1)) : craft.rarity == ITEM_QUALITY_MAGIC ? 1 + static_cast<int>(Mix(seed ^ 2) % 2)
	                                                                                                                               : 0;
	for (int i = 0; i < count; i++)
		AddWaystoneMod(craft, tier, Mix(seed ^ (0x100U + static_cast<uint32_t>(i))));
	item._iPoe2Craft = craft;
	FinishWaystone(item);
}

void FinishWaystone(Item &item)
{
	if (!IsWaystone(item))
		return;
	if (!item._iPoe2Craft.explicitMods) {
		RollWaystone(item, WaystoneTierForLevel(item._iCreateInfo & CF_LEVEL));
		return;
	}
	const Poe2Craft &craft = item._iPoe2Craft;
	item._iMagical = static_cast<item_quality>(craft.rarity);
	std::string name = FormatRuntime(_("Waystone (Tier {:d})"), WaystoneTier(item));
	if (craft.rarity == ITEM_QUALITY_MAGIC) {
		std::string_view prefix;
		std::string_view suffix;
		for (const Poe2CraftMod &mod : craft.mods) {
			if (mod.family == 0)
				continue;
			const WaystoneMod &data = GetWaystoneMod(mod.family);
			(data.prefix ? prefix : suffix) = data.name;
		}
		name = StrCat(prefix, prefix.empty() ? "" : " ", name, suffix.empty() ? "" : " ", suffix);
	} else if (craft.rarity == ITEM_QUALITY_RARE) {
		name = StrCat(GetRareItemName(item), " ", name);
	}
	CopyUtf8(item._iIName, name, ItemNameLength);
	item._iIdentified = true;
}

std::vector<std::string> WaystoneText(const Item &item)
{
	std::vector<std::string> lines;
	const int tier = WaystoneTier(item);
	lines.push_back(FormatRuntime(_("Map tier {:d} (area level {:d})"), tier, MapAreaLevelBase + tier));
	lines.push_back(FormatRuntime(_("Monsters: {:d}% more life, {:d}% more damage"), tier * MapLifePercentPerTier, tier * MapDamagePercentPerTier));
	for (const Poe2CraftMod &mod : item._iPoe2Craft.mods) {
		for (const std::string &line : WaystoneModText(mod))
			lines.push_back(line);
	}
	lines.push_back(FormatRuntime(_("Revives: {:d}   Tablet slots: {:d}"), WaystoneRevives(item), WaystoneTabletSlots(item)));
	if ((item.dwBuff & CF_POE2_CORRUPTED) != 0)
		lines.emplace_back(_("Corrupted"));
	lines.emplace_back(_("Use in town to open the map"));
	return lines;
}

bool CraftWaystone(CurrencyOrb orb, const Player &player, Item &item)
{
	if ((item.dwBuff & CF_POE2_CORRUPTED) != 0) {
		EventPlrMsg(_("Corrupted waystones can't be crafted."), UiFlags::ColorRed);
		return false;
	}
	Poe2Craft craft = item._iPoe2Craft;
	const int tier = WaystoneTier(item);
	const int count = CraftModCount(craft);
	const uint32_t seed = WaystoneCraftSeed(item, orb);
	const auto fail = [](std::string_view message) {
		EventPlrMsg(message, UiFlags::ColorRed);
		return false;
	};
	uint32_t dwBuff = item.dwBuff;
	switch (orb) {
	case CurrencyOrb::Transmutation:
		if (craft.rarity != ITEM_QUALITY_NORMAL)
			return fail(_("Orb of Transmutation needs a normal waystone."));
		craft.rarity = ITEM_QUALITY_MAGIC;
		AddWaystoneMod(craft, tier, seed);
		break;
	case CurrencyOrb::Augmentation:
		if (craft.rarity != ITEM_QUALITY_MAGIC || !AddWaystoneMod(craft, tier, seed))
			return fail(_("Orb of Augmentation needs a magic waystone with room for a modifier."));
		break;
	case CurrencyOrb::Regal:
		if (craft.rarity != ITEM_QUALITY_MAGIC)
			return fail(_("Regal Orb needs a magic waystone."));
		craft.rarity = ITEM_QUALITY_RARE;
		AddWaystoneMod(craft, tier, seed);
		break;
	case CurrencyOrb::Alchemy:
		if (craft.rarity == ITEM_QUALITY_RARE)
			return fail(_("Orb of Alchemy needs a normal or magic waystone."));
		craft.rarity = ITEM_QUALITY_RARE;
		craft.mods = {};
		for (uint32_t i = 0; i < 4; i++)
			AddWaystoneMod(craft, tier, Mix(seed ^ i));
		break;
	case CurrencyOrb::Exalted:
	case CurrencyOrb::GreaterExalted:
	case CurrencyOrb::PerfectExalted:
		if (craft.rarity != ITEM_QUALITY_RARE || !AddWaystoneMod(craft, tier, seed))
			return fail(_("Exalted Orbs need a rare waystone with room for a modifier."));
		break;
	case CurrencyOrb::Chaos:
	case CurrencyOrb::GreaterChaos:
	case CurrencyOrb::PerfectChaos:
		if (craft.rarity != ITEM_QUALITY_RARE || count == 0)
			return fail(_("Chaos Orbs need a rare waystone."));
		RemoveWaystoneMod(craft, seed);
		AddWaystoneMod(craft, tier, Mix(seed ^ 1));
		break;
	case CurrencyOrb::Annul:
		if (craft.rarity == ITEM_QUALITY_NORMAL || count == 0)
			return fail(_("Orb of Annulment needs a waystone with a modifier."));
		RemoveWaystoneMod(craft, seed);
		break;
	case CurrencyOrb::Scouring:
		if (craft.rarity == ITEM_QUALITY_NORMAL)
			return fail(_("This waystone has no modifiers to remove."));
		craft.rarity = ITEM_QUALITY_NORMAL;
		craft.mods = {};
		break;
	case CurrencyOrb::Divine:
		if (count == 0)
			return fail(_("Divine Orb needs a waystone with a modifier."));
		for (size_t i = 0; i < craft.mods.size(); i++) {
			if (craft.mods[i].family != 0)
				craft.mods[i].salt = static_cast<uint8_t>(1 + (Mix(seed ^ static_cast<uint32_t>(i)) % MaxCraftSalt));
		}
		break;
	case CurrencyOrb::Vaal: {
		// Corrupted: nothing, one tier higher, or one more modifier (past three a side, if it has room).
		dwBuff |= CF_POE2_CORRUPTED;
		switch (seed % 3) {
		case 1:
			craft.catalyst = static_cast<uint8_t>(std::min(tier, MaxWaystoneTier - 1));
			break;
		case 2:
			if (craft.rarity == ITEM_QUALITY_NORMAL)
				craft.rarity = ITEM_QUALITY_MAGIC;
			if (count < static_cast<int>(craft.mods.size())) {
				if (const uint8_t id = RollWaystoneMod(craft.mods, WaystoneModsPerSide + 1, seed); id != 0)
					craft.mods[static_cast<size_t>(count)] = { id, static_cast<uint8_t>(WaystoneBand(tier) + 1), 1 };
			}
			break;
		default:
			break;
		}
		break;
	}
	default:
		return fail(_("That currency doesn't work on waystones."));
	}
	Rebuild(player, item, craft, dwBuff);
	return true;
}

int MapLevelForTier(int tier, uint32_t seed)
{
	// Levels 15, 16, 20 and 24 hold bosses and quests, so maps use the levels around them.
	if (tier >= 5 && gbIsHellfire)
		return 21 + static_cast<int>(seed % 3);
	return 13 + static_cast<int>(seed % 2);
}

int ActiveMapTier()
{
	if (!gbIsPoe2 || MyPlayer == nullptr || setlevel)
		return 0;
	const Poe2PlayerData &data = MyPlayer->poe2;
	if (data.mapTier == 0 || currlevel != data.mapLevel)
		return 0;
	return data.mapTier;
}

const MapEffects &ActiveMapEffects()
{
	static const MapEffects None;
	return ActiveMapTier() > 0 ? CurrentEffects : None;
}

void RefreshMapEffects(const Player &player)
{
	CurrentEffects = {};
	if (player.poe2.mapTier == 0)
		return;
	const Poe2Craft &waystone = player.poe2.mapWaystone;
	// Rewards on the waystone add up, then multiply with the other sources.
	std::array<int, static_cast<size_t>(MapAxis::Count)> rewards {};
	// Deadlier Bargain makes both sides of every modifier stronger.
	const int strength = WaystoneModStrengthPercent(player);
	for (const Poe2CraftMod &mod : waystone.mods) {
		if (mod.family == 0 || mod.family > WaystoneModIds)
			continue;
		const WaystoneMod &data = GetWaystoneMod(mod.family);
		CurrentEffects.danger[static_cast<size_t>(data.danger)] += WaystoneModValue(mod) * strength / 100;
		rewards[static_cast<size_t>(data.axis)] += WaystoneModReward(mod) * strength / 100;
	}
	for (size_t axis = 0; axis < rewards.size(); axis++)
		CurrentEffects.multiply(static_cast<MapAxis>(axis), rewards[axis]);
	// Tablets, the Atlas and a pinnacle boss's theme.
	AddEndgameMapEffects(player, CurrentEffects);
}

int PrepareMap(Player &player, int tier, uint32_t seed)
{
	tier = std::clamp(tier, 1, MaxWaystoneTier);
	const int level = MapLevelForTier(tier, seed);
	Poe2ApplyFreshFloor(level, false, seed, static_cast<uint16_t>(Poe2FloorVisits(level, false) + 1));
	player._pLvlVisited[level] = false;
	player.poe2.mapLevel = static_cast<uint8_t>(level);
	player.poe2.mapTier = static_cast<uint8_t>(tier);
	player.poe2.mapBoss = -1;
	player.poe2.mapBossKilled = false;
	player.poe2.mapRun = {};
	MapEntrance = std::nullopt;
	return level;
}

void EnterMap(Player &player, int level)
{
	// Level 21 is the Hellfire crypt, which is entered from town.
	StartNewLvl(player, level != 21 ? WM_DIABNEXTLVL : WM_DIABTOWNWARP, level);
}

bool UseWaystone(Player &player, Item &waystone)
{
	if (!IsWaystone(waystone) || &player != MyPlayer)
		return false;
	if (gbIsMultiplayer) {
		EventPlrMsg(_("Maps aren't available in multiplayer yet."), UiFlags::ColorRed);
		return false;
	}
	if (!player.isOnLevel(0)) {
		EventPlrMsg(_("Use waystones in town to open a map."), UiFlags::ColorRed);
		return false;
	}
	const int tier = WaystoneTier(waystone);
	const int level = PrepareMap(player, tier, waystone._iSeed);
	player.poe2.mapWaystone = waystone._iPoe2Craft;
	player.poe2.mapRevives = static_cast<uint8_t>(MapRevivesFor(player, WaystoneRevives(waystone)));
	PrepareMapEndgame(player, waystone);
	RefreshMapEffects(player);
	EventPlrMsg(FormatRuntime(_("Map tier {:d}: {:d} revives. Defeat the map boss to complete it."), tier, player.poe2.mapRevives), UiFlags::ColorWhitegold);
	EnterMap(player, level);
	return true;
}

void UpdateActiveMap(Player &player)
{
	if (player.poe2.mapTier == 0 || leveltype == DTYPE_TOWN)
		return;
	if (setlevel || currlevel != player.poe2.mapLevel) {
		player.poe2.mapTier = 0;
		player.poe2.mapLevel = 0;
		player.poe2.mapWaystone = {};
		player.poe2.mapRun = {};
		RefreshMapEffects(player);
		ClearMapPlay();
		return;
	}
	RefreshMapEffects(player);
	if (&player != MyPlayer)
		return;
	if (!MapEntrance)
		MapEntrance = player.position.tile;
	PlaceMapGroundPatches(DungeonSeeds[player.poe2.mapLevel] ^ 0x67A0D5U, *MapEntrance);
}

void ChooseMapBoss()
{
	if (ActiveMapTier() == 0 || MyPlayer == nullptr || MyPlayer->poe2.mapBoss >= 0)
		return;
	// The toughest monster on the level, preferring a rare one, takes the role.
	std::optional<size_t> best;
	int64_t bestScore = -1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion() || monster.isUnique() || monster.hasNoLife())
			continue;
		int64_t score = monster.maxHitPoints.raw();
		if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
			score *= 4;
		if (score > bestScore) {
			bestScore = score;
			best = ActiveMonsters[i];
		}
	}
	if (!best)
		return;
	Monster &boss = Monsters[*best];
	MyPlayer->poe2.mapBoss = static_cast<int16_t>(*best);
	// A powerful boss or a pinnacle boss is tougher still.
	const int64_t bonus = MapBossLifePercentBonus();
	const int64_t lifePercent = (100 + MapBossLifePercent + (ActiveMapTier() * MapBossLifePercentPerTier)) * bonus / 100;
	boss.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(boss.maxHitPoints.raw()) * lifePercent / 100, INT32_MAX / 2)));
	boss.hitPoints = boss.maxHitPoints;
	const int damagePercent = (100 + MapBossDamagePercent) * static_cast<int>(std::min<int64_t>(bonus, 200)) / 100;
	const auto scale = [damagePercent](uint8_t &damage) { damage = static_cast<uint8_t>(std::min(damage * damagePercent / 100, 255)); };
	scale(boss.minDamage);
	scale(boss.maxDamage);
	scale(boss.minDamageSpecial);
	scale(boss.maxDamageSpecial);
}

bool IsMapBoss(const Monster &monster)
{
	if (ActiveMapTier() == 0 || MyPlayer == nullptr)
		return false;
	const Poe2PlayerData &data = MyPlayer->poe2;
	return !data.mapBossKilled && data.mapBoss >= 0 && static_cast<size_t>(data.mapBoss) == monster.getId();
}

void OnMapBossKilled(const Monster &monster, bool sendmsg)
{
	if (!IsMapBoss(monster))
		return;
	Player &player = *MyPlayer;
	player.poe2.mapBossKilled = true;
	player.poe2.mapsCompleted = static_cast<uint16_t>(std::min(player.poe2.mapsCompleted + 1, 65535));
	const int tier = ActiveMapTier();
	EventPlrMsg(FormatRuntime(_("Map complete (tier {:d})."), tier), UiFlags::ColorWhitegold);
	// Waystone sustain: the boss usually drops one of this tier, and now and then one tier higher.
	const int find = ActiveMapEffects()[MapAxis::WaystoneFind] * AtlasBossWaystonePercent(player) / 100;
	if (LootRandom(100) < MapBossWaystonePercent * find / 100)
		DropWaystone(monster.position.tile, tier, sendmsg);
	if (LootRandom(100) < MapBossUpgradePercent * find / 100)
		DropWaystone(monster.position.tile, std::min(tier + 1, MaxWaystoneTier), sendmsg);
	OnEndgameBossKilled(monster, sendmsg);
}

bool TryMapRevive(Player &player)
{
	if (&player != MyPlayer || ActiveMapTier() == 0 || player.poe2.mapRevives == 0)
		return false;
	player.poe2.mapRevives--;
	SetPlayerHitPoints(player, player._pMaxHP);
	player._pMana = player._pMaxMana;
	player._pManaBase = player._pMaxManaBase;
	if (MapEntrance) {
		const std::optional<Point> destination = FindClosestValidPosition([&player](Point target) { return PosOkPlayer(player, target); }, *MapEntrance, 0, 5);
		if (destination) {
			dPlayer[player.position.tile.x][player.position.tile.y] = 0;
			player.position.tile = *destination;
			player.position.future = *destination;
			player.position.old = *destination;
			player.occupyTile(*destination, false);
			ChangeLightXY(player.lightId, *destination);
			ChangeVisionXY(player.getId(), *destination);
			ViewPosition = *destination;
		}
	}
	EventPlrMsg(FormatRuntime(_("Revived at the map's start. Revives left: {:d}"), player.poe2.mapRevives), UiFlags::ColorWhitegold);
	return true;
}

} // namespace devilution

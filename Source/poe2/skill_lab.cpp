/**
 * @file poe2/skill_lab.cpp
 *
 * POE2 Mashup Skill Lab: the stand-in hero, the arena and the timing controls (see skill_lab.hpp).
 */
#include "poe2/skill_lab.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_iostream.h>
#else
#include <SDL.h>

#include "utils/sdl_compat.h"
#endif

#include "control/control.hpp"
#include "crawl.hpp"
#include "cursor.h"
#include "diablo.h"
#include "engine/point.hpp"
#include "gamemenu.h"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "lighting.h"
#include "menu.h"
#include "monster.h"
#include "multi.h"
#include "nthread.h"
#include "panels/skill_bar.hpp"
#include "pfile.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/aim.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/skill_lab_notes.hpp"
#include "poe2/skill_looks.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/telemetry_events.hpp"
#include "poe2/weapon_skills.hpp"
#include "quests.h"
#include "utils/file_util.h"
#include "utils/language.h"
#include "utils/paths.h"
#include "utils/str_cat.hpp"
#include "utils/png.h"
#include "utils/sdl_wrap.h"
#include "utils/surface_to_png.hpp"

namespace devilution {

namespace {

/** The lab hero's save slot: one past the hero list, so it never shows there. */
constexpr uint32_t LabSaveNumber = MAX_CHARACTERS;
constexpr int LabHeroLevel = 50;
/** High enough for any gem's attribute needs (157 at gem level 20). */
constexpr int LabAttributes = 300;
/** Life a target gets: no single hit takes it, and it's filled up every tick. */
constexpr int LabDummyLife = 1000000;
/** Game ticks between the end of one cast and the next while looping (about a second and a half). */
constexpr int LoopGapTicks = 30;
/** How long a lab cast keeps the cursor on its target, in game ticks, if the hero never goes back to standing. */
constexpr int MaxCastTicks = 200;

enum class Phase : uint8_t {
	Idle,
	/** The cast was sent; waiting for the hero to stand again. */
	Casting,
	/** Looping: waiting a moment before casting again. */
	Waiting,
};

struct WeaponKit {
	std::string name;
	std::optional<_item_indexes> main;
	std::optional<_item_indexes> offhand;
};

bool Active;
bool RestartPending;
bool ExitPending;
/** Set up the arena when the next level has loaded. */
bool ArenaPending;
bool ArenaReady;
/** The hero has had their level, attributes and weapon for this game. */
bool HeroReady;

std::vector<LabSkill> Skills;
int Selected = -1;
/** The class the user chose, or empty for the one that fits the skill. */
std::optional<HeroClass> ChosenClass;
/** The class of the hero in the game running now. */
HeroClass GameClass = HeroClass::Sorcerer;
std::vector<WeaponKit> Kits;
/** Index into Kits the user chose, or -1 for the one that fits the skill. */
int ChosenKit = -1;
int CurrentKit = -1;
int GemLevel = 1;

bool Looping;
int Slowdown = 1;
bool Paused;
int StepsPending;
float StepMsPending;
uint16_t NormalTickDelay = 50;

LabTargets Targets = LabTargets::Monsters;
int TargetCount = 1;
/** Index into LevelMonsterTypes. */
size_t MonsterType = 0;
LabFloor Floor = LabFloor::Cathedral;

Point Home;
Point TargetCentre;
/** A light over the targets, so ones a few steps away aren't lost in the dark. */
int TargetLight = NO_LIGHT;
constexpr uint8_t TargetLightRadius = 7;
struct Dummy {
	int id;
	Point home;
};
std::vector<Dummy> Dummies;
std::array<bool, MaxMonsters> DummyFlags {};

Phase CurrentPhase = Phase::Idle;
int PhaseTicks;
/** Game ticks since the latest cast began, or -1 before the first one. */
int CastTicks = -1;
/** The tick of the latest cast when the hero's swing or spell went off, or -1. */
int ActionTick = -1;
/** How many ticks the cast before took, for the timeline's length. */
int LastCastTicks;

LabLight Lighting = LabLight::Normal;
SupportGem Support = SupportGem::None;
/** Each loop swaps between the looks before and after the player's edits. */
bool Comparing;

/** Saving each tick of the next cast as a picture. */
bool RecordPending;
bool Recording;
std::string ClipFolder;
int ClipFrames;
int LastClipTick = -1;
/** The cast tick the effects were last moved on to while recording. */
int EffectTick;
/** Ticks still to record once the cast is over, for the effects to fade. */
int RecordTail;
constexpr int RecordTailTicks = 30;
constexpr int MaxClipFrames = 200;
/** Where the lab cast aims, while it's in flight. */
std::optional<Point> AimTile;
int AimMonster = -1;

int FloorLevel(LabFloor floor)
{
	switch (floor) {
	case LabFloor::Catacombs:
		return 5;
	case LabFloor::Caves:
		return 9;
	case LabFloor::Hell:
		return 13;
	case LabFloor::Nest:
		return 17;
	case LabFloor::Crypt:
		return 21;
	default:
		return 1;
	}
}

std::vector<HeroClass> AvailableClasses()
{
	// The shareware data only has the Warrior's pictures.
	if (gbIsSpawn)
		return { HeroClass::Warrior };
	std::vector<HeroClass> classes = { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk };
	if (gbIsHellfire) {
		classes.push_back(HeroClass::Bard);
		classes.push_back(HeroClass::Barbarian);
	}
	return classes;
}

GemCategory CategoryOf(const GemEntry &gem)
{
	for (uint8_t i = 0; i < static_cast<uint8_t>(GemCategory::All); i++) {
		const auto category = static_cast<GemCategory>(i);
		if (category != GemCategory::Support && GemInCategory(gem, category))
			return category;
	}
	return GemCategory::All;
}

void BuildSkills()
{
	Skills.clear();
	Skills.push_back(LabSkill { SkillSlot::ForDefault(0), std::string(_("Weapon attack")), GemCategory::All, 1, 1 });
	for (const GemEntry &gem : GemCatalog()) {
		if (gem.skill.isEmpty())
			continue;
		const bool duplicate = std::any_of(Skills.begin(), Skills.end(), [&gem](const LabSkill &skill) { return skill.skill == gem.skill; });
		if (duplicate)
			continue;
		Skills.push_back(LabSkill { gem.skill, std::string(SkillName(gem.skill)), CategoryOf(gem), gem.level, GemTier(gem.level) });
	}
	std::stable_sort(Skills.begin() + 1, Skills.end(), [](const LabSkill &a, const LabSkill &b) {
		if (a.category != b.category)
			return a.category < b.category;
		if (a.tier != b.tier)
			return a.tier < b.tier;
		return a.name < b.name;
	});
}

bool IsTwoHanded(const ItemData &data)
{
	return data.iLoc == ILOC_TWOHAND;
}

std::string KitName(const ItemData &data)
{
	const std::string_view name = data.iName;
	if (name.find("Dagger") != std::string_view::npos)
		return std::string(_("Dagger"));
	if (name.find("Crossbow") != std::string_view::npos)
		return std::string(_("Crossbow"));
	std::string_view type;
	switch (data.itype) {
	case ItemType::Sword:
		type = _("Sword");
		break;
	case ItemType::Axe:
		type = _("Axe");
		break;
	case ItemType::Mace:
		type = _("Mace");
		break;
	case ItemType::Bow:
		return std::string(_("Bow"));
	case ItemType::Staff:
		return std::string(_("Staff"));
	default:
		return std::string(name);
	}
	return IsTwoHanded(data) ? StrCat(_("Two-handed"), " ", type) : std::string(type);
}

/** @brief One weapon of each kind (one- and two-handed swords, axes and maces, daggers, bows, crossbows, staves), with and without a shield. */
void BuildKits()
{
	Kits.clear();
	Kits.push_back(WeaponKit { std::string(_("Bare hands")), std::nullopt, std::nullopt });
	std::optional<_item_indexes> shield;
	std::vector<std::pair<std::string, _item_indexes>> weapons;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (!IsItemAvailable(static_cast<int>(i)) || data.dropRate <= 0 || data.iMiscId == IMISC_UNIQUE)
			continue;
		if (data.itype == ItemType::Shield) {
			if (!shield || data.iMinMLvl < AllItemsList[*shield].iMinMLvl)
				shield = static_cast<_item_indexes>(i);
			continue;
		}
		if (data.iClass != ICLASS_WEAPON || !IsAnyOf(data.itype, ItemType::Sword, ItemType::Axe, ItemType::Mace, ItemType::Bow, ItemType::Staff))
			continue;
		const std::string name = KitName(data);
		auto it = std::find_if(weapons.begin(), weapons.end(), [&name](const auto &weapon) { return weapon.first == name; });
		if (it == weapons.end())
			weapons.emplace_back(name, static_cast<_item_indexes>(i));
		else if (data.iMinMLvl < AllItemsList[it->second].iMinMLvl)
			it->second = static_cast<_item_indexes>(i);
	}
	for (const auto &[name, index] : weapons) {
		Kits.push_back(WeaponKit { name, index, std::nullopt });
		if (shield && !IsTwoHanded(AllItemsList[index]))
			Kits.push_back(WeaponKit { StrCat(name, " + ", _("Shield")), index, shield });
	}
}

Item MakeItem(_item_indexes index)
{
	Item item {};
	InitializeItem(item, index);
	item._iSeed = static_cast<int32_t>(index) * 7919;
	SetupItem(item);
	item._iIdentified = true;
	return item;
}

bool KitAllowsSkill(const WeaponKit &kit, SkillSlot skill)
{
	if (skill.kind != SkillKind::Attack || !WeaponSkillNeedsWeapon(skill.attack()))
		return true;
	if (kit.main && ItemAllowsWeaponSkill(MakeItem(*kit.main), skill.attack()))
		return true;
	return kit.offhand && ItemAllowsWeaponSkill(MakeItem(*kit.offhand), skill.attack());
}

int FindKit(std::string_view name)
{
	for (size_t i = 0; i < Kits.size(); i++) {
		if (Kits[i].name == name)
			return static_cast<int>(i);
	}
	return -1;
}

/** @brief The kits a class or a gemcutting tab reaches for first. */
std::vector<std::string> PreferredKits(GemCategory category, HeroClass heroClass)
{
	const std::string sword = std::string(_("Sword"));
	const std::string mace = std::string(_("Mace"));
	const std::string axe = std::string(_("Axe"));
	const std::string shield = StrCat(" + ", _("Shield"));
	const std::string twoHanded = StrCat(_("Two-handed"), " ");
	switch (category) {
	case GemCategory::Mace:
		return { mace + shield, twoHanded + mace, mace };
	case GemCategory::Axe:
		return { twoHanded + axe, axe + shield, axe };
	case GemCategory::Sword:
		return { sword + shield, twoHanded + sword, sword, std::string(_("Dagger")) };
	case GemCategory::Shield:
		return { sword + shield, mace + shield, axe + shield };
	case GemCategory::Bow:
		return { std::string(_("Bow")), std::string(_("Crossbow")) };
	case GemCategory::Staff:
		return { std::string(_("Staff")), std::string(_("Bare hands")) };
	default:
		break;
	}
	switch (heroClass) {
	case HeroClass::Warrior:
		return { sword + shield };
	case HeroClass::Rogue:
		return { std::string(_("Bow")) };
	case HeroClass::Barbarian:
		return { twoHanded + axe };
	case HeroClass::Bard:
		return { sword };
	default:
		return { std::string(_("Staff")) };
	}
}

HeroClass FittingClass(const LabSkill &skill)
{
	switch (skill.category) {
	case GemCategory::Mace:
	case GemCategory::Axe:
	case GemCategory::Sword:
	case GemCategory::Shield:
		return HeroClass::Warrior;
	case GemCategory::Bow:
		return HeroClass::Rogue;
	case GemCategory::Staff:
		return HeroClass::Monk;
	default:
		return HeroClass::Sorcerer;
	}
}

HeroClass WantedClass()
{
	HeroClass wanted = GameClass;
	if (ChosenClass)
		wanted = *ChosenClass;
	else if (Selected >= 0)
		wanted = FittingClass(Skills[Selected]);
	const std::vector<HeroClass> classes = AvailableClasses();
	if (std::find(classes.begin(), classes.end(), wanted) == classes.end())
		return classes.front();
	return wanted;
}

int FittingKit()
{
	if (Selected < 0)
		return FindKit(PreferredKits(GemCategory::All, GameClass).front());
	const LabSkill &skill = Skills[Selected];
	for (const std::string &name : PreferredKits(skill.category, GameClass)) {
		const int kit = FindKit(name);
		if (kit >= 0 && KitAllowsSkill(Kits[kit], skill.skill))
			return kit;
	}
	for (size_t i = 0; i < Kits.size(); i++) {
		if (KitAllowsSkill(Kits[i], skill.skill))
			return static_cast<int>(i);
	}
	return 0;
}

void Equip(Player &player, int kitIndex)
{
	if (kitIndex < 0 || static_cast<size_t>(kitIndex) >= Kits.size())
		return;
	const WeaponKit &kit = Kits[kitIndex];
	player.InvBody[INVLOC_HAND_LEFT].clear();
	player.InvBody[INVLOC_HAND_RIGHT].clear();
	if (kit.main)
		player.InvBody[INVLOC_HAND_LEFT] = MakeItem(*kit.main);
	if (kit.offhand)
		player.InvBody[INVLOC_HAND_RIGHT] = MakeItem(*kit.offhand);
	CurrentKit = kitIndex;
	CalcPlrInv(player, true);
	if (player._pmode == PM_STAND)
		StartStand(player, player._pdir);
}

/** @brief Puts the skill on show in the first Skills panel socket and on the Q key, at the lab's gem level. */
/** @brief Puts the chosen support gem, at its best tier, in the skill's first support socket. */
void ApplySupport(Player &player)
{
	player.poe2.supportsAt(0).fill(SupportGem::None);
	player.poe2.supportsAt(DefaultAttackRow).fill(SupportGem::None);
	if (Selected < 0 || Support == SupportGem::None)
		return;
	const SkillSlot skill = Skills[Selected].skill;
	if (!SupportGemCanSupport(Support, skill)) {
		Support = SupportGem::None;
		return;
	}
	const size_t row = skill.kind == SkillKind::Default ? DefaultAttackRow : 0;
	player.poe2.supportsAt(row)[0] = Support;
	player.poe2.tiersAt(row)[0] = static_cast<uint8_t>(SupportTierCount(Support));
}

void SocketSelected(Player &player)
{
	for (size_t slot = 0; slot < NumSkillSlots; slot++)
		SocketSkill(player, slot, {}, 1);
	BindTraySlot(player, TraySlot::LeftMouse, DefaultTrayBinding(TraySlot::LeftMouse));
	if (Selected < 0)
		return;
	const SkillSlot skill = Skills[Selected].skill;
	if (skill.kind == SkillKind::Default) {
		ApplySupport(player);
		return;
	}
	SocketSkill(player, 0, skill, GemLevel);
	if (SkillGoesOnTray(skill))
		BindTraySlot(player, TrayKeySlot(0), TrayBinding::ForGem(0));
	ApplySupport(player);
}

void SetUpHero(Player &player)
{
	while (player.getCharacterLevel() < LabHeroLevel && !player.isMaxCharacterLevel()) {
		player._pExperience = player.getNextExperienceThreshold();
		NextPlrLevel(player);
	}
	ModifyPlrStr(player, LabAttributes - player._pBaseStr);
	ModifyPlrMag(player, LabAttributes - player._pBaseMag);
	ModifyPlrDex(player, LabAttributes - player._pBaseDex);
	ModifyPlrVit(player, LabAttributes - player._pBaseVit);
	player.poe2.hardcore = false;
	HeroReady = true;
}

bool IsFree(Point tile)
{
	return InDungeonBounds(tile) && IsTileWalkable(tile) && dMonster[tile.x][tile.y] == 0 && dPlayer[tile.x][tile.y] == 0 && dObject[tile.x][tile.y] == 0;
}

/** @brief A tile nothing stands on and nothing blocks: no wall, pillar, object or anything that stops light or shots. */
bool IsClear(Point tile)
{
	return InDungeonBounds(tile) && IsTileWalkable(tile) && dObject[tile.x][tile.y] == 0
	    && !TileHasAny(tile, TileProperties::Solid | TileProperties::BlockLight | TileProperties::BlockMissile);
}

/** @brief Whether every tile within the radius of the tile is clear. */
bool IsClearAround(Point tile, int radius)
{
	for (int dx = -radius; dx <= radius; dx++) {
		for (int dy = -radius; dy <= radius; dy++) {
			if (!IsClear(tile + Displacement { dx, dy }))
				return false;
		}
	}
	return true;
}

/**
 * @brief The middle of the arena: a spot with a clear lane from the hero (left of it on screen) to the targets
 * (right of it), and the most open floor around it.
 */
Point FindArenaCentre()
{
	constexpr int Radius = 6;
	Point best = MyPlayer->position.tile;
	int bestScore = -1;
	const Point middle { MAXDUNX / 2, MAXDUNY / 2 };
	for (int x = Radius; x < MAXDUNX - Radius; x++) {
		for (int y = Radius; y < MAXDUNY - Radius; y++) {
			const Point centre { x, y };
			if (!IsClear(centre))
				continue;
			int score = 0;
			for (int dx = -Radius; dx <= Radius; dx++) {
				for (int dy = -Radius; dy <= Radius; dy++) {
					if (IsClear(centre + Displacement { dx, dy }))
						score++;
				}
			}
			// The lane: clear around the hero, the targets and every step between.
			bool lane = IsClearAround(centre + Displacement { -2, 2 }, 1) && IsClearAround(centre + Displacement { 2, -2 }, 2);
			for (int step = -1; lane && step <= 1; step++)
				lane = IsClearAround(centre + Displacement { step, -step }, 1);
			if (lane)
				score += 1000;
			// Between two equal spots, the one nearer the middle of the map.
			score = (score * 256) - centre.ManhattanDistance(middle);
			if (score > bestScore) {
				bestScore = score;
				best = centre;
			}
		}
	}
	return best;
}

/** @brief Melee skills want their targets next to the hero; everything else a few steps away, to the right on screen. */
bool WantsCloseTargets()
{
	if (Selected < 0)
		return false;
	const SkillSlot skill = Skills[Selected].skill;
	if (skill.kind == SkillKind::Default)
		return CurrentKit < 0 || !Kits[CurrentKit].main || AllItemsList[*Kits[CurrentKit].main].itype != ItemType::Bow;
	return skill.kind == SkillKind::Attack && !IsRangedWeaponSkill(skill.attack());
}

Point PickTargetCentre()
{
	const int distance = WantsCloseTargets() ? 2 : 4;
	// Screen right first (x up, y down), then the other directions.
	constexpr std::array<Displacement, 8> Ways = { { { 1, -1 }, { 1, 0 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 0, 1 }, { -1, 0 }, { -1, 1 } } };
	for (const Displacement way : Ways) {
		const Point tile = Home + way * distance;
		if (IsClear(tile))
			return tile;
	}
	return Home + Displacement { 1, -1 };
}

void ClearDummies()
{
	for (size_t i = 0; i < ActiveMonsterCount;) {
		const int id = static_cast<int>(ActiveMonsters[i]);
		if (DummyFlags[id]) {
			DummyFlags[id] = false;
			RemoveMonsterQuietly(i);
			continue;
		}
		i++;
	}
	Dummies.clear();
}

void SpawnDummies()
{
	ClearDummies();
	if (Targets != LabTargets::Monsters || LevelMonsterTypeCount == 0)
		return;
	MonsterType = std::min(MonsterType, LevelMonsterTypeCount - 1);
	int placed = 0;
	Crawl(0, 6, [&](Displacement displacement) {
		const Point tile = TargetCentre + displacement;
		if (tile == Home || !IsFree(tile))
			return false;
		Monster *monster = AddMonster(tile, GetDirection(tile, Home), MonsterType, true);
		if (monster == nullptr)
			return true;
		monster->maxHitPoints = Fixed26_6::fromInt(LabDummyLife);
		monster->hitPoints = monster->maxHitPoints;
		const int id = static_cast<int>(monster->getId());
		DummyFlags[id] = true;
		Dummies.push_back(Dummy { id, tile });
		placed++;
		return placed >= TargetCount;
	});
}

/** @brief Puts the hero and the targets back where they started, if a skill moved them. */
void ResetPositions()
{
	Player &player = *MyPlayer;
	if (player.position.tile != Home && player._pmode == PM_STAND && IsFree(Home))
		MovePlayerTo(player, Home);
	if (player._pmode == PM_STAND)
		player._pdir = GetDirection(Home, TargetCentre);
	const bool moved = std::any_of(Dummies.begin(), Dummies.end(), [](const Dummy &dummy) {
		const Monster &monster = Monsters[dummy.id];
		return monster.position.tile != dummy.home || monster.hasNoLife();
	});
	if (moved || static_cast<int>(Dummies.size()) != (Targets == LabTargets::Monsters ? TargetCount : 0))
		SpawnDummies();
}

void SetUpArena()
{
	Player &player = *MyPlayer;
	// The floor's own monsters go; the hero's minions stay.
	for (size_t i = 0; i < ActiveMonsterCount;) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion() || ActiveMonsters[i] < 4) {
			i++;
			continue;
		}
		if (monster.lightId != NO_LIGHT)
			AddUnLight(monster.lightId);
		RemoveMonsterQuietly(i);
	}
	DummyFlags.fill(false);
	Dummies.clear();
	MonsterType = 0;
	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		if (LevelMonsterTypes[i].type != MT_GOLEM) {
			MonsterType = i;
			break;
		}
	}
	// The hero stands left of the middle of the open spot, the targets right of it.
	const Point middle = FindArenaCentre();
	Home = middle + Displacement { -2, 2 };
	if (!IsTileWalkable(Home))
		Home = middle;
	if (player.position.tile != Home)
		MovePlayerTo(player, Home);
	TargetCentre = PickTargetCentre();
	TargetLight = AddLight(TargetCentre, TargetLightRadius);
	player._pdir = GetDirection(Home, TargetCentre);
	StartStand(player, player._pdir);
	SpawnDummies();
	ArenaReady = true;
	CurrentPhase = Phase::Idle;
	CastTicks = -1;
}

void ApplySkill()
{
	if (MyPlayer == nullptr || !HeroReady)
		return;
	Player &player = *MyPlayer;
	const int kit = ChosenKit >= 0 ? ChosenKit : FittingKit();
	if (kit != CurrentKit)
		Equip(player, kit);
	SocketSelected(player);
	if (ArenaReady) {
		const Point centre = PickTargetCentre();
		if (centre != TargetCentre) {
			TargetCentre = centre;
			if (TargetLight != NO_LIGHT)
				ChangeLightXY(TargetLight, TargetCentre);
			SpawnDummies();
		}
	}
	CurrentPhase = Phase::Idle;
	CastTicks = -1;
	LabDropMoment();
}

void ApplyTickDelay()
{
	gnTickDelay = static_cast<uint16_t>(NormalTickDelay * Slowdown);
}

/** @brief Ends the game running now; the lab picks up again in SkillLabPrepareHero, or closes. */
void EndGame()
{
	for (Player &player : Players) {
		player._pmode = PM_QUIT;
		player._pInvincible = true;
	}
	MyPlayerIsDead = false;
	gamemenu_off();
	gbRunGame = false;
}

void RequestRestart()
{
	RestartPending = true;
	EndGame();
}

void AimAt(Point tile, int monster)
{
	cursPosition = tile;
	pcursmonst = monster;
	pcursitem = -1;
	ObjectUnderCursor = nullptr;
	PlayerUnderCursor = nullptr;
	// Shots aim a tile below the cursor (see AimAlongCursor), so the cursor goes a tile above the target.
	SetCursorTileExact(static_cast<float>(tile.x) - 1.F, static_cast<float>(tile.y) - 1.F);
}

} // namespace

bool SkillLabActive()
{
	return Active;
}

void SkillLabBegin()
{
	Active = true;
	RestartPending = false;
	ExitPending = false;
	HeroReady = false;
	ArenaReady = false;
	Paused = false;
	StepsPending = 0;
	StepMsPending = 0;
	CastTicks = -1;
	TelemetryPaused = true;
	LabLoadNotes();
}

void SkillLabEnd()
{
	if (!Active)
		return;
	Active = false;
	Paused = false;
	Slowdown = 1;
	Looping = false;
	AimTile = std::nullopt;
	gnTickDelay = NormalTickDelay;
	TelemetryPaused = false;
	Comparing = false;
	SetSkillLookEditsOn(true);
	RecordPending = false;
	Recording = false;
}

bool SkillLabPrepareHero(GameData *gameData)
{
	if (ExitPending || (!RestartPending && HeroReady)) {
		// Leaving the lab (Exit, or New Game from the game menu).
		ExitPending = false;
		return false;
	}
	RestartPending = false;
	HeroReady = false;
	ArenaReady = false;
	CurrentKit = -1;
	GameClass = WantedClass();
	gbLoadGame = false;
	gameData->nDifficulty = DIFF_NORMAL;
	gSaveNumber = LabSaveNumber;
	return pfile_create_lab_hero(LabSaveNumber, GameClass, _("Stand-in"));
}

void SkillLabPrepareNewGame()
{
	if (!Active)
		return;
	for (Quest &quest : Quests)
		quest._qactive = QUEST_NOTAVAIL;
	const int level = FloorLevel(Floor);
	// The same layout each time, so the arena doesn't move under the user.
	DungeonSeeds[level] = 0x5C111AB0U + static_cast<uint32_t>(level);
	currlevel = static_cast<uint8_t>(level);
	leveltype = GetLevelType(level);
	MyPlayer->setLevel(static_cast<uint8_t>(level));
	ArenaPending = true;
}

void SkillLabLevelLoaded()
{
	if (!Active || MyPlayer == nullptr || leveltype == DTYPE_TOWN)
		return;
	NormalTickDelay = static_cast<uint16_t>(1000 / std::max<int>(sgGameInitInfo.nTickRate, 1));
	ApplyTickDelay();
	if (Skills.empty())
		BuildSkills();
	BuildKits();
	Player &player = *MyPlayer;
	if (!HeroReady)
		SetUpHero(player);
	CurrentKit = -1;
	ApplySkill();
	SetUpArena();
	ArenaPending = false;
}

bool SkillLabHoldsTick()
{
	// While recording, each tick waits for its picture, however long saving takes, so no tick is missed.
	if (Active && Recording && CastTicks != LastClipTick)
		return true;
	if (!Active || !Paused)
		return false;
	if (StepsPending > 0) {
		StepsPending--;
		return false;
	}
	return true;
}

namespace {

/** @brief Whether the hero's swing or spell has reached the frame where it goes off. */
bool HeroStruck(const Player &player)
{
	const int frame = player.AnimInfo.currentFrame;
	switch (player._pmode) {
	case PM_ATTACK:
	case PM_RATTACK:
		return frame >= player._pAFNum - 1;
	case PM_SPELL:
		return frame >= (player.poe2.castSkill != WeaponSkill::None ? player.poe2.castFrame : player._pSFNum - 1);
	default:
		return false;
	}
}

void SetLightRadius(int light, uint8_t radius)
{
	if (light != NO_LIGHT && Lights[light].radius != radius)
		ChangeLightRadius(light, radius);
}

/** @brief Bright lights the whole arena, Dark leaves only a glimmer round the hero, Normal is the game's own. */
void ApplyLighting(const Player &player)
{
	switch (Lighting) {
	case LabLight::Bright:
		SetLightRadius(player.lightId, 15);
		SetLightRadius(TargetLight, 15);
		break;
	case LabLight::Dark:
		SetLightRadius(player.lightId, 2);
		SetLightRadius(TargetLight, 1);
		break;
	default:
		SetLightRadius(player.lightId, static_cast<uint8_t>(std::max<int>(player._pLightRad, 0)));
		SetLightRadius(TargetLight, TargetLightRadius);
		break;
	}
}

} // namespace

void SkillLabTick()
{
	if (!Active || MyPlayer == nullptr)
		return;
	Player &player = *MyPlayer;
	player.RestoreFullLife();
	player.RestoreFullMana();
	for (const Dummy &dummy : Dummies) {
		Monster &monster = Monsters[dummy.id];
		if (!monster.hasNoLife())
			monster.hitPoints = monster.maxHitPoints;
	}
	if (!ArenaReady)
		return;

	ApplyLighting(player);
	PhaseTicks++;
	// The cast's clock runs on a little after it ends, while its effects fade, then stops for the timeline.
	if (CastTicks >= 0 && (CurrentPhase == Phase::Casting || CastTicks < LastCastTicks + RecordTailTicks))
		CastTicks++;
	if (Recording && CurrentPhase != Phase::Casting && --RecordTail <= 0)
		Recording = false;
	switch (CurrentPhase) {
	case Phase::Casting:
		if (ActionTick < 0 && HeroStruck(player))
			ActionTick = CastTicks;
		if ((player._pmode == PM_STAND && PhaseTicks > 2 && player.destAction == ACTION_NONE) || PhaseTicks > MaxCastTicks) {
			AimTile = std::nullopt;
			CurrentPhase = Looping ? Phase::Waiting : Phase::Idle;
			PhaseTicks = 0;
			LastCastTicks = CastTicks;
			RecordTail = RecordTailTicks;
		}
		break;
	case Phase::Waiting:
		if (PhaseTicks >= LoopGapTicks) {
			ResetPositions();
			if (Comparing)
				SetSkillLookEditsOn(!SkillLookEditsOn());
			LabCast();
		}
		break;
	case Phase::Idle:
		break;
	}
}

void SkillLabAfterCursorMove()
{
	if (Active && AimTile)
		AimAt(*AimTile, AimMonster);
}

float SkillLabEffectMs(float elapsedMs)
{
	if (!Active)
		return elapsedMs;
	if (Recording) {
		// One tick's worth for each tick recorded, so the effects keep step with the pictures.
		const int ticks = std::max(CastTicks - EffectTick, 0);
		EffectTick = CastTicks;
		return static_cast<float>(ticks * NormalTickDelay);
	}
	if (Paused) {
		const float step = StepMsPending;
		StepMsPending = 0;
		return step;
	}
	return elapsedMs / static_cast<float>(Slowdown);
}

bool SkillLabFreezesAnimation()
{
	return Active && ((Paused && StepsPending == 0) || Recording);
}

bool IsLabDummy(const Monster &monster)
{
	return Active && DummyFlags[monster.getId()];
}

const std::vector<LabSkill> &LabSkills()
{
	if (Skills.empty())
		BuildSkills();
	return Skills;
}

int LabSelectedSkill()
{
	return Selected;
}

void LabSelectSkill(int index)
{
	if (index < 0 || static_cast<size_t>(index) >= LabSkills().size())
		return;
	Selected = index;
	GemLevel = std::max(Skills[index].gemLevel, 1);
	if (WantedClass() != GameClass) {
		RequestRestart();
		return;
	}
	ApplySkill();
	ResetPositions();
	if (Looping)
		LabCast();
}

namespace {

void StartRecording()
{
	RecordPending = false;
	const std::time_t now = std::time(nullptr);
	std::tm local {};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
	std::string name = SkillLookKey(Skills[Selected].skill);
	std::replace(name.begin(), name.end(), ' ', '_');
	std::string folder = TelemetryFolder();
	if (folder.empty())
		folder = StrCat(paths::PrefPath(), "poe2_logs", DIRECTORY_SEPARATOR_STR);
	ClipFolder = StrCat(folder, "skill_lab_clips", DIRECTORY_SEPARATOR_STR, name, "_", stamp, DIRECTORY_SEPARATOR_STR);
	RecursivelyCreateDir(ClipFolder.c_str());
	ClipFrames = 0;
	LastClipTick = -1;
	EffectTick = CastTicks;
	RecordTail = RecordTailTicks;
	Recording = true;
}

} // namespace

void LabCast()
{
	if (!ArenaReady || Selected < 0 || MyPlayer == nullptr)
		return;
	Player &player = *MyPlayer;
	if (!player.CanChangeAction() && player._pmode != PM_STAND)
		return;
	int monster = -1;
	Point tile = TargetCentre;
	for (const Dummy &dummy : Dummies) {
		if (!Monsters[dummy.id].hasNoLife()) {
			monster = dummy.id;
			tile = Monsters[dummy.id].position.tile;
			break;
		}
	}
	const Point mouse = MousePosition;
	MousePosition = { GetMainPanel().position.x + (GetMainPanel().size.width / 2), GetMainPanel().position.y / 2 };
	const int cursor = pcurs;
	if (pcurs != CURSOR_HAND)
		NewCursor(CURSOR_HAND);
	AimTile = tile;
	AimMonster = monster;
	AimAt(tile, monster);
	const SkillSlot skill = Skills[Selected].skill;
	if (skill.kind == SkillKind::Default)
		UseTraySlot(TraySlot::LeftMouse);
	else
		UseSkillSlot(0);
	MousePosition = mouse;
	if (cursor != CURSOR_HAND)
		NewCursor(cursor);
	CurrentPhase = Phase::Casting;
	PhaseTicks = 0;
	CastTicks = 0;
	ActionTick = -1;
	LabDropMoment();
	if (RecordPending)
		StartRecording();
}

bool LabLooping()
{
	return Looping;
}

void LabSetLooping(bool loop)
{
	Looping = loop;
	if (loop && CurrentPhase == Phase::Idle)
		LabCast();
	if (!loop && CurrentPhase == Phase::Waiting)
		CurrentPhase = Phase::Idle;
}

int LabSlowdown()
{
	return Slowdown;
}

void LabSetSlowdown(int slowdown)
{
	Slowdown = std::clamp(slowdown, 1, 4);
	ApplyTickDelay();
}

bool LabPaused()
{
	return Paused;
}

void LabSetPaused(bool paused)
{
	Paused = paused;
	StepsPending = 0;
	StepMsPending = 0;
}

void LabStep()
{
	if (!Paused) {
		LabSetPaused(true);
		return;
	}
	StepsPending++;
	StepMsPending += static_cast<float>(NormalTickDelay);
}

LabTargets LabTargetMode()
{
	return Targets;
}

void LabSetTargetMode(LabTargets targets)
{
	Targets = targets;
	if (ArenaReady)
		SpawnDummies();
}

int LabTargetCount()
{
	return TargetCount;
}

void LabSetTargetCount(int count)
{
	TargetCount = std::clamp(count, 1, 12);
	if (ArenaReady)
		SpawnDummies();
}

std::string LabMonsterName()
{
	if (LevelMonsterTypeCount == 0 || !ArenaReady)
		return {};
	return std::string(LevelMonsterTypes[std::min(MonsterType, LevelMonsterTypeCount - 1)].data().name);
}

void LabCycleMonster(int delta)
{
	if (LevelMonsterTypeCount == 0 || !ArenaReady)
		return;
	const int count = static_cast<int>(LevelMonsterTypeCount);
	int next = static_cast<int>(MonsterType);
	for (int i = 0; i < count; i++) {
		next = (next + delta + count) % count;
		if (LevelMonsterTypes[next].type != MT_GOLEM)
			break;
	}
	MonsterType = static_cast<size_t>(next);
	SpawnDummies();
}

LabFloor LabCurrentFloor()
{
	return Floor;
}

bool LabFloorAvailable(LabFloor floor)
{
	return gbIsHellfire || IsNoneOf(floor, LabFloor::Nest, LabFloor::Crypt);
}

void LabSetFloor(LabFloor floor)
{
	if (floor == Floor || !LabFloorAvailable(floor) || MyPlayer == nullptr)
		return;
	Floor = floor;
	const int level = FloorLevel(floor);
	DungeonSeeds[level] = 0x5C111AB0U + static_cast<uint32_t>(level);
	ArenaReady = false;
	ArenaPending = true;
	AimTile = std::nullopt;
	CurrentPhase = Phase::Idle;
	StartNewLvl(*MyPlayer, level == 21 ? WM_DIABTOWNWARP : WM_DIABNEXTLVL, level);
}

std::string_view LabFloorName(LabFloor floor)
{
	switch (floor) {
	case LabFloor::Cathedral:
		return _("Cathedral");
	case LabFloor::Catacombs:
		return _("Catacombs");
	case LabFloor::Caves:
		return _("Caves");
	case LabFloor::Hell:
		return _("Hell");
	case LabFloor::Nest:
		return _("Nest");
	case LabFloor::Crypt:
		return _("Crypt");
	default:
		return {};
	}
}

HeroClass LabHeroClass()
{
	return GameClass;
}

bool LabHeroAuto()
{
	return !ChosenClass;
}

void LabCycleHeroClass(int delta)
{
	const std::vector<HeroClass> classes = AvailableClasses();
	const int count = static_cast<int>(classes.size());
	// Position 0 is "fits the skill", then each class.
	int position = 0;
	if (ChosenClass) {
		const auto it = std::find(classes.begin(), classes.end(), *ChosenClass);
		position = it == classes.end() ? 0 : static_cast<int>(it - classes.begin()) + 1;
	}
	position = (position + delta + count + 1) % (count + 1);
	ChosenClass = position == 0 ? std::nullopt : std::optional(classes[position - 1]);
	if (WantedClass() != GameClass)
		RequestRestart();
}

std::string LabWeaponName()
{
	if (CurrentKit < 0 || static_cast<size_t>(CurrentKit) >= Kits.size())
		return {};
	return Kits[CurrentKit].name;
}

bool LabWeaponAuto()
{
	return ChosenKit < 0;
}

void LabCycleWeapon(int delta)
{
	if (Kits.empty() || MyPlayer == nullptr)
		return;
	const int count = static_cast<int>(Kits.size());
	// -1 is "fits the skill".
	ChosenKit = ((ChosenKit + 1 + delta + count + 1) % (count + 1)) - 1;
	ApplySkill();
}

int LabGemLevel()
{
	return GemLevel;
}

void LabSetGemLevel(int level)
{
	GemLevel = std::clamp(level, 1, 20);
	if (MyPlayer != nullptr && HeroReady)
		SocketSelected(*MyPlayer);
}

void LabExit()
{
	ExitPending = true;
	EndGame();
}

int LabTicksSinceCast()
{
	return CastTicks;
}

int LabActionTick()
{
	return ActionTick;
}

int LabLastCastTicks()
{
	return LastCastTicks;
}

LabLight LabLighting()
{
	return Lighting;
}

void LabSetLighting(LabLight light)
{
	Lighting = light;
}

SupportGem LabSupport()
{
	return Support;
}

void LabCycleSupport(int delta)
{
	if (Selected < 0)
		return;
	const SkillSlot skill = Skills[Selected].skill;
	std::vector<SupportGem> fitting { SupportGem::None };
	for (uint8_t i = 1; i < static_cast<uint8_t>(SupportGem::Count); i++) {
		const auto gem = static_cast<SupportGem>(i);
		if (SupportGemCanSupport(gem, skill))
			fitting.push_back(gem);
	}
	const auto it = std::find(fitting.begin(), fitting.end(), Support);
	const int at = it == fitting.end() ? 0 : static_cast<int>(it - fitting.begin());
	const int count = static_cast<int>(fitting.size());
	Support = fitting[((at + delta) % count + count) % count];
	if (MyPlayer != nullptr && HeroReady)
		ApplySupport(*MyPlayer);
}

bool LabComparing()
{
	return Comparing;
}

void LabSetComparing(bool compare)
{
	Comparing = compare;
	if (compare && !Looping)
		LabSetLooping(true);
}

bool LabRecording()
{
	return Recording || RecordPending;
}

void LabRecord()
{
	if (LabRecording() || Selected < 0)
		return;
	RecordPending = true;
	if (CurrentPhase != Phase::Casting)
		LabCast();
}

namespace {

/** Whether frames come from the screen as shown (with the effects drawn over the game), not from the game's own picture. */
bool PresentedFrames;

std::string NextClipPath()
{
	char name[24];
	std::snprintf(name, sizeof(name), "frame_%03d.png", ClipFrames++);
	return StrCat(ClipFolder, name);
}

/** @brief Whether this tick's frame is still to be saved; stops the recording when it's long enough. */
bool WantsClipFrame()
{
	if (!Active || !Recording || CastTicks == LastClipTick)
		return false;
	LastClipTick = CastTicks;
	if (ClipFrames >= MaxClipFrames) {
		Recording = false;
		return false;
	}
	return true;
}

} // namespace

#if !defined(USE_SDL1) && !defined(USE_SDL3)
void SkillLabCapturePresented(SDL_Renderer *renderer)
{
	PresentedFrames = true;
	if (!WantsClipFrame())
		return;
	int width;
	int height;
	if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0)
		return;
	SDLSurfaceUniquePtr frame = SDLWrap::CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
	if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, frame->pixels, frame->pitch) != 0 || IMG_SavePNG(frame.get(), NextClipPath().c_str()) != 0)
		Recording = false;
}
#endif

void SkillLabCaptureFrame(const Surface &out)
{
	// The screen as shown has the effects, which the game's own picture doesn't.
	if (PresentedFrames || !WantsClipFrame())
		return;
	const std::string path = NextClipPath();
	SDL_IOStream *stream = SDL_IOFromFile(path.c_str(), "wb");
	if (stream == nullptr) {
		Recording = false;
		return;
	}
	if (!WriteSurfaceToFilePng(out, stream).has_value())
		Recording = false;
}

bool SkillLabRecordingNow()
{
	return Active && Recording;
}

} // namespace devilution

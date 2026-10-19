#include <algorithm>
#include <optional>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/meta_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

constexpr size_t MetaRow = 2;
const SkillSlot Firebolt = SkillSlot::ForSpell(SpellID::Firebolt);
const SkillSlot Fireball = SkillSlot::ForSpell(SpellID::Fireball);
const SkillSlot CastOnDodge = SkillSlot::ForPersistent(PersistentSkill::CastOnDodge);

class Poe2MetaGemsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->_pStrength = 200;
		MyPlayer->_pMagic = 200;
		MyPlayer->_pDexterity = 200;
		// Every Spirit skull broken: 100 Spirit, enough for one meta gem.
		MyPlayer->poe2.spiritSkulls = 7;
		MyPlayer->poe2.spiritGemsOn = 0xFFFF;
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMissileData();
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pMaxMana = MyPlayer->_pMana = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxManaBase = MyPlayer->_pManaBase = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(1000);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		ClearSupportEffects();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	static Monster MakeMonster(uint32_t seed)
	{
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.flags = 0;
		monster.rndItemSeed = seed;
		monster.position.tile = { 43, 40 };
		return monster;
	}

	static Monster MonsterOf(MonsterRarity rarity)
	{
		for (uint32_t seed = 1; seed < 100000; seed++) {
			Monster monster = MakeMonster(seed);
			if (GetMonsterAffixes(monster).rarity == rarity)
				return monster;
		}
		ADD_FAILURE();
		return MakeMonster(0);
	}

	/** @brief Puts a monster in the level, where Blasphemy and totems look for them. */
	static Monster &PlaceMonster(Point tile, int life = 100)
	{
		Monster &monster = Monsters[0];
		monster = MakeMonster(1);
		monster.talkMsg = TEXT_NONE;
		monster.position.tile = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(life);
		ActiveMonsters[0] = 0;
		ActiveMonsterCount = 1;
		return monster;
	}

	static MetaSocketResult Inside(size_t socket, SkillSlot skill, int level = 1, SkillSlot *replaced = nullptr)
	{
		SkillSlot out;
		int outLevel = 1;
		const MetaSocketResult result = SocketSkillInMeta(*MyPlayer, MetaRow, socket, skill, level, out, outLevel);
		if (replaced != nullptr)
			*replaced = out;
		return result;
	}
};

TEST_F(Poe2MetaGemsTest, SpellsGoInsideAndComeOut)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[MetaRow] = CastOnDodge;
	EXPECT_TRUE(SkillTakesSupports(CastOnDodge));
	EXPECT_EQ(SpiritReservation(PersistentSkill::CastOnDodge), 100);
	EXPECT_EQ(Inside(0, Firebolt, 3), MetaSocketResult::Socketed);
	EXPECT_EQ(Inside(1, Fireball), MetaSocketResult::Socketed);
	EXPECT_EQ(MetaSkillCount(data, MetaRow), 2);
	EXPECT_EQ(MetaMaxEnergy(data, MetaRow), 2 * MetaEnergyPerSpell);
	// A spell inside casts at its own gem's level.
	EXPECT_EQ(data.skillLevel(Firebolt), 3);
	EXPECT_GE(MyPlayer->GetSpellLevel(SpellID::Firebolt), 3);

	// A spell is in one socket of the Skills panel only.
	EXPECT_EQ(Inside(1, Firebolt), MetaSocketResult::AlreadySocketed);
	data.skillSlots[0] = SkillSlot::ForSpell(SpellID::ChainLightning);
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::ChainLightning)), MetaSocketResult::AlreadySocketed);
	// Movement spells, minions and attacks can't go inside.
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::Teleport)), MetaSocketResult::NotTriggerable);
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::Golem)), MetaSocketResult::NotTriggerable);
	EXPECT_EQ(Inside(1, SkillSlot::ForAttack(WeaponSkill::Cleave)), MetaSocketResult::NotTriggerable);
	// The third socket opens with a Jeweller's Orb, as for supports.
	EXPECT_EQ(Inside(2, SkillSlot::ForSpell(SpellID::ChargedBolt)), MetaSocketResult::Locked);
	SkillSlot replaced;
	int replacedLevel = 0;
	EXPECT_EQ(SocketSkillInMeta(*MyPlayer, 0, 0, SkillSlot::ForSpell(SpellID::ChargedBolt), 1, replaced, replacedLevel), MetaSocketResult::NotMeta);

	// Another spell in a taken socket hands the old one back.
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::ChargedBolt), 1, &replaced), MetaSocketResult::Replaced);
	EXPECT_EQ(replaced, Fireball);
	EXPECT_EQ(data.skillLevel(Fireball), 0);

	// Taking a spell out shortens the bar and keeps what Energy fits.
	data.supportState.metaEnergy[MetaRow] = 150 * 100;
	int level = 0;
	EXPECT_EQ(UnsocketMetaSkill(data, MetaRow, 0, &level), Firebolt);
	EXPECT_EQ(level, 3);
	EXPECT_EQ(MetaEnergy(*MyPlayer, MetaRow), MetaEnergyPerSpell);
}

TEST_F(Poe2MetaGemsTest, SupportsInTheRowWorkOnTheSpellsInside)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[MetaRow] = CastOnDodge;
	SupportGem replaced;
	// With no spell inside, nothing fits.
	EXPECT_EQ(SocketSupportGem(*MyPlayer, MetaRow, 1, SupportGem::IncreasedArea, replaced), SupportSocketResult::NotApplicable);
	ASSERT_EQ(Inside(0, Fireball), MetaSocketResult::Socketed);
	EXPECT_EQ(SocketSupportGem(*MyPlayer, MetaRow, 0, SupportGem::IncreasedArea, replaced), SupportSocketResult::SpellInSocket);
	EXPECT_EQ(FindSlotForSupport(*MyPlayer, SupportGem::IncreasedArea), MetaRow);
	ASSERT_EQ(SocketSupportGem(*MyPlayer, MetaRow, std::nullopt, SupportGem::IncreasedArea, replaced), SupportSocketResult::Socketed);
	EXPECT_EQ(data.supportsAt(MetaRow)[1], SupportGem::IncreasedArea);
	EXPECT_TRUE(data.hasSupport(Fireball, SupportGem::IncreasedArea));
	EXPECT_GT(SupportAreaRadius(*MyPlayer, Fireball, 10), 10);
	// Both sockets are taken now.
	EXPECT_EQ(FindSlotForSupport(*MyPlayer, SupportGem::Pierce), std::nullopt);

	// Supports that act when you use a skill yourself don't fit triggered spells (POE2).
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::SpellEcho, Fireball));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::SpellEcho, CastOnDodge));
	// The old trigger supports fit nothing now.
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::CastOnDodge, Fireball));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::CastWhenHit, Firebolt));
}

TEST_F(Poe2MetaGemsTest, DodgingCastsTheSpellsInsideForTheirMana)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = CastOnDodge;
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::CastOnDodge));
	const int cost = SkillManaCost(player, Firebolt);
	ASSERT_GT(cost, 0);

	OnMetaDodge(player, 2);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 2 * CastOnDodgeEnergyPerTile);
	EXPECT_TRUE(Missiles.empty());
	int rolls = 1;
	while (Missiles.empty() && rolls < 20) {
		OnMetaDodge(player, 2);
		rolls++;
	}
	// 34 Energy a roll of 2 tiles fills an 80 Energy bar on the third.
	EXPECT_EQ(rolls, 3);
	EXPECT_FALSE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
	EXPECT_EQ(player._pMana.whole(), 1000 - cost);

	// Without the mana the spell doesn't go off, and the Energy still empties.
	Missiles.clear();
	player._pMana = player._pManaBase = Fixed26_6::fromInt(0);
	for (int i = 0; i < 3; i++)
		OnMetaDodge(player, 2);
	EXPECT_TRUE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);

	// Switched off, it gains no Energy.
	player.poe2.spiritGemsOn = 0;
	OnMetaDodge(player, 2);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
}

TEST_F(Poe2MetaGemsTest, HigherGemLevelsGainMoreEnergy)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = CastOnDodge;
	player.poe2.slotLevels[MetaRow] = 20;
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	OnMetaDodge(player, 1);
	EXPECT_EQ(MetaEnergy(player, MetaRow), CastOnDodgeEnergyPerTile * (100 + (19 * MetaEnergyPercentPerLevel)) / 100);
}

TEST_F(Poe2MetaGemsTest, TakingHitsFillsCastWhenDamageTaken)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::CastWhenDamageTaken);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	OnMetaDamageTaken(player, { 43, 40 });
	OnMetaDamageTaken(player, { 43, 40 });
	EXPECT_EQ(MetaEnergy(player, MetaRow), 2 * CastWhenDamageTakenEnergy);
	EXPECT_TRUE(Missiles.empty());
	OnMetaDamageTaken(player, { 43, 40 });
	EXPECT_FALSE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
}

TEST_F(Poe2MetaGemsTest, ChannellingFillsCastWhileChannellingAndStoppingDrainsIt)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::CastWhileChannelling);
	ASSERT_EQ(Inside(0, Fireball), MetaSocketResult::Socketed);
	player.poe2.volleyShotsLeft = 5;
	player.poe2.volleyTargetX = 43;
	player.poe2.volleyTargetY = 40;
	int ticks = 0;
	while (Missiles.empty() && ticks < 100) {
		ProcessMetaGems(player);
		ticks++;
	}
	// 60 Energy a second fills 80 in about 1.3 seconds.
	EXPECT_NEAR(ticks, 27, 1);
	EXPECT_FALSE(Missiles.empty());

	for (int i = 0; i < 20; i++)
		ProcessMetaGems(player);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 60);
	player.poe2.volleyShotsLeft = 0;
	for (int i = 0; i < 20; i++)
		ProcessMetaGems(player);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 35);
}

TEST_F(Poe2MetaGemsTest, OldTriggerSupportsBecomeMetaGems)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[0] = Firebolt;
	data.slotSupports[0][1] = SupportGem::CastOnDodge;
	data.skillSlots[1] = Fireball;
	data.slotSupports[1][0] = SupportGem::CastWhenHit;
	data.slotSupports[1][1] = SupportGem::CastOnDodge;
	MigrateTriggerSupports(*MyPlayer);

	EXPECT_EQ(data.slotSupports[0][1], SupportGem::None);
	EXPECT_EQ(data.slotSupports[1][0], SupportGem::None);
	EXPECT_EQ(data.slotSupports[1][1], SupportGem::None);
	// One gem each, in the free rows, switched off so they take no Spirit the hero was using.
	EXPECT_EQ(data.skillSlots[2], CastOnDodge);
	EXPECT_EQ(data.skillSlots[3], SkillSlot::ForPersistent(PersistentSkill::CastWhenDamageTaken));
	EXPECT_TRUE(data.skillSlots[4].isEmpty());
	EXPECT_FALSE(data.spiritGemOn(2));
	EXPECT_FALSE(data.spiritGemOn(3));
	// The spells stay where they were.
	EXPECT_EQ(data.skillSlots[0], Firebolt);
	EXPECT_EQ(data.skillSlots[1], Fireball);

	// A second load changes nothing.
	MigrateTriggerSupports(*MyPlayer);
	EXPECT_EQ(std::count(data.skillSlots.begin(), data.skillSlots.end(), CastOnDodge), 1);
}

TEST_F(Poe2MetaGemsTest, ReapersInvocationStoresMeleeKillsUntilYouUseIt)
{
	Player &player = *MyPlayer;
	const SkillSlot reaper = SkillSlot::ForPersistent(PersistentSkill::ReapersInvocation);
	player.poe2.skillSlots[MetaRow] = reaper;
	EXPECT_TRUE(IsInvocationGem(reaper));
	EXPECT_TRUE(SkillGoesOnTray(reaper));
	EXPECT_FALSE(SkillGoesOnTray(CastOnDodge));
	EXPECT_EQ(SpiritReservation(PersistentSkill::ReapersInvocation), 30);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);

	// Only melee kills count.
	OnMetaKill(player, MonsterOf(MonsterRarity::Normal), Firebolt);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
	OnMetaKill(player, MonsterOf(MonsterRarity::Normal), cleave);
	EXPECT_EQ(MetaEnergy(player, MetaRow), ReaperEnergyNormal);
	OnMetaKill(player, MonsterOf(MonsterRarity::Magic), cleave);
	EXPECT_EQ(MetaEnergy(player, MetaRow), ReaperEnergyNormal + ReaperEnergyMagic);
	// It stores the Energy rather than casting at full.
	EXPECT_TRUE(Missiles.empty());
	OnMetaKill(player, MonsterOf(MonsterRarity::Rare), cleave);
	OnMetaKill(player, MonsterOf(MonsterRarity::Rare), cleave);
	OnMetaKill(player, MonsterOf(MonsterRarity::Rare), cleave);
	EXPECT_EQ(MetaEnergy(player, MetaRow), ReaperMaxEnergy);
	EXPECT_TRUE(Missiles.empty());

	// Using it casts the spells inside and spends one cast's Energy.
	EXPECT_EQ(InvokeMetaGem(player, MetaRow, { 43, 40 }), InvokeResult::Cast);
	EXPECT_FALSE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), ReaperMaxEnergy - MetaEnergyPerSpell);
	player.poe2.supportState.metaEnergy[MetaRow] = (MetaEnergyPerSpell - 1) * 100;
	EXPECT_EQ(InvokeMetaGem(player, MetaRow, { 43, 40 }), InvokeResult::NotEnoughEnergy);
	leveltype = DTYPE_TOWN;
	player.poe2.supportState.metaEnergy[MetaRow] = ReaperMaxEnergy * 100;
	EXPECT_EQ(InvokeMetaGem(player, MetaRow, { 43, 40 }), InvokeResult::Town);
	EXPECT_EQ(MetaEnergy(player, MetaRow), ReaperMaxEnergy);
	leveltype = DTYPE_CATHEDRAL;
	EXPECT_EQ(InvokeMetaGem(player, 0, { 43, 40 }), InvokeResult::Off);
	UnsocketMetaSkill(player.poe2, MetaRow, 0);
	EXPECT_EQ(InvokeMetaGem(player, MetaRow, { 43, 40 }), InvokeResult::NoSpells);
}

TEST_F(Poe2MetaGemsTest, MirageArcherNeedsABowAttack)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::MirageArcher);
	EXPECT_FALSE(IsMetaGem(PersistentSkill::MirageArcher));
	EXPECT_EQ(SpiritReservation(PersistentSkill::MirageArcher), 60);
	// No bow attack in the Skills panel: no mirage.
	OnSupportedDodge(player, 3);
	EXPECT_EQ(MirageCount(), 0);
	EXPECT_TRUE(MirageArcherAttack(player).isEmpty());

	// With a bow and a bow attack, a roll leaves a mirage, and the next roll has to wait 10 seconds.
	const SkillSlot barrage = SkillSlot::ForAttack(WeaponSkill::Barrage);
	player.poe2.skillSlots[0] = barrage;
	EXPECT_TRUE(MirageArcherAttack(player).isEmpty());
	InitializeItem(player.InvBody[INVLOC_HAND_LEFT], IDI_ROGUE);
	ASSERT_EQ(MirageArcherAttack(player), barrage);
	OnSupportedDodge(player, 3);
	EXPECT_EQ(MirageCount(), 1);
	EXPECT_EQ(player.poe2.supportState.mirageCooldown, 10 * 20);
	ClearSupportEffects();
	OnSupportedDodge(player, 3);
	EXPECT_EQ(MirageCount(), 0);
	for (int tick = 0; tick < 10 * 20; tick++)
		ProcessSupportEffects(player);
	OnSupportedDodge(player, 3);
	EXPECT_EQ(MirageCount(), 1);
	// Switched off, it leaves none.
	ClearSupportEffects();
	player.poe2.supportState.mirageCooldown = 0;
	player.poe2.spiritGemsOn = 0;
	OnSupportedDodge(player, 3);
	EXPECT_EQ(MirageCount(), 0);
}

TEST_F(Poe2MetaGemsTest, CastOnKillAndDecoyBecomeTheirSpiritGems)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[0] = Firebolt;
	data.slotSupports[0][0] = SupportGem::CastOnKill;
	data.skillSlots[1] = SkillSlot::ForAttack(WeaponSkill::Cleave);
	data.slotSupports[1][1] = SupportGem::Decoy;
	MigrateTriggerSupports(*MyPlayer);
	EXPECT_EQ(data.slotSupports[0][0], SupportGem::None);
	EXPECT_EQ(data.slotSupports[1][1], SupportGem::None);
	EXPECT_EQ(data.skillSlots[2], SkillSlot::ForPersistent(PersistentSkill::ReapersInvocation));
	EXPECT_EQ(data.skillSlots[3], SkillSlot::ForPersistent(PersistentSkill::MirageArcher));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::CastOnKill, Firebolt));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Decoy, SkillSlot::ForAttack(WeaponSkill::Barrage)));
}

TEST_F(Poe2MetaGemsTest, VizjereiFocusReservesAsMuchAsArchmage)
{
	EXPECT_EQ(SpiritReservation(PersistentSkill::VizjereiFocus), 100);
}

TEST_F(Poe2MetaGemsTest, BlasphemyCursesTheEnemiesAroundYouAtHalfStrength)
{
	Player &player = *MyPlayer;
	const SkillSlot weaken = SkillSlot::ForAttack(WeaponSkill::Weaken);
	const SkillSlot amplify = SkillSlot::ForAttack(WeaponSkill::AmplifyDamage);
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::Blasphemy);
	// It takes curses, but not spells or curses that take control of an enemy.
	EXPECT_EQ(Inside(0, Firebolt), MetaSocketResult::NotTriggerable);
	EXPECT_EQ(Inside(0, SkillSlot::ForAttack(WeaponSkill::Confuse)), MetaSocketResult::NotTriggerable);
	EXPECT_EQ(Inside(0, SkillSlot::ForAttack(WeaponSkill::Haunt)), MetaSocketResult::NotTriggerable);
	ASSERT_EQ(Inside(0, weaken), MetaSocketResult::Socketed);
	ASSERT_EQ(Inside(1, amplify), MetaSocketResult::Socketed);
	// 60 Spirit for each curse: two need more than the 100 there is.
	EXPECT_EQ(BlasphemyCurseCount(player), 2);
	EXPECT_EQ(GemSpiritCost(player, PersistentSkill::Blasphemy), 120);
	EXPECT_FALSE(IsPersistentSkillActive(player, PersistentSkill::Blasphemy));
	UnsocketMetaSkill(player.poe2, MetaRow, 1);
	EXPECT_EQ(GemSpiritCost(player, PersistentSkill::Blasphemy), 60);
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::Blasphemy));
	EXPECT_FALSE(UsesMetaEnergy(SkillSlot::ForPersistent(PersistentSkill::Blasphemy)));

	Monster &monster = PlaceMonster({ 42, 40 });
	ProcessMetaGems(player);
	EXPECT_EQ(monster.poe2.weakenTicks, BlasphemyCurseTicks);
	EXPECT_EQ(TableCurseStrength(monster, TableCurse::Weaken), BlasphemyStrength(1));
	EXPECT_EQ(BlasphemyStrength(1), 50);
	EXPECT_EQ(BlasphemyStrength(20), 59);
	// Weaken at half strength: 15% softer hits rather than 30%.
	EXPECT_EQ(ApplyTableSkillDefences(monster, player, Fixed26_6::fromInt(100)).whole(), 85);

	// Cast by hand, the curse is at full strength, and the aura leaves it be while it lasts longer.
	ApplyTableCurse(player, monster, WeaponSkill::Weaken, 100);
	EXPECT_EQ(TableCurseStrength(monster, TableCurse::Weaken), 100);
	const uint8_t handTicks = monster.poe2.weakenTicks;
	for (int tick = 0; tick < BlasphemyPulseTicks; tick++)
		ProcessMetaGems(player);
	EXPECT_EQ(monster.poe2.weakenTicks, handTicks);
	EXPECT_EQ(TableCurseStrength(monster, TableCurse::Weaken), 100);

	// An enemy out of reach isn't cursed.
	Monster &far = PlaceMonster({ 40 + BlasphemyRadius + 2, 40 });
	for (int tick = 0; tick < BlasphemyPulseTicks; tick++)
		ProcessMetaGems(player);
	EXPECT_EQ(far.poe2.weakenTicks, 0);
}

TEST_F(Poe2MetaGemsTest, HoradricTotemPlacesTotemsForMana)
{
	Player &player = *MyPlayer;
	const SkillSlot totem = SkillSlot::ForPersistent(PersistentSkill::HoradricTotem);
	player.poe2.skillSlots[MetaRow] = totem;
	EXPECT_TRUE(SkillGoesOnTray(totem));
	EXPECT_FALSE(IsSpiritGemSwitchable(PersistentSkill::HoradricTotem));
	EXPECT_FALSE(ToggleSpiritGem(player, MetaRow));
	EXPECT_EQ(GemSpiritCost(player, PersistentSkill::HoradricTotem), 0);
	EXPECT_EQ(PlaceHoradricTotem(player, MetaRow, { 43, 40 }), InvokeResult::NoSpells);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);

	EXPECT_EQ(PlaceHoradricTotem(player, MetaRow, { 43, 40 }), InvokeResult::Cast);
	EXPECT_EQ(player._pMana.whole(), 1000 - HoradricTotemManaCost(1));
	EXPECT_EQ(HoradricTotemCount(), 1);
	// One totem at gem level 1, two from gem level 14.
	EXPECT_EQ(PlaceHoradricTotem(player, MetaRow, { 43, 41 }), InvokeResult::Cast);
	EXPECT_EQ(HoradricTotemCount(), 1);
	player.poe2.slotLevels[MetaRow] = 14;
	EXPECT_EQ(PlaceHoradricTotem(player, MetaRow, { 43, 42 }), InvokeResult::Cast);
	EXPECT_EQ(HoradricTotemCount(), 2);
	EXPECT_EQ(HoradricTotemManaCost(14), 74);

	// The totem casts at the nearest monster, for no mana.
	PlaceMonster({ 46, 40 });
	const Fixed26_6 mana = player._pMana;
	for (int tick = 0; tick < 12; tick++)
		ProcessSupportEffects(player);
	EXPECT_FALSE(Missiles.empty());
	EXPECT_EQ(player._pMana, mana);

	// It goes when its spells come out.
	UnsocketMetaSkill(player.poe2, MetaRow, 0);
	ProcessSupportEffects(player);
	EXPECT_EQ(HoradricTotemCount(), 0);

	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	player._pMana = Fixed26_6::fromInt(10);
	EXPECT_EQ(PlaceHoradricTotem(player, MetaRow, { 43, 40 }), InvokeResult::NoMana);
	player._pMana = Fixed26_6::fromInt(1000);
	leveltype = DTYPE_TOWN;
	EXPECT_EQ(PlaceHoradricTotem(player, MetaRow, { 43, 40 }), InvokeResult::Town);
}

TEST_F(Poe2MetaGemsTest, CastOnCriticalFillsMoreFromHarderHitsOnStrongerMonsters)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::CastOnCritical);
	EXPECT_EQ(SpiritReservation(PersistentSkill::CastOnCritical), 100);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	Monster normal = MonsterOf(MonsterRarity::Normal);
	normal.maxHitPoints = normal.hitPoints = Fixed26_6::fromInt(100);
	// Half its life: half of 100 Energy.
	OnMetaCrit(player, normal, Fixed26_6::fromInt(50));
	EXPECT_EQ(MetaEnergy(player, MetaRow), 50);
	// A scratch still counts a little.
	OnMetaCrit(player, normal, Fixed26_6::fromInt(1));
	EXPECT_EQ(MetaEnergy(player, MetaRow), 55);
	EXPECT_TRUE(Missiles.empty());
	// A rare is worth 5 times as much: 5% of its life fills the rest, and the spell casts.
	Monster rare = MonsterOf(MonsterRarity::Rare);
	rare.maxHitPoints = rare.hitPoints = Fixed26_6::fromInt(100);
	OnMetaCrit(player, rare, Fixed26_6::fromInt(5));
	EXPECT_FALSE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
	EXPECT_EQ(MonsterPower(rare), 5);
}

TEST_F(Poe2MetaGemsTest, ElementalAilmentsFillTheirMetaGems)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::CastOnElementalAilment);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	const Monster normal = MonsterOf(MonsterRarity::Normal);
	OnMetaAilment(player, normal, MetaAilment::Shock);
	EXPECT_EQ(MetaEnergy(player, MetaRow), ShockEnergyPerPower);
	OnMetaAilment(player, normal, MetaAilment::Ignite);
	EXPECT_EQ(MetaEnergy(player, MetaRow), ShockEnergyPerPower + IgniteEnergyPerPower);
	// A freeze counts ten times as much; two fill a one-spell gem.
	OnMetaAilment(player, normal, MetaAilment::Freeze);
	EXPECT_TRUE(Missiles.empty());
	OnMetaAilment(player, normal, MetaAilment::Freeze);
	EXPECT_FALSE(Missiles.empty());

	// Elemental Invocation stores the same Energy until you use it.
	Missiles.clear();
	UnsocketMetaSkill(player.poe2, MetaRow, 0);
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::ElementalInvocation);
	EXPECT_TRUE(IsInvocationGem(player.poe2.skillSlots[MetaRow]));
	ASSERT_EQ(Inside(0, Fireball), MetaSocketResult::Socketed);
	for (int i = 0; i < 20; i++)
		OnMetaAilment(player, normal, MetaAilment::Freeze);
	EXPECT_EQ(MetaEnergy(player, MetaRow), InvocationMaxEnergy);
	EXPECT_TRUE(Missiles.empty());
	EXPECT_EQ(InvokeMetaGem(player, MetaRow, { 43, 40 }), InvokeResult::Cast);
	EXPECT_FALSE(Missiles.empty());
}

TEST_F(Poe2MetaGemsTest, MinionDeathsFillCastOnMinionDeath)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::CastOnMinionDeath);
	EXPECT_EQ(SpiritReservation(PersistentSkill::CastOnMinionDeath), 30);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	const Monster minion = MonsterOf(MonsterRarity::Normal);
	OnMetaMinionDied(player, minion);
	EXPECT_EQ(MetaEnergy(player, MetaRow), MinionDeathEnergy);
	EXPECT_TRUE(Missiles.empty());
	OnMetaMinionDied(player, minion);
	EXPECT_FALSE(Missiles.empty());
}

TEST_F(Poe2MetaGemsTest, BarrierInvocationStoresEnergyShieldLostToHits)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::BarrierInvocation);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	player.poe2.maxEnergyShield = 500;
	player.poe2.energyShield = 500 * 64;
	// 200 of the hit lands on energy shield: 20 Energy.
	AbsorbWithEnergyShield(player, Fixed26_6::fromInt(200));
	EXPECT_EQ(MetaEnergy(player, MetaRow), 200 / BarrierShieldPerEnergy);
	OnMetaShieldLost(player, Fixed26_6::fromInt(10000));
	EXPECT_EQ(MetaEnergy(player, MetaRow), InvocationMaxEnergy);
	EXPECT_TRUE(Missiles.empty());
	EXPECT_EQ(InvokeMetaGem(player, MetaRow, { 43, 40 }), InvokeResult::Cast);
	EXPECT_FALSE(Missiles.empty());
}

TEST_F(Poe2MetaGemsTest, CurseOnHitAndHoradricTotemSupportsBecomeSpiritGems)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[0] = Firebolt;
	data.slotSupports[0][0] = SupportGem::HoradricTotem;
	data.skillSlots[1] = SkillSlot::ForAttack(WeaponSkill::Cleave);
	data.slotSupports[1][1] = SupportGem::CurseOnHit;
	MigrateTriggerSupports(*MyPlayer);
	EXPECT_EQ(data.slotSupports[0][0], SupportGem::None);
	EXPECT_EQ(data.slotSupports[1][1], SupportGem::None);
	EXPECT_EQ(data.skillSlots[2], SkillSlot::ForPersistent(PersistentSkill::HoradricTotem));
	EXPECT_EQ(data.skillSlots[3], SkillSlot::ForPersistent(PersistentSkill::Blasphemy));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::HoradricTotem, Firebolt));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::CurseOnHit, SkillSlot::ForAttack(WeaponSkill::Cleave)));
}

} // namespace
} // namespace devilution

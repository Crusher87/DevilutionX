#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/currency.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SupportGemsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	/** @brief A hero with plenty of every attribute, so support capacity never gets in the way. */
	static Player &FreshPlayer()
	{
		Player &player = *MyPlayer;
		player = {};
		player._pStrength = 100;
		player._pMagic = 100;
		player._pDexterity = 100;
		return player;
	}

	/** @brief Puts the skill in the skill bar if it isn't there yet and sockets the support into it. */
	static SupportSocketResult Socket(Player &player, SkillSlot skill, SupportGem gem)
	{
		std::optional<size_t> slot = FindSkillSlot(player, skill);
		if (!slot) {
			slot = FirstFreeSkillSlot(player);
			player.poe2.skillSlots[*slot] = skill;
		}
		SupportGem replaced;
		return SocketSupportGem(player, *slot, std::nullopt, gem, replaced);
	}

	static SupportSocketResult Socket(Player &player, SpellID spell, SupportGem gem)
	{
		return Socket(player, SkillSlot::ForSpell(spell), gem);
	}
};

TEST_F(Poe2SupportGemsTest, EveryGemHasAnItem)
{
	for (uint8_t i = 1; i < static_cast<uint8_t>(SupportGem::Count); i++) {
		const auto gem = static_cast<SupportGem>(i);
		Item item;
		ASSERT_TRUE(CreateSupportGemItem(item, gem)) << "no item for gem " << static_cast<int>(i);
		EXPECT_EQ(SupportGemForItem(item), gem);
		EXPECT_FALSE(SupportGemName(gem).empty());
		EXPECT_FALSE(SupportGemDescription(gem).empty());

		// Gems are stored in the hero file by index and seed, so they must rebuild the same.
		Item recreated;
		RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(SupportGemForItem(recreated), gem);
		EXPECT_EQ(std::string(recreated._iName), std::string(item._iName));
	}
}

TEST_F(Poe2SupportGemsTest, TiersFollowPoe2)
{
	// Every tier is listed once, in order, and cut from a higher uncut gem than the one before.
	for (const SupportTierData &data : SupportTierTable()) {
		EXPECT_EQ(FindSupportTier(data.gem, data.tier), &data) << SupportGemName(data.gem);
		EXPECT_FALSE(data.description.empty());
		EXPECT_LE(data.tier, SupportTierCount(data.gem));
		if (data.tier > 1)
			EXPECT_GE(data.uncut, FindSupportTier(data.gem, data.tier - 1)->uncut) << SupportGemName(data.gem);
	}
	EXPECT_EQ(SupportTierCount(SupportGem::Chain), 3);
	EXPECT_EQ(SupportTierCount(SupportGem::Empower), 1);
	// Chain III: cut from a level 5 uncut support gem, our gemcutting row IX.
	EXPECT_EQ(SupportTierGemLevel(SupportGem::Chain, 3, 1), 15);
	EXPECT_EQ(UncutSupportLevelForGemLevel(14), 4);
	EXPECT_EQ(UncutSupportLevelForGemLevel(15), 5);
	EXPECT_EQ(SupportTierGemLevel(SupportGem::Empower, 1, 5), 5);
}

TEST_F(Poe2SupportGemsTest, HigherTiersChangeTheNumbers)
{
	Player &player = FreshPlayer();
	Socket(player, SpellID::Firebolt, SupportGem::Chain);
	const size_t slot = *FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Firebolt));
	Missile missile {};
	ApplySupportsToMissile(missile, player, SpellID::Firebolt);
	EXPECT_EQ(missile.poe2.chainsLeft, 1);
	EXPECT_EQ(SupportSpellDamagePercent(player, SpellID::Firebolt), 70);

	// Chain II in its place: 2 chains, 50% less damage.
	SupportGem replaced;
	int replacedTier = 0;
	EXPECT_EQ(SocketSupportGem(player, slot, 0, SupportGem::Chain, replaced, 2, &replacedTier), SupportSocketResult::Replaced);
	EXPECT_EQ(replaced, SupportGem::Chain);
	EXPECT_EQ(replacedTier, 1);
	EXPECT_EQ(player.poe2.supportTier(SkillSlot::ForSpell(SpellID::Firebolt), SupportGem::Chain), 2);
	Missile chained {};
	ApplySupportsToMissile(chained, player, SpellID::Firebolt);
	EXPECT_EQ(chained.poe2.chainsLeft, 2);
	EXPECT_EQ(SupportSpellDamagePercent(player, SpellID::Firebolt), 50);
	EXPECT_EQ(SocketSupportGem(player, slot, 0, SupportGem::Chain, replaced, 2), SupportSocketResult::AlreadySocketed);

	// A support without tiers stays tier I.
	EXPECT_EQ(SocketSupportGem(player, slot, 1, SupportGem::Empower, replaced, 3), SupportSocketResult::Socketed);
	EXPECT_EQ(player.poe2.supportTier(SkillSlot::ForSpell(SpellID::Firebolt), SupportGem::Empower), 1);

	int tier = 0;
	EXPECT_EQ(UnsocketSupportGem(player.poe2, slot, 0, &tier), SupportGem::Chain);
	EXPECT_EQ(tier, 2);
}

TEST_F(Poe2SupportGemsTest, TierItemsKeepTheirTier)
{
	Item item;
	ASSERT_TRUE(CreateSupportGemItem(item, SupportGem::Brutality, 3));
	EXPECT_EQ(SupportGemItemTier(item), 3);
	EXPECT_EQ(std::string(item._iName), "Brutality III Support");
	EXPECT_EQ(SupportGemDescription(SupportGem::Brutality, 3), FindSupportTier(SupportGem::Brutality, 3)->description);

	// Saved by index, seed and buffs, it comes back as the same tier.
	Item recreated;
	RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
	EXPECT_EQ(SupportGemItemTier(recreated), 3);
	EXPECT_EQ(std::string(recreated._iName), "Brutality III Support");

	// Tiers above a support's highest don't exist.
	ASSERT_TRUE(CreateSupportGemItem(item, SupportGem::Empower, 3));
	EXPECT_EQ(SupportGemItemTier(item), 1);
	EXPECT_EQ(std::string(item._iName), "Empower Support");
}

TEST_F(Poe2SupportGemsTest, SocketingRules)
{
	Player &player = FreshPlayer();
	SupportGem replaced;

	EXPECT_EQ(SocketSupportGem(player, 0, std::nullopt, SupportGem::Empower, replaced), SupportSocketResult::NoSkill);
	EXPECT_EQ(Socket(player, SpellID::Healing, SupportGem::MultipleProjectiles), SupportSocketResult::NotApplicable);

	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::MultipleProjectiles), SupportSocketResult::Socketed);
	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::MultipleProjectiles), SupportSocketResult::AlreadySocketed);
	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::Empower), SupportSocketResult::Socketed);
	EXPECT_TRUE(player.poe2.hasSupport(SpellID::Firebolt, SupportGem::Empower));
	EXPECT_FALSE(player.poe2.hasSupport(SpellID::Fireball, SupportGem::Empower));

	// A skill starts with two open sockets.
	const size_t slot = *FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Firebolt));
	EXPECT_EQ(SupportSocketCount(player, slot), 2U);
	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::FasterCasting), SupportSocketResult::NoFreeSocket);
	EXPECT_EQ(SocketSupportGem(player, slot, 2, SupportGem::FasterCasting, replaced), SupportSocketResult::Locked);

	// Swapping a gem into a full socket hands back the old one.
	EXPECT_EQ(SocketSupportGem(player, slot, 0, SupportGem::FasterCasting, replaced), SupportSocketResult::Replaced);
	EXPECT_EQ(replaced, SupportGem::MultipleProjectiles);
	EXPECT_FALSE(player.poe2.hasSupport(SpellID::Firebolt, SupportGem::MultipleProjectiles));
	EXPECT_TRUE(player.poe2.hasSupport(SpellID::Firebolt, SupportGem::FasterCasting));
	EXPECT_EQ(UnsocketSupportGem(player.poe2, slot, 0), SupportGem::FasterCasting);
	EXPECT_FALSE(player.poe2.hasSupport(SpellID::Firebolt, SupportGem::FasterCasting));
}

TEST_F(Poe2SupportGemsTest, OnePowerSupportPerSkill)
{
	Player &player = FreshPlayer();
	player.setCharacterLevel(30);
	player.poe2.openedSockets.fill(3); // every socket open, as Jeweller's Orbs would
	EXPECT_TRUE(IsPowerSupport(SupportGem::Empower));
	EXPECT_TRUE(IsPowerSupport(SupportGem::ConcentratedEffect));
	EXPECT_TRUE(IsPowerSupport(SupportGem::Brutality));
	EXPECT_FALSE(IsPowerSupport(SupportGem::Pierce));

	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::Empower), SupportSocketResult::Socketed);
	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::ConcentratedEffect), SupportSocketResult::PowerTaken);
	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::Pierce), SupportSocketResult::Socketed);
	// Another skill can still take one.
	EXPECT_EQ(Socket(player, SpellID::Fireball, SupportGem::ConcentratedEffect), SupportSocketResult::Socketed);
	// Swapping the Power support for another in its own socket is fine.
	const size_t slot = *FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Firebolt));
	SupportGem replaced;
	EXPECT_EQ(SocketSupportGem(player, slot, 0, SupportGem::ConcentratedEffect, replaced), SupportSocketResult::Replaced);
	EXPECT_EQ(replaced, SupportGem::Empower);
}

TEST_F(Poe2SupportGemsTest, JewellerOrbsOpenSocketsInOrder)
{
	Player &player = FreshPlayer();
	player.setCharacterLevel(50);
	Socket(player, SpellID::Firebolt, SupportGem::Empower);
	Socket(player, SpellID::Fireball, SupportGem::Pierce);
	const size_t slot = *FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Firebolt));
	EXPECT_EQ(SupportSocketCount(player, slot), 2U) << "Levels no longer open sockets";

	EXPECT_FALSE(ApplyJewellerOrb(CurrencyOrb::GreaterJeweller, player, slot)) << "The fourth socket needs the third first";
	EXPECT_TRUE(ApplyJewellerOrb(CurrencyOrb::LesserJeweller, player, slot));
	EXPECT_EQ(SupportSocketCount(player, slot), 3U);
	EXPECT_FALSE(ApplyJewellerOrb(CurrencyOrb::LesserJeweller, player, slot)) << "A Lesser opens only the third";
	EXPECT_TRUE(ApplyJewellerOrb(CurrencyOrb::GreaterJeweller, player, slot));
	EXPECT_TRUE(ApplyJewellerOrb(CurrencyOrb::PerfectJeweller, player, slot));
	EXPECT_EQ(SupportSocketCount(player, slot), MaxSupportSockets);
	EXPECT_FALSE(ApplyJewellerOrb(CurrencyOrb::PerfectJeweller, player, slot));

	// Each skill has its own sockets; an empty row takes no orb.
	const size_t other = *FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Fireball));
	EXPECT_EQ(SupportSocketCount(player, other), 2U);
	const std::optional<size_t> empty = FirstFreeSkillSlot(player);
	ASSERT_TRUE(empty.has_value());
	EXPECT_FALSE(ApplyJewellerOrb(CurrencyOrb::LesserJeweller, player, *empty));

	// Jeweller's Orbs are never used on items.
	Item ring {};
	EXPECT_FALSE(CurrencyFitsItem(CurrencyOrb::LesserJeweller, ring));
	EXPECT_EQ(LevelOpenedSockets(9), 0);
	EXPECT_EQ(LevelOpenedSockets(25), 2);
	EXPECT_EQ(LevelOpenedSockets(80), 3);
}

TEST_F(Poe2SupportGemsTest, AttributesLimitSupports)
{
	Player &player = FreshPlayer();
	player._pDexterity = 9;
	player._pMagic = 35;
	player.setCharacterLevel(30);
	player.poe2.openedSockets.fill(3); // every socket open, as Jeweller's Orbs would
	// One Dexterity support plus one per 5 points.
	EXPECT_EQ(SupportCapacity(player, GemColor::Dexterity), 2);
	EXPECT_EQ(SupportCapacity(player, GemColor::Magic), 8);

	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::Pierce), SupportSocketResult::Socketed);
	EXPECT_EQ(Socket(player, SpellID::Firebolt, SupportGem::Fork), SupportSocketResult::Socketed);
	EXPECT_EQ(SupportsUsed(player.poe2, GemColor::Dexterity), 2);
	EXPECT_EQ(Socket(player, SpellID::Fireball, SupportGem::FasterProjectiles), SupportSocketResult::NoCapacity);
	// Gems of other colors still fit.
	EXPECT_EQ(Socket(player, SpellID::Fireball, SupportGem::Empower), SupportSocketResult::Socketed);

	// Swapping a Dexterity gem for another doesn't need more capacity.
	const size_t slot = *FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Firebolt));
	SupportGem replaced;
	EXPECT_EQ(SocketSupportGem(player, slot, 1, SupportGem::FasterProjectiles, replaced), SupportSocketResult::Replaced);
	EXPECT_EQ(replaced, SupportGem::Fork);
}

TEST_F(Poe2SupportGemsTest, AttackSupports)
{
	Player &player = FreshPlayer();
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	const SkillSlot barrage = SkillSlot::ForAttack(WeaponSkill::Barrage);
	const SkillSlot herald = SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh);

	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Brutality, cleave));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Knockback, cleave));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::MartialTempo, cleave));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::MartialTempo, barrage));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Brutality, barrage));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Pierce, cleave));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Brutality, SpellID::Firebolt));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Efficiency, cleave));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Efficiency, herald));

	// With no skill it's the weapon's free default attack, which hits softer.
	EXPECT_EQ(SupportMeleeDamagePercent(player), DefaultAttackDamagePercent);
	EXPECT_EQ(Socket(player, cleave, SupportGem::Brutality), SupportSocketResult::Socketed);
	EXPECT_EQ(Socket(player, cleave, SupportGem::MartialTempo), SupportSocketResult::Socketed);
	// The supports apply while the skill's attack is in progress.
	EXPECT_EQ(SupportMeleeDamagePercent(player), DefaultAttackDamagePercent);
	player.poe2.activeSkill = WeaponSkill::Cleave;
	EXPECT_EQ(SupportMeleeDamagePercent(player), BrutalityDamagePercent);
	EXPECT_EQ(SupportAttackSkippedFrames(player, WeaponSkill::Cleave), MartialTempoFrames);
	EXPECT_FALSE(SupportMeleeKnocksBack(player));
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::Cleave), WeaponSkillManaCost(WeaponSkill::Cleave) * 120 / 100);
}

TEST_F(Poe2SupportGemsTest, TakingSupportsOut)
{
	Player &player = FreshPlayer();
	Socket(player, SpellID::Firebolt, SupportGem::Pierce);
	Socket(player, SpellID::Firebolt, SupportGem::Empower);
	const size_t slot = *FindSkillSlot(player, SkillSlot::ForSpell(SpellID::Firebolt));
	EXPECT_EQ(FindSlotForSupport(player, SupportGem::Fork), std::nullopt);
	const std::vector<TieredSupport> taken = TakeSlotSupports(player.poe2, slot);
	ASSERT_EQ(taken.size(), 2U);
	EXPECT_EQ(taken[0].gem, SupportGem::Pierce);
	EXPECT_EQ(taken[1].gem, SupportGem::Empower);
	EXPECT_EQ(FindSlotForSupport(player, SupportGem::Fork), slot);
	EXPECT_EQ(SupportManaCostPercent(player, SpellID::Firebolt), 100);
}

TEST_F(Poe2SupportGemsTest, SupportsChangeTheSpell)
{
	Player &player = FreshPlayer();
	player._pClass = HeroClass::Sorcerer;
	player._pSplLvl[static_cast<int8_t>(SpellID::Firebolt)] = 5;
	player._pSplLvl[static_cast<int8_t>(SpellID::Healing)] = 5;

	const Fixed26_6 baseMana = GetManaAmount(player, SpellID::Firebolt);
	EXPECT_EQ(SupportExtraProjectiles(player, SpellID::Firebolt), 0);
	EXPECT_EQ(SupportSpellLevelBonus(player, SpellID::Firebolt), 0);
	EXPECT_EQ(SupportCastSkippedFrames(player, SpellID::Firebolt), 0);

	Socket(player, SpellID::Firebolt, SupportGem::MultipleProjectiles);
	Socket(player, SpellID::Firebolt, SupportGem::Empower);
	EXPECT_EQ(SupportExtraProjectiles(player, SpellID::Firebolt), 2);
	EXPECT_EQ(SupportSpellLevelBonus(player, SpellID::Firebolt), 2);
	EXPECT_EQ(SupportManaCostPercent(player, SpellID::Firebolt), 140);
	EXPECT_EQ(GetManaAmount(player, SpellID::Firebolt).raw(), baseMana.raw() * 140 / 100);

	// Supports belong to one spell only.
	EXPECT_EQ(SupportManaCostPercent(player, SpellID::Healing), 100);

	Socket(player, SpellID::Healing, SupportGem::Efficiency);
	Socket(player, SpellID::Healing, SupportGem::FasterCasting);
	EXPECT_EQ(SupportManaCostPercent(player, SpellID::Healing), 70);
	EXPECT_EQ(SupportCastSkippedFrames(player, SpellID::Healing), 2);
	EXPECT_EQ(SupportExtraProjectiles(player, SpellID::Healing), 0);
}

TEST_F(Poe2SupportGemsTest, ProjectileSupportsOnlyFitBolts)
{
	for (const SupportGem gem : { SupportGem::Pierce, SupportGem::Fork, SupportGem::Chain, SupportGem::ConcentratedEffect, SupportGem::FasterProjectiles }) {
		EXPECT_TRUE(SupportGemCanSupport(gem, SpellID::Firebolt)) << SupportGemName(gem);
		EXPECT_TRUE(SupportGemCanSupport(gem, SpellID::Fireball)) << SupportGemName(gem);
		EXPECT_FALSE(SupportGemCanSupport(gem, SpellID::Healing)) << SupportGemName(gem);
		EXPECT_FALSE(SupportGemCanSupport(gem, SpellID::Lightning)) << SupportGemName(gem);
	}
}

TEST_F(Poe2SupportGemsTest, SupportsShapeTheProjectile)
{
	Player &player = FreshPlayer();
	Socket(player, SpellID::Firebolt, SupportGem::Pierce);
	Socket(player, SpellID::Firebolt, SupportGem::FasterProjectiles);

	Missile missile {};
	missile.position.velocity = { 100, -50 };
	ApplySupportsToMissile(missile, player, SpellID::Firebolt);
	EXPECT_EQ(missile.poe2.spell, SpellID::Firebolt);
	// Pierce I passes through one monster; Projectile Acceleration I flies 40% faster.
	EXPECT_EQ(missile.poe2.piercesLeft, 1);
	EXPECT_EQ(missile.poe2.chainsLeft, 0);
	EXPECT_EQ(missile.position.velocity, (Displacement { 140, -70 }));

	// Spells without supports leave their missiles alone.
	Missile plain {};
	plain.position.velocity = { 100, -50 };
	ApplySupportsToMissile(plain, player, SpellID::Fireball);
	EXPECT_EQ(plain.poe2.spell, SpellID::Invalid);
	EXPECT_EQ(plain.position.velocity, (Displacement { 100, -50 }));
}

TEST_F(Poe2SupportGemsTest, PiercingSkipsTheMonsterItHit)
{
	Player &player = FreshPlayer();
	Socket(player, SpellID::Firebolt, SupportGem::Pierce);
	Socket(player, SpellID::Firebolt, SupportGem::ConcentratedEffect);

	Missile missile {};
	ApplySupportsToMissile(missile, player, SpellID::Firebolt);
	int minDamage = 10;
	int maxDamage = 20;
	ScaleSupportedMissileDamage(missile, minDamage, maxDamage);
	EXPECT_EQ(minDamage, 13);
	EXPECT_EQ(maxDamage, 26);

	const Monster &first = Monsters[3];
	const Monster &second = Monsters[4];
	EXPECT_FALSE(SupportedMissileIgnoresMonster(missile, 3));
	EXPECT_TRUE(OnSupportedMissileHit(missile, first));
	EXPECT_TRUE(SupportedMissileIgnoresMonster(missile, 3));
	// Once it has pierced, it hits for 20% less, just once.
	EXPECT_EQ(missile.poe2.damagePercent, ConcentratedEffectDamagePercent * PierceDamagePercent / 100);
	// Out of pierces, the next hit stops the projectile.
	EXPECT_FALSE(OnSupportedMissileHit(missile, second));
}

TEST_F(Poe2SupportGemsTest, SupportsHaveTradeOffs)
{
	Player &player = FreshPlayer();
	EXPECT_EQ(SupportSpellDamagePercent(player, SpellID::Firebolt), 100);
	EXPECT_EQ(SupportCastRecoveryTicks(player, SpellID::Firebolt, 20), 0);

	// Multiple Projectiles: 35% less damage and 20% slower casts.
	Socket(player, SpellID::Firebolt, SupportGem::MultipleProjectiles);
	EXPECT_EQ(SupportSpellDamagePercent(player, SpellID::Firebolt), MultipleProjectilesDamagePercent);
	EXPECT_EQ(SupportCastRecoveryTicks(player, SpellID::Firebolt, 20), 5);
	// Empower slows the cast a little more.
	Socket(player, SpellID::Firebolt, SupportGem::Empower);
	EXPECT_EQ(SupportCastRecoveryTicks(player, SpellID::Firebolt, 20), 7);
	// Chain hits for 30% less on top.
	Socket(player, SpellID::ChargedBolt, SupportGem::MultipleProjectiles);
	Socket(player, SpellID::ChargedBolt, SupportGem::Chain);
	EXPECT_EQ(SupportSpellDamagePercent(player, SpellID::ChargedBolt), MultipleProjectilesDamagePercent * ChainDamagePercent / 100);

	// Lightning's bolt isn't a projectile that pierces, but Multiple Projectiles still costs it damage.
	Socket(player, SpellID::Lightning, SupportGem::MultipleProjectiles);
	Missile lightning {};
	ApplySupportsToMissile(lightning, player, SpellID::Lightning);
	EXPECT_EQ(lightning.poe2.spell, SpellID::Invalid);
	EXPECT_EQ(lightning.poe2.damagePercent, MultipleProjectilesDamagePercent);

	// Concentrated Effect: more damage, slower projectiles.
	Socket(player, SpellID::Fireball, SupportGem::ConcentratedEffect);
	Missile fireball {};
	fireball.position.velocity = { 100, -50 };
	ApplySupportsToMissile(fireball, player, SpellID::Fireball);
	EXPECT_EQ(fireball.poe2.damagePercent, ConcentratedEffectDamagePercent);
	EXPECT_EQ(fireball.poe2.speedPercent, ConcentratedEffectSpeedPercent);
	EXPECT_EQ(fireball.position.velocity, (Displacement { 70, -35 }));

	// Brutality: more melee damage, but none of the weapon's fire or lightning.
	EXPECT_EQ(Socket(player, SkillSlot::ForAttack(WeaponSkill::Cleave), SupportGem::Brutality), SupportSocketResult::Socketed);
	EXPECT_FALSE(SupportMeleeIsPhysicalOnly(player));
	player.poe2.activeSkill = WeaponSkill::Cleave;
	EXPECT_TRUE(SupportMeleeIsPhysicalOnly(player));
	EXPECT_EQ(SupportMeleeDamagePercent(player), BrutalityDamagePercent);
}

} // namespace
} // namespace devilution

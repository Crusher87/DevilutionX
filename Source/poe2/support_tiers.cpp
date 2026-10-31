#include "poe2/support_tiers.hpp"

#include <algorithm>

namespace devilution {

namespace {

using SG = SupportGem;

/*
 * The tiers, from POE2 0.5.4's support gem tiers (poe2db and the POE2 wiki), matched to our supports by what they do.
 * The uncut level and the mana multiplier are POE2's. Where a support works differently in Diablo (leech heals at once,
 * Hellfire burns instead of setting minions alight), tiers II and III grow by as much as POE2's do.
 * Bleed I and II and Poison I and III have no published numbers; theirs are estimates between the known tiers.
 */
constexpr SupportTierData Tiers[] = {
	// Multishot I-II: extra projectiles, less damage, slower casts.
	{ SG::MultipleProjectiles, 1, 1, 100, { 2, 35, 20 }, "Fires 2 extra projectiles. 35% less\ndamage, 20% slower casts" },
	{ SG::MultipleProjectiles, 2, 2, 100, { 2, 25, 20 }, "Fires 2 extra projectiles. 25% less\ndamage, 20% slower casts" },
	// Rapid Casting I-III: cast animation frames skipped, and frames per different spell cast lately.
	{ SG::FasterCasting, 1, 1, 100, { 2, 0, 0 }, "Casts 15% faster" },
	{ SG::FasterCasting, 2, 4, 100, { 3, 0, 0 }, "Casts 20% faster" },
	{ SG::FasterCasting, 3, 5, 100, { 0, 1, 0 }, "Casts 10% faster for each different\nspell cast in the last 8 seconds,\nup to 4" },
	// Efficiency I-II: less mana cost.
	{ SG::Efficiency, 1, 1, 100, { 30, 0, 0 }, "30% less mana cost" },
	{ SG::Efficiency, 2, 4, 100, { 40, 0, 0 }, "40% less mana cost" },
	// Pierce I-III: monsters pierced, less damage once it has pierced, less damage for each one pierced.
	{ SG::Pierce, 1, 1, 100, { 1, 20, 0 }, "Bolts pass through a monster, then\ndeal 20% less damage" },
	{ SG::Pierce, 2, 3, 100, { 1, 0, 0 }, "Bolts pass through a monster" },
	{ SG::Pierce, 3, 5, 100, { 99, 0, 20 }, "Bolts pass through every monster,\n20% less damage for each one" },
	{ SG::Fork, 1, 3, 110, {}, "Bolts split in two on hit; forks\ndeal 30% less damage. Mana +10%" },
	// Chain I-III: jumps, less damage.
	{ SG::Chain, 1, 1, 100, { 1, 30, 0 }, "Bolts jump to 1 more monster.\n30% less damage" },
	{ SG::Chain, 2, 2, 100, { 2, 50, 0 }, "Bolts jump to 2 more monsters.\n50% less damage" },
	{ SG::Chain, 3, 5, 100, { 2, 40, 0 }, "Bolts jump to 2 more monsters.\n40% less damage" },
	{ SG::ConcentratedEffect, 1, 1, 100, {}, "Power: bolts deal 30% more damage\nbut fly 30% slower" },
	// Projectile Acceleration I-III: more speed, chance to pierce, more damage.
	{ SG::FasterProjectiles, 1, 1, 120, { 40, 0, 0 }, "Bolts fly 40% faster. Mana +20%" },
	{ SG::FasterProjectiles, 2, 4, 120, { 40, 40, 0 }, "Bolts fly 40% faster, with a 40%\nchance to pierce a monster. Mana +20%" },
	{ SG::FasterProjectiles, 3, 5, 120, { 40, 0, 40 }, "Bolts fly 40% faster and deal 40%\nmore damage. Mana +20%" },
	// Brutality I-III: more physical damage, chance for a hit to ignore armour.
	{ SG::Brutality, 1, 1, 120, { 25, 0, 0 }, "Power: 25% more melee damage, but no\nfire or lightning from the weapon. Mana +20%" },
	{ SG::Brutality, 2, 2, 120, { 30, 0, 0 }, "Power: 30% more melee damage, but no\nfire or lightning from the weapon. Mana +20%" },
	{ SG::Brutality, 3, 5, 120, { 30, 20, 0 }, "Power: 30% more melee damage, but no\nfire or lightning from the weapon; 20% of\nhits ignore armour. Mana +20%" },
	// Rapid Attacks I-III: attack animation frames skipped, less damage.
	{ SG::MartialTempo, 1, 1, 100, { 2, 0, 0 }, "Attacks 15% faster" },
	{ SG::MartialTempo, 2, 4, 100, { 3, 0, 0 }, "Attacks 25% faster" },
	{ SG::MartialTempo, 3, 5, 100, { 4, 50, 0 }, "Attacks 35% faster, but deals 50%\nless damage" },
	{ SG::Knockback, 1, 3, 100, {}, "Melee attack skills knock enemies\nback" },
	// Rage I-III: Rage per melee hit, frames skipped while not at full Rage.
	{ SG::BattleRage, 1, 1, 100, { 3, 0, 0 }, "Melee hits build 3 Rage (up to 30):\n1% more attack damage each" },
	{ SG::BattleRage, 2, 4, 100, { 5, 0, 0 }, "Melee hits build 5 Rage (up to 30):\n1% more attack damage each" },
	{ SG::BattleRage, 3, 5, 100, { 5, 2, 0 }, "Melee hits build 5 Rage (up to 30):\n1% more attack damage each. 15% faster\nattacks while not at full Rage" },
	// Magnified Area I-II.
	{ SG::IncreasedArea, 1, 1, 130, { 35, 0, 0 }, "35% larger area. Mana +30%" },
	{ SG::IncreasedArea, 2, 3, 130, { 45, 0, 0 }, "45% larger area. Mana +30%" },
	// Prolonged Duration I-II.
	{ SG::IncreasedDuration, 1, 1, 120, { 30, 0, 0 }, "Lasts 30% longer. Mana +20%" },
	{ SG::IncreasedDuration, 2, 3, 120, { 35, 0, 0 }, "Lasts 35% longer. Mana +20%" },
	{ SG::Volley, 1, 4, 120, {}, "2 extra projectiles side by side.\n25% less damage. Mana +20%" },
	{ SG::SpellEcho, 1, 2, 150, {}, "Each cast repeats once. 30% slower\ncasts. Mana +50%" },
	// Aftershock I-II: chance for the slam to strike again.
	{ SG::Aftershock, 1, 2, 100, { 25, 0, 0 }, "Slams have a 25% chance to strike\nagain where they landed" },
	{ SG::Aftershock, 2, 4, 100, { 35, 0, 0 }, "Slams have a 35% chance to strike\nagain where they landed" },
	// Close Combat I-II: most damage next to the monster.
	{ SG::CloseCombat, 1, 3, 120, { 20, 0, 0 }, "Power: up to 20% more damage the closer\nthe monster, none at 4 tiles. Mana +20%" },
	{ SG::CloseCombat, 2, 5, 120, { 30, 0, 0 }, "Power: up to 30% more damage the closer\nthe monster, none at 4 tiles. Mana +20%" },
	{ SG::InfusedFlames, 1, 1, 100, {}, "Half of physical damage becomes fire" },
	{ SG::InfusedStorm, 1, 1, 100, {}, "Half of physical damage becomes\nlightning" },
	{ SG::InfusedFrost, 1, 1, 100, {}, "Half of physical damage becomes cold" },
	// Persistent Ground I-III: burning pulses and radius.
	{ SG::BurningGround, 1, 2, 100, { 4, 1, 0 }, "Hits leave burning ground. 20% less\ndamage with hits" },
	{ SG::BurningGround, 2, 3, 100, { 6, 1, 0 }, "Hits leave burning ground that lasts\n50% longer. 20% less damage with hits" },
	{ SG::BurningGround, 3, 5, 100, { 6, 2, 0 }, "Hits leave wide burning ground that\nlasts 50% longer. 20% less damage\nwith hits" },
	{ SG::Overcharge, 1, 4, 100, {}, "Shocks twice as often. 25% less\ndamage with hits" },
	{ SG::DeepFreeze, 1, 1, 120, {}, "Freezes sooner. 20% less damage\nwith hits. Mana +20%" },
	{ SG::WitheringTouch, 1, 1, 100, {}, "Hits wither: 5% more chaos damage\ntaken a stack, up to 10. 15% less damage" },
	{ SG::EchoingShout, 1, 4, 150, {}, "The warcry repeats twice, each echo\n50% weaker. Mana +50%" },
	// Feeding Frenzy I-II: more minion damage, more damage taken.
	{ SG::FeedingFrenzy, 1, 3, 100, { 30, 20, 0 }, "Minions deal 30% more damage, but\ntake 20% more" },
	{ SG::FeedingFrenzy, 2, 4, 100, { 30, 15, 0 }, "Minions deal 30% more damage, but\ntake 15% more" },
	// Minion Pact I-II: the explosion, in percent of the minion's life it takes.
	{ SG::GravePact, 1, 1, 100, { 100, 0, 0 }, "Dying minions explode. Minions have\n20% less life" },
	{ SG::GravePact, 2, 4, 100, { 150, 0, 0 }, "Dying minions explode 50% harder.\nMinions have 20% less life" },
	{ SG::CurseMastery, 1, 1, 140, {}, "Larger, longer curses. Mana +40%" },
	{ SG::Sanctify, 1, 5, 100, {}, "Each heal pulses holy fire around\nyou" },
	// Life Leech I-III: life per 100 damage, and leech kept while at full life.
	{ SG::LifeLeech, 1, 2, 100, { 3, 0, 0 }, "Leeches 3% of damage as life. 10% less\ndamage" },
	{ SG::LifeLeech, 2, 3, 100, { 4, 0, 0 }, "Leeches 4% of damage as life. 10% less\ndamage" },
	{ SG::LifeLeech, 3, 5, 100, { 4, 1, 0 }, "Leeches 4% of damage as life, kept for\n2 seconds at full life. 10% less damage" },
	{ SG::ManaLeech, 1, 2, 100, {}, "Leeches 2% of damage as mana. 10% less\ndamage" },
	{ SG::BloodPrice, 1, 2, 100, {}, "Costs life instead of mana, 20% more" },
	{ SG::ElementalFocus, 1, 3, 100, {}, "Power: 25% more elemental damage, but\nno ailments" },
	// Infernal Legion I-II: how much harder ignites burn.
	{ SG::Hellfire, 1, 2, 100, { 75, 0, 0 }, "Ignites burn 75% harder. 25% less\ndamage with hits" },
	{ SG::Hellfire, 2, 5, 100, { 100, 0, 0 }, "Ignites burn twice as hard. 25% less\ndamage with hits" },
	// Fan the Flames I-II: extra fire against burning monsters, and the burn spreading.
	{ SG::FuelThePyre, 1, 2, 100, { 30, 0, 0 }, "30% of hits as extra fire against\nburning monsters; can't ignite" },
	{ SG::FuelThePyre, 2, 4, 100, { 30, 1, 0 }, "30% of hits as extra fire against\nburning monsters, spreading the burn\nto 2 more; can't ignite" },
	{ SG::Frostbite, 1, 4, 120, {}, "Freezing a monster gives your hits 25%\nextra cold for 5 seconds. Mana +20%" },
	{ SG::Conductor, 1, 2, 120, {}, "Hits on shocked monsters arc to one\nmore; can't shock. Mana +20%" },
	// Bleed I-III: chance to bleed (I and II estimated).
	{ SG::SerratedEdge, 1, 1, 110, { 25, 0, 0 }, "Hits have a 25% chance to make\nmonsters bleed. Mana +10%" },
	{ SG::SerratedEdge, 2, 3, 110, { 50, 0, 0 }, "Hits have a 50% chance to make\nmonsters bleed. Mana +10%" },
	{ SG::SerratedEdge, 3, 5, 100, { 100, 0, 0 }, "Hits always make monsters bleed" },
	// Poison I-III: chance to poison (I and III estimated).
	{ SG::Envenom, 1, 1, 110, { 40, 0, 0 }, "Hits have a 40% chance to poison for\n20% of physical and chaos a second for\n2 seconds. 15% less damage. Mana +10%" },
	{ SG::Envenom, 2, 3, 120, { 60, 0, 0 }, "Hits have a 60% chance to poison for\n20% of physical and chaos a second for\n2 seconds. 15% less damage. Mana +20%" },
	{ SG::Envenom, 3, 5, 120, { 100, 0, 0 }, "Hits poison for 20% of physical and chaos\na second for 2 seconds. 15% less\ndamage. Mana +20%" },
	// Armour Break I-III: seconds the armour stays broken.
	{ SG::SunderArmour, 1, 2, 100, { 6, 0, 0 }, "Hits break armour: 25% more physical\ndamage taken for 6 seconds" },
	{ SG::SunderArmour, 2, 3, 100, { 7, 0, 0 }, "Hits break armour: 25% more physical\ndamage taken for 7 seconds" },
	{ SG::SunderArmour, 3, 4, 100, { 8, 0, 0 }, "Hits break armour: 25% more physical\ndamage taken for 8 seconds" },
	{ SG::Pestilence, 1, 2, 120, {}, "A monster dying with your bleed, poison or\nignite on it passes it to 2 more. Mana +20%" },
	{ SG::HoradricSeals, 1, 1, 150, {}, "Stores a seal every 1.5s, up to 2; the next\ncast repeats once per seal at 50%. Mana +50%" },
	{ SG::StoredVolley, 1, 3, 100, {}, "Stores a seal every second, up to 6; the\nnext use fires 1 more projectile each" },
	{ SG::Cascade, 1, 1, 100, {}, "The area repeats on each side of the\ntarget. 30% less damage" },
	// Nova Projectiles I-II: extra projectiles, less damage, seconds between novas.
	{ SG::NovaProjectiles, 1, 2, 100, { 0, 20, 0 }, "Fires in a circle around you. 20% less\ndamage" },
	{ SG::NovaProjectiles, 2, 4, 100, { 6, 50, 8 }, "Fires in a circle with 6 more\nprojectiles, 50% less damage; at most\nonce every 8 seconds" },
	// Ricochet I-II: chance to bounce off a wall.
	{ SG::Ricochet, 1, 2, 100, { 40, 0, 0 }, "Projectiles that hit a wall have a 40%\nchance to bounce to a monster" },
	{ SG::Ricochet, 2, 4, 100, { 50, 0, 0 }, "Projectiles that hit a wall have a 50%\nchance to bounce to a monster" },
	{ SG::Momentum, 1, 2, 120, {}, "40% more damage after moving at least\n2 tiles. Mana +20%" },
	{ SG::CrushingBlow, 1, 4, 100, {}, "Power: 35% more physical damage, 10%\nslower attacks" },
	{ SG::DeliberateCasting, 1, 2, 115, {}, "Power: 35% more damage, 15% slower\ncasts. Mana +15%" },
	// Zenith I-II: more damage near full mana, less mana cost.
	{ SG::BrimmingMana, 1, 1, 120, { 30, 0, 0 }, "Power: 30% more damage while above 90%\nmana. Mana +20%" },
	{ SG::BrimmingMana, 2, 3, 100, { 30, 20, 0 }, "Power: 30% more damage while above 90%\nmana. 20% less mana cost" },
	// Culling Strike I-II: more damage below a share of life.
	{ SG::CoupDeGrace, 1, 4, 100, { 40, 35, 0 }, "Power: 40% more damage to monsters\nbelow 35% life" },
	{ SG::CoupDeGrace, 2, 5, 100, { 40, 42, 0 }, "Power: 40% more damage to monsters\nbelow 42% life" },
	// Rageforged I-II: more damage for 10 Rage, chance to spend none.
	{ SG::Wrathforged, 1, 4, 100, { 35, 0, 0 }, "Spends 10 Rage when it can for 35% more\ndamage" },
	{ SG::Wrathforged, 2, 5, 100, { 35, 30, 0 }, "Spends 10 Rage when it can for 35% more\ndamage; 30% of the time it spends none" },
	// Combo Finisher I-II: more damage, Combo needed.
	{ SG::ComboFinisher, 1, 1, 100, { 25, 3, 0 }, "With 3 Combo or more, spends it all\nfor 25% more damage" },
	{ SG::ComboFinisher, 2, 3, 100, { 40, 5, 0 }, "With 5 Combo or more, spends it all\nfor 40% more damage" },
	{ SG::MixedHost, 1, 3, 100, {}, "Minions deal 7% more damage for each\nkind of minion you have" },
	// Penetration I-III: resistance the hits get past.
	// Deadly Herald: more damage, and Spirit added to the herald's reservation.
	{ SG::DeadlyHerald, 1, 3, 100, { 30, 20, 0 }, "The Aspect's explosions deal 30% more\ndamage. Reserves 20 more Spirit" },
	{ SG::FirePenetration, 1, 2, 100, { 15, 0, 0 }, "Fire hits penetrate 15% of the\nmonster's fire resistance" },
	{ SG::FirePenetration, 2, 3, 100, { 20, 0, 0 }, "Fire hits penetrate 20% of the\nmonster's fire resistance" },
	{ SG::FirePenetration, 3, 5, 100, { 25, 0, 0 }, "Fire hits penetrate 25% of the\nmonster's fire resistance" },
	{ SG::LightningPenetration, 1, 2, 100, { 15, 0, 0 }, "Lightning hits penetrate 15% of the\nmonster's lightning resistance" },
	{ SG::LightningPenetration, 2, 3, 100, { 20, 0, 0 }, "Lightning hits penetrate 20% of the\nmonster's lightning resistance" },
	{ SG::LightningPenetration, 3, 5, 100, { 25, 0, 0 }, "Lightning hits penetrate 25% of the\nmonster's lightning resistance" },
	{ SG::ColdPenetration, 1, 2, 100, { 15, 0, 0 }, "Cold hits penetrate 15% of the\nmonster's cold resistance" },
	{ SG::ColdPenetration, 2, 3, 100, { 20, 0, 0 }, "Cold hits penetrate 20% of the\nmonster's cold resistance" },
	{ SG::ColdPenetration, 3, 5, 100, { 25, 0, 0 }, "Cold hits penetrate 25% of the\nmonster's cold resistance" },
};

} // namespace

std::span<const SupportTierData> SupportTierTable()
{
	return Tiers;
}

const SupportTierData *FindSupportTier(SupportGem gem, int tier)
{
	for (const SupportTierData &data : Tiers) {
		if (data.gem == gem && data.tier == tier)
			return &data;
	}
	return nullptr;
}

int SupportTierCount(SupportGem gem)
{
	int count = 1;
	for (const SupportTierData &data : Tiers) {
		if (data.gem == gem)
			count = std::max<int>(count, data.tier);
	}
	return std::min(count, MaxSupportTier);
}

int SupportTierValue(SupportGem gem, int tier, size_t index)
{
	if (index >= 3)
		return 0;
	const SupportTierData *data = FindSupportTier(gem, std::clamp(tier, 1, SupportTierCount(gem)));
	return data != nullptr ? data->value[index] : 0;
}

int SupportValue(const Poe2PlayerData &data, SkillSlot skill, SupportGem gem, size_t index)
{
	const int tier = data.supportTier(skill, gem);
	return tier > 0 ? SupportTierValue(gem, tier, index) : 0;
}

int UncutSupportGemLevel(int uncutLevel)
{
	constexpr int Levels[] = { 1, 3, 6, 10, 15 };
	return Levels[std::clamp(uncutLevel, 1, MaxUncutSupportLevel) - 1];
}

int UncutSupportLevelForGemLevel(int gemLevel)
{
	int uncut = 1;
	while (uncut < MaxUncutSupportLevel && gemLevel >= UncutSupportGemLevel(uncut + 1))
		uncut++;
	return uncut;
}

int UncutSupportLevelForArea(int areaLevel)
{
	// POE2 0.5's Uncut Support Gem drop levels (research/support-tiers).
	constexpr int AreaLevels[MaxUncutSupportLevel] = { 4, 16, 33, 45, 55 };
	int level = 0;
	while (level < MaxUncutSupportLevel && areaLevel >= AreaLevels[level])
		level++;
	return level;
}

int UncutSupportWeightPercent(int areaLevel)
{
	// A straight climb from the first weight at area level 1 to full weight.
	const int area = std::clamp(areaLevel, 1, UncutSupportFullWeightArea);
	return UncutSupportFirstWeightPercent + ((100 - UncutSupportFirstWeightPercent) * (area - 1) / (UncutSupportFullWeightArea - 1));
}

int SupportTierGemLevel(SupportGem gem, int tier, int baseLevel)
{
	if (const SupportTierData *data = FindSupportTier(gem, tier); data != nullptr)
		return UncutSupportGemLevel(data->uncut);
	return std::max(baseLevel, 1);
}

} // namespace devilution

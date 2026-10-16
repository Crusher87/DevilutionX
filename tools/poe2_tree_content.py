"""What the POE2 Mashup passive tree holds (stage 1): regions, clusters, notables, shrines and jewel sockets.

tools/poe2_passive_tree.py lays these out and links them; this file is only the content, so lore, balance and
layout can be reviewed and changed separately. Version 3: after audit rounds 1 and 2 (lore, balance, fun, visual/rules).

Regions sit on a compass around Tristram, in POE2's attribute order:
  Sorcerer (Magic) top, Warrior (Strength) right, Rogue (Dexterity) bottom, Monk (Dexterity + Magic) left.
Between each pair of neighbours is a shared border zone that mixes both styles.

Each region is a list of clusters. A cluster is a spur of small nodes leading to its notables:
  band:      'inner' (first notables, 4-5 steps), 'mid' (8-12 steps) or 'outer' (12-18 steps)
  small:     the stat lines of its small nodes
  small_alt: optional; every second small node gets these lines instead
  notables:  (name, stat lines, one line of lore)

Stats are PassiveStat names (Source/poe2/passive_tree.hpp). Values are whole numbers.

What the stats mean (they decide which skills a node helps):
  Damage            increased attack damage with any weapon (attacks only)
  MeleeDamage       increased attack damage with melee weapons and unarmed, including daggers and thrown blades
  BowDamage         increased attack damage with bows and crossbows
  StaffDamage       increased attack damage with staves
  TwoHandDamage     increased attack damage with two-handed melee weapons
  OneHandDamage     increased attack damage with one-handed melee weapons
  SpellDamage       increased spell damage
  Fire/Lightning/ColdDamage  increased damage of that type, spells and attacks. Cold is this mod's Magic damage
                    type, so ColdDamage also scales Blood Star, Bone Spirit, Apocalypse and Stone Curse.
  AttackSpeed / CastSpeed     increased speed in percent; every 10% (tree and gear together) skips one frame, up to the cap
                    (MaxPassiveSpeedFrames); only notables carry speed, small nodes do not
  LifeRegenRate     life regenerated per second, in tenths of a percent of maximum life (10 = 1% a second)
  LifeRegen         flat life per second (kept for old content; v2 uses LifeRegenRate)
  ManaCost          increased mana cost; negative is reduced
  Rule stats (shrines and a few notables) feed the unique-item rules of the same name; a shrine and a unique
  with the same rule take the higher value, they do not add.
"""

# Attribute order and compass direction (degrees on screen: 0 right, -90 up).
REGIONS = {
    'Sorcerer': {'angle': -90, 'attributes': ('Magic',), 'defence': 'EnergyShieldPercent', 'tint': 'azure'},
    'Warrior': {'angle': 0, 'attributes': ('Strength',), 'defence': 'ArmorPercent', 'tint': 'iron'},
    'Rogue': {'angle': 90, 'attributes': ('Dexterity',), 'defence': 'EvasionPercent', 'tint': 'blood'},
    'Monk': {'angle': 180, 'attributes': ('Dexterity', 'Magic'), 'defence': 'EvasionPercent|EnergyShieldPercent', 'tint': 'saffron'},
}

# Border zones between neighbours, clockwise from top-right.
BORDERS = {
    'Zakarum Cloister': {'between': ('Sorcerer', 'Warrior'), 'angle': -45,
                         'theme': "Leoric's faith: holy fire, armour and energy shield, block, regeneration, and a corrupted spur of costly power"},
    'Khanduras Muster': {'between': ('Warrior', 'Rogue'), 'angle': 45,
                         'theme': "Leoric's levies: armour and evasion, shields and bows, flasks and provisions, the dead who rose again"},
    'Sightless Eye': {'between': ('Rogue', 'Monk'), 'angle': 135,
                      'theme': 'the monastery between West and East: precision, inner sight, speed, and the Monk storm'},
    'Horadric Archive': {'between': ('Monk', 'Sorcerer'), 'angle': -135,
                         'theme': 'Horadric magic: staff casting, mana, cold as pure arcane force, lore, golems'},
}

CLUSTERS = {
    'Warrior': [
        {'key': 'w_armour', 'band': 'inner', 'title': "Griswold's Forge", 'small': [('ArmorPercent', 8)], 'notables': [
            ("Griswold's Temper", [('ArmorPercent', 25), ('Strength', 7)], 'Griswold the smith tempered every blade and breastplate the town owned.'),
        ]},
        {'key': 'w_melee', 'band': 'inner', 'title': 'Brute Force', 'small': [('MeleeDamage', 5)], 'notables': [
            ("Lachdanan's Oath", [('MeleeDamage', 15), ('LifeOnKill', 6)], "Lachdanan, captain of Leoric's knights, fought the king's curse with all the honour he had left."),
        ]},
        {'key': 'w_twohand', 'band': 'mid', 'title': 'Two-Handed Mastery', 'small': [('TwoHandDamage', 6)], 'notables': [
            ('Executioner', [('TwoHandDamage', 25), ('ToHit', -10)], "The headsman's axe of Leoric's court fell slowly, and once."),
            ('Cleaving Blow', [('TwoHandDamage', 15), ('StunBuildup', 15)], 'A great swing that staggers whatever it does not cut.'),
            ('Reckless Swings', [('AttackSpeed', 6), ('TwoHandDamage', 8), ('ToHit', -12)], 'Swing twice where others swing once, and trust that one of them lands.'),
        ]},
        {'key': 'w_butchery', 'band': 'mid', 'title': 'Butchery', 'small': [('MeleeDamage', 4)], 'small_alt': [('LifeRegenRate', 2)], 'notables': [
            ('The Mangler', [('TwoHandDamage', 25), ('LifePercent', -8), ('ManaPercent', -10)], 'A great axe that does terrible work and dulls the mind that swings it.'),
            ('Eater of Souls', [('MeleeDamage', 12), ('LifeOnKill', 10), ('LifeRegenRate', -2)], 'A great sword that steals life from all it cuts, and slowly from its bearer too.'),
            ('Singing Steel', [('Spirit', 8), ('MeleeDamage', 8)], "'My blade sings for your blood, mortal, and by my dark masters it shall not be denied.'"),
        ]},
        {'key': 'w_onehand', 'band': 'mid', 'title': 'Blade and Mace', 'small': [('OneHandDamage', 6)], 'notables': [
            ("Griswold's Edge", [('OneHandDamage', 16), ('AttackSpeed', 4)], 'Griswold forged one blade from the Anvil of Fury, and it was quick in the hand.'),
            ('Anvil of Fury', [('OneHandDamage', 12), ('StunBuildup', 15)], 'Griswold needed only the Anvil of Fury to forge a blade fit for the labyrinth.'),
            ("The Gladiator's Bane", [('OneHandDamage', 8), ('ArmorPercent', 15)], 'Studded leather that takes the sting out of every blow.'),
        ]},
        {'key': 'w_shield', 'band': 'mid', 'title': 'Shield Line', 'small': [('BlockChance', 1), ('ArmorPercent', 4)], 'notables': [
            ('Defender of Tristram', [('BlockChance', 3), ('ArmorPercent', 15)], "With the army lost in Westmarch, Tristram's few defenders held their own doors."),
            ("King's Guard", [('BlockChance', 3), ('StunThreshold', 20)], "Leoric's household knights were drilled to never give a step."),
            ('Split Skull Shield', [('BlockChance', 3), ('StunBuildup', 20)], 'Catch the blow on the shield, then return it with the rim.'),
        ]},
        {'key': 'w_vitality', 'band': 'mid', 'title': 'Vitality', 'small': [('Vitality', 7)], 'notables': [
            ('Unbroken', [('Vitality', 15), ('LifeRegenRate', 4)], 'Some men simply refuse to fall.'),
            ('Blood of Westmarch', [('LifePercent', 6), ('Vitality', 8)], "Leoric's men left their blood on the fields of Westmarch. Those who came home were harder for it."),
            ('Golden Elixir', [('LifePercent', 4), ('LifeRegenRate', 6)], "The elixir Lachdanan begged for, to lift a king's curse."),
        ]},
        {'key': 'w_regen', 'band': 'mid', 'title': 'Battle Recovery', 'small': [('LifeRegenRate', 2)], 'notables': [
            ("Pepin's Poultice", [('LifeRegenRate', 8), ('FlaskRecovery', 10)], 'Pepin sent everyone who went below away with a pouch of his salves.'),
            ('Second Wind', [('LifeOnKill', 10), ('LifeRegenRate', 4)], 'Each foe that falls puts breath back in your lungs.'),
        ]},
        {'key': 'w_stun', 'band': 'outer', 'title': 'Skullcracking', 'small': [('StunBuildup', 8)], 'notables': [
            ('The Cranium Basher', [('StunBuildup', 25), ('MeleeDamage', 8)], 'A maul from the labyrinth, made to crush bone, living or dead.'),
            ('Bone Breaker', [('TwoHandDamage', 12), ('StunBuildup', 20)], 'Skeletons do not bleed, so the knights learned to shatter them.'),
        ]},
        {'key': 'w_steadfast', 'band': 'outer', 'title': 'Steadfast', 'small': [('StunThreshold', 10)], 'notables': [
            ('Iron Will', [('StunThreshold', 30), ('AllResist', 5)], 'Fear has no purchase on a mind made of iron.'),
            ('Hold the Line', [('ArmorPercent', 20), ('StunThreshold', 20)], 'Stand and let them break upon you.'),
            ('The Undead Crown', [('LifeOnKill', 10), ('StunThreshold', 15)], "Leoric's crown, pried from his bones. It still drinks from those it strikes."),
        ]},
        {'key': 'w_fortress', 'band': 'outer', 'title': 'Fortress', 'small': [('ArmorPercent', 10)], 'notables': [
            ("Arkaine's Valor", [('ArmorPercent', 30), ('AllResist', 5)], 'Arkaine wore Valor when he drove the legions of darkness back to the Burning Hells.'),
            ('Tempered Plate', [('ArmorPercent', 20), ('ArmourVsElements', 10)], 'Steel heated past red takes flame and frost alike.'),
            ('Demonspike Coat', [('ArmorPercent', 20), ('FireResist', 10)], 'Unbreakable full plate that blunts every blow and turns aside fire.'),
        ]},
        {'key': 'w_warlord', 'band': 'outer', 'title': 'Warlord', 'small': [('MeleeDamage', 5)], 'notables': [
            ("King Leoric's Might", [('Strength', 15), ('MeleeDamage', 8)], 'Before the madness, Leoric had always been the holiest of men, and the strongest.'),
            ('Battle Rhythm', [('AttackSpeed', 6), ('MeleeDamage', 8)], 'A veteran fights to a beat only he can hear.'),
            ('Bloodied Veteran', [('Vitality', 10), ('MaxElementalResist', 1)], 'Scarred by every element and still standing.'),
        ]},
    ],
    'Sorcerer': [
        {'key': 's_shield', 'band': 'inner', 'title': 'Energy Shield', 'small': [('EnergyShieldPercent', 8)], 'notables': [
            ('Pure Energy', [('EnergyShieldPercent', 25), ('Magic', 7)], 'Raw mana, wrapped about the body like a second skin.'),
        ]},
        {'key': 's_spell', 'band': 'inner', 'title': 'Spell Power', 'small': [('SpellDamage', 4)], 'notables': [
            ('Vizjerei Lore', [('SpellDamage', 15), ('ManaOnKill', 4)], 'The Vizjerei clan taught that every spell is a bargain struck with power.'),
        ]},
        {'key': 's_mana', 'band': 'mid', 'title': 'Mana Flow', 'small': [('ManaRegen', 8)], 'small_alt': [('MinionDamage', 8)], 'notables': [
            ('Thinking Cap', [('ManaRegen', 25), ('ManaPercent', 6)], 'A plain skull cap that holds more mana than many a staff.'),
            ('Arcane Thirst', [('ManaOnKill', 6), ('ManaRegen', 12)], 'Each death leaves a little magic in the air for the taking.'),
            ('Vizjerei Adept', [('Spirit', 8), ('ManaPercent', 6)], 'An adept keeps a part of his will apart, always bound to some working.'),
        ]},
        {'key': 's_fire', 'band': 'mid', 'title': 'Fire', 'small': [('FireDamage', 5)], 'notables': [
            ('Immolator', [('FireDamage', 20), ('IgniteChance', 15)], 'Fire is the first school a Vizjerei apprentice learns, and the last he forgets.'),
            ('Kindling', [('FireDamage', 16), ('FireResist', 10)], 'A sorcerer who plays with fire learns not to be burned.'),
            ('Ash and Ember', [('FireDamage', 12), ('ManaOnKill', 4)], 'Burn them, and drink what rises from the ash.'),
        ]},
        {'key': 's_lightning', 'band': 'mid', 'title': 'Storm', 'small': [('LightningDamage', 5)], 'notables': [
            ('Storm Spire', [('LightningDamage', 20), ('ShockChance', 15)], 'A war staff from the depths that hums before every storm.'),
            ('Static Field', [('LightningDamage', 18), ('ShockChance', 10), ('LightningResist', -10)], 'The air around you crackles and bites, you included.'),
            ('Maelstrom', [('CastSpeed', 8), ('LightningDamage', 8), ('ManaCost', 10)], 'Faster words, hungrier spells.'),
        ]},
        {'key': 's_cold', 'band': 'mid', 'title': 'Frost and Arcana', 'small': [('ColdDamage', 5)], 'notables': [
            ('Frozen Heart', [('ColdDamage', 20), ('FreezeBuildup', 15)], 'Cold magic answers best to those who feel nothing.'),
            ('Glacial Ward', [('ColdDamage', 10), ('EnergyShieldPercent', 15)], 'A shell of frost turns blows aside.'),
            ('Ring of the Mystics', [('ColdDamage', 20), ('MagicResist', 15), ('FireResist', -10), ('LightningResist', -10)], 'A ring that wards against magic and leaves its wearer open to fire and storm.'),
        ]},
        {'key': 's_cast', 'band': 'mid', 'title': 'Quick Casting', 'small': [('SpellDamage', 4)], 'notables': [
            ("Horazon's Focus", [('CastSpeed', 6), ('SpellDamage', 8)], 'Horazon and his brother Bartuc were the greatest Vizjerei of their age.'),
            ('Quickened Words', [('CastSpeed', 5), ('ManaCost', -6)], 'The Vizjerei cut every word a demon could twist.'),
        ]},
        {'key': 's_minions', 'band': 'mid', 'title': 'Bound Servants', 'small': [('MinionDamage', 8), ('MinionLife', 6)], 'notables': [
            ('Vizjerei Binding', [('MinionDamage', 20), ('MinionLife', 15)], 'The Vizjerei bound demons to their will, and called it scholarship.'),
            ("Horazon's Servants", [('MinionLife', 25), ('Spirit', 8)], 'Horazon kept demons as servants in his sanctuary.'),
            ('Heart of the Golem', [('MinionDamage', 15), ('MinionLife', 20), ('ManaPercent', -6)], "A golem walks on a share of its maker's mana."),
        ]},
        {'key': 's_barrier', 'band': 'outer', 'title': 'Barrier', 'small': [('EnergyShieldPercent', 10)], 'notables': [
            ('Mana Before Flesh', [('HitDamageFromMana', 10), ('ManaPercent', 10)], 'A sorcerer can let the blow fall on his mana instead of his flesh.'),
            ('Melding', [('EnergyShieldPercent', 40), ('ManaPercent', -12)], 'Mana poured into the ward is mana not spent on spells.'),
            ('Quick Recovery', [('EnergyShieldRecharge', 25), ('EnergyShieldPercent', 10)], 'The shield knits itself back the moment you step clear.'),
        ]},
        {'key': 's_archmage', 'band': 'outer', 'title': 'Archmage', 'small': [('ManaPercent', 4)], 'notables': [
            ('Wellspring', [('ManaSpellDamage', 2), ('ManaPercent', 10)], 'The deeper the well, the stronger the spell.'),
            ('Elemental Mastery', [('FireDamage', 12), ('LightningDamage', 12), ('ColdDamage', 12), ('AllResist', -5)], 'Fire, storm and ice bow to a master, and lash out at him too.'),
        ]},
        {'key': 's_wards', 'band': 'outer', 'title': 'Wards', 'small': [('AllResist', 3)], 'notables': [
            ('Warding Circle', [('AllResist', 8), ('MaxMagicResist', 2)], 'Chalk, salt and old words keep the worst of it out.'),
            ("Wisdom's Wrap", [('EnergyShieldPercent', 20), ('HitDamageFromMana', 5), ('ManaPercent', 6)], 'A mage\'s robe that adds to his mana and softens every blow.'),
        ]},
        {'key': 's_clans', 'band': 'outer', 'title': 'Mage Clans', 'small': [('SpellDamage', 4)], 'notables': [
            ('Mage Clan Wars', [('SpellDamage', 16), ('IgniteChance', 10)], 'When the clans of Kehjistan turned on each other, whole cities burned.'),
            ("Bartuc's Hunger", [('SpellDamage', 20), ('EnergyShieldPercent', -20)], "Bartuc of the Vizjerei bathed in his enemies' blood and took his power from demons."),
            ('Arcane Sanctuary', [('EnergyShieldPercent', 20), ('EvasionVsSpells', 5)], "Horazon hid in a sanctum of his own making, out of the world's reach."),
        ]},
    ],
    'Rogue': [
        {'key': 'r_evasion', 'band': 'inner', 'title': 'Evasion', 'small': [('EvasionPercent', 8)], 'notables': [
            ('Light Feet', [('EvasionPercent', 25), ('Dexterity', 7)], 'A Sister of the Sightless Eye is never where the blow lands.'),
        ]},
        {'key': 'r_bow', 'band': 'inner', 'title': 'Archery', 'small': [('BowDamage', 5)], 'notables': [
            ('Sisterhood Volley', [('BowDamage', 15), ('AttackSpeed', 4)], 'The Sisters loose together, and nothing stands in the field before them.'),
        ]},
        {'key': 'r_knife', 'band': 'mid', 'title': 'Knife Work', 'small': [('MeleeDamage', 5)], 'notables': [
            ("Gonnagal's Dirk", [('MeleeDamage', 15), ('AttackSpeed', 4)], 'A short blade from the labyrinth, quick and mean.'),
            ("Stalker's Step", [('AttackSpeed', 5), ('EvasionPercent', 15), ('LightRadius', -1)], 'Step out of the dark, strike, and step back into it.'),
            ('Cold Steel', [('ColdDamage', 15), ('MeleeDamage', 8)], 'Freeze them first, and the knife does the rest.'),
        ]},
        {'key': 'r_fletching', 'band': 'mid', 'title': 'Fletching', 'small': [('BowDamage', 6)], 'notables': [
            ('Blackoak Fletching', [('BowDamage', 16), ('AttackSpeed', 4)], 'Arrows of black oak fly straight and true.'),
            ('Eaglehorn Draw', [('BowDamage', 12), ('ToHit', 10)], 'A battle bow drawn to the ear.'),
            ('Fleshstinger', [('BowDamage', 25), ('LifePercent', -8)], 'A bow for the archer who would rather kill first and bleed later.'),
        ]},
        {'key': 'r_aim', 'band': 'mid', 'title': 'Precision', 'small': [('ToHit', 4)], 'notables': [
            ('Sightless Aim', [('ToHit', 12), ('DamageVsRareUnique', 5)], 'The Sisters are taught to shoot with the inner eye.'),
            ("Hunter's Patience", [('ToHit', 10), ('BowDamage', 8)], 'Wait for the shot that matters.'),
            ('Windforce', [('BowDamage', 16), ('StunBuildup', 20)], 'A great war bow that hurls its target back.'),
        ]},
        {'key': 'r_speed', 'band': 'mid', 'title': 'Swiftness', 'small': [('BowDamage', 5)], 'notables': [
            ('The Needler', [('AttackSpeed', 6), ('ToHit', 8), ('BowDamage', -8)], 'Many small wounds, quickly given.'),
            ('Gnat Sting', [('AttackSpeed', 6), ('ToHit', -10)], 'A light bow, and many light arrows.'),
        ]},
        {'key': 'r_survival', 'band': 'mid', 'title': 'Survival', 'small': [('FlaskRecovery', 6)], 'notables': [
            ("Akara's Remedy", [('FlaskRecovery', 20), ('LifeRegenRate', 4)], "The priestess Akara's draughts kept the Sisters alive."),
            ('Feast of the Fallen', [('LifeOnKill', 10), ('ManaOnKill', 4)], 'The Fallen breed fast and die faster.'),
            ('Quick Hands', [('FlaskCharges', 20), ('AttackSpeed', 3)], 'A Sister drinks between heartbeats.'),
        ]},
        {'key': 'r_lightning', 'band': 'outer', 'title': 'Charged Arrows', 'small': [('LightningDamage', 4)], 'notables': [
            ('Storm Arrows', [('LightningDamage', 18), ('ShockChance', 15)], 'Arrows tipped with captured lightning.'),
            ('Thunderhead', [('LightningDamage', 12), ('BowDamage', 8)], 'The storm follows the archer.'),
        ]},
        {'key': 'r_fire', 'band': 'outer', 'title': 'Fire Arrows', 'small': [('FireDamage', 4)], 'notables': [
            ('Burning Pitch', [('FireDamage', 18), ('IgniteChance', 15)], 'Pitch-soaked heads, lit from the campfire.'),
            ('Flaming Volley', [('FireDamage', 12), ('PhysicalToFire', 15)], 'Set the whole field ablaze.'),
            ('Flamedart', [('FireDamage', 15), ('AttackSpeed', 4)], 'A bow whose arrows catch light as they leave the string.'),
        ]},
        {'key': 'r_frost', 'band': 'outer', 'title': 'Frost Arrows', 'small': [('ColdDamage', 4)], 'notables': [
            ('Hoarfrost Shafts', [('ColdDamage', 16), ('FreezeBuildup', 15), ('BowDamage', 8)], 'Shafts left out on the monastery walls overnight.'),
            ('Highland Frost', [('ColdDamage', 12), ('EvasionPercent', 12)], 'On the Tamoe Highland a Sister learns to strike and keep moving.'),
        ]},
        {'key': 'r_shadow', 'band': 'outer', 'title': 'Shadows', 'small': [('EvasionPercent', 10)], 'notables': [
            ('Halls of the Blind', [('BowDamage', 12), ('StationaryDefences', 15)], "'Shadows move where light should be.' In the Halls of the Blind an archer holds her ground and shoots at what moves."),
            ('Elusive', [('EvasionPercent', 25), ('LightRadius', -1), ('StunThreshold', -20)], 'Walk where the light does not reach.'),
        ]},
        {'key': 'r_blades', 'band': 'mid', 'title': 'Back-Alley Blades', 'small': [('MeleeDamage', 5)], 'notables': [
            ('Black Razor', [('OneHandDamage', 20), ('AttackSpeed', 5)], 'A thin black dagger that cuts far deeper than its size.'),
            ('Shadowhawk', [('MeleeDamage', 12), ('LifeOnKill', 8), ('EvasionPercent', 10)], 'A broadsword that drinks from every foe it fells and keeps its bearer in shadow.'),
        ]},
        {'key': 'r_hunter', 'band': 'outer', 'title': 'Monster Hunter', 'small': [('Dexterity', 7)], 'notables': [
            ("Kashya's Command", [('Dexterity', 15), ('BowDamage', 8)], 'Kashya leads the Sisters with a hard voice and a harder bow.'),
            ('Keen Eyes', [('LightRadius', 2), ('ToHit', 12), ('EvasionPercent', -10)], 'You see them; they see you.'),
            ("Sisters' Vigil", [('Spirit', 8), ('DamageVsRareUnique', 4)], 'One Sister always watches while the others sleep.'),
        ]},
    ],
    'Monk': [
        {'key': 'm_hybrid', 'band': 'inner', 'title': 'Balanced Body', 'small': [('EvasionPercent', 8), ('EnergyShieldPercent', 8)], 'notables': [
            ('Mindful Awareness', [('EvasionPercent', 20), ('EnergyShieldPercent', 20)], 'The body moves; the mind shields.'),
        ]},
        {'key': 'm_staff', 'band': 'inner', 'title': 'Staff Training', 'small': [('StaffDamage', 5)], 'notables': [
            ('Way of the Staff', [('StaffDamage', 15), ('AttackSpeed', 5)], 'The monks of Ivgorod carry no blade, only the staff.'),
        ]},
        {'key': 'm_palm', 'band': 'mid', 'title': 'Empty Hand', 'small': [('MeleeDamage', 5)], 'notables': [
            ('Open Palm', [('MeleeDamage', 15), ('AttackSpeed', 4)], 'An empty hand hides nothing and needs nothing.'),
            ('Shun the Blade', [('MeleeDamage', 10), ('EvasionPercent', 12)], 'The monks fight without metal weapons, and are lighter for it.'),
        ]},
        {'key': 'm_flow', 'band': 'mid', 'title': 'Flow', 'small': [('StaffDamage', 5)], 'notables': [
            ('Flowing Strikes', [('AttackSpeed', 5), ('CastSpeed', 5)], 'Attack and prayer are one movement.'),
            ('Hundred Palms', [('AttackSpeed', 5), ('StaffDamage', 8)], 'Strike faster than the eye can follow.'),
            ('Wind of Ivgorod', [('AttackSpeed', 6), ('CastSpeed', 6), ('LifePercent', -5)], 'The fastest hands in the East, and the thinnest skin.'),
        ]},
        {'key': 'm_stillness', 'band': 'mid', 'title': 'Stillness', 'small': [('ManaRegen', 8)], 'notables': [
            ('Inner Stillness', [('ManaRegen', 25), ('EnergyShieldRecharge', 15)], 'Breathe, and the well refills.'),
            ("Patriarch's Discipline", [('ManaCost', -8), ('Magic', 8)], 'The Patriarchs of Ivgorod waste nothing, least of all prayer.'),
            ('Fast of the Patriarchs', [('LifeRegenRate', 8), ('StunThreshold', 15)], 'A body that has learned to want nothing heals itself.'),
        ]},
        {'key': 'm_storm', 'band': 'mid', 'title': 'Storm Hands', 'small': [('LightningDamage', 4)], 'notables': [
            ('Thunder Palm', [('LightningDamage', 15), ('MeleeDamage', 8), ('ShockChance', 10)], 'A strike that carries the storm in it.'),
            ('Thundercall', [('LightningDamage', 12), ('StaffDamage', 12)], 'A staff that answers the sky.'),
            ('Storm Hymn', [('Spirit', 8), ('LightningDamage', 8)], 'Sung to the gods of the storm, who sometimes listen.'),
        ]},
        {'key': 'm_frost', 'band': 'mid', 'title': 'Frost Forms', 'small': [('ColdDamage', 4)], 'notables': [
            ('Frost Kata', [('ColdDamage', 16), ('AttackSpeed', 4)], 'Learned in the icy winds that batter the Floating Sky Monastery.'),
            ('Glacier Stance', [('ColdDamage', 12), ('FreezeBuildup', 15), ('StunThreshold', 10)], 'Plant your feet in the frost you called.'),
            ('Winter Calm', [('ColdDamage', 10), ('EvasionPercent', 12)], 'Cold hands, cold mind, no wasted motion.'),
        ]},
        {'key': 'm_body', 'band': 'outer', 'title': 'Hardened Body', 'small': [('Vitality', 7)], 'notables': [
            ('Iron Body', [('Vitality', 12), ('StunThreshold', 20)], 'Years of training make the body a weapon and a wall.'),
            ('Floating Sky Training', [('Dexterity', 15), ('Magic', 15), ('LifePercent', -5)], 'In the Floating Sky Monastery, body and mind are trained as one.'),
        ]},
        {'key': 'm_spirit', 'band': 'outer', 'title': 'Spirit', 'small': [('Spirit', 2), ('MinionDamage', 6)], 'notables': [
            ('Veradani Vow', [('Spirit', 8), ('AllResist', 5)], 'These holy warriors undergo intense mental and physical training.'),
            ('Thousand and One Gods', [('Spirit', 8), ('MinionDamage', 15)], 'The monks of Ivgorod find a god in the hearth fire and the river.'),
            ('Gleamsong', [('ManaPercent', 10), ('CastSpeed', 4), ('Vitality', -5)], 'A short staff that deepens the mana and weakens the body.'),
        ]},
        {'key': 'm_quarterstaff', 'band': 'outer', 'title': 'Quarterstaff Mastery', 'small': [('StaffDamage', 6)], 'notables': [
            ('Daibo Form', [('StaffDamage', 20), ('ToHit', 8)], 'The daibo is the first weapon an initiate of Ivgorod learns, and often the last.'),
            ('Sweeping Staff', [('StaffDamage', 12), ('StunBuildup', 15)], 'A wide arc that knocks them off their feet.'),
            ('The Protector', [('StaffDamage', 10), ('EvasionPercent', 12)], 'A staff held across the body turns as many blows as it gives.'),
        ]},
        {'key': 'm_harmony', 'band': 'outer', 'title': 'Harmony', 'small': [('EvasionPercent', 8), ('EnergyShieldPercent', 8)], 'notables': [
            ('Perfect Harmony', [('EvasionPercent', 25), ('EnergyShieldPercent', 25)], 'Order and Chaos, held level, as the Patriarchs hold them.'),
            ('Serene Recovery', [('LifeRegenRate', 6), ('ManaRegen', 15)], 'Peace heals.'),
            ('Unseen Hand', [('EvasionVsSpells', 5), ('Dexterity', 10)], 'Spells pass where the monk no longer stands.'),
        ]},
        {'key': 'm_vipers', 'band': 'outer', 'title': 'Pit of Vipers', 'small': [('LifeRegenRate', 2)], 'notables': [
            ('The Thirteenth Viper', [('EvasionPercent', 20), ('LifeOnKill', 8)], 'Initiates are thrown into the Pit of Twelve Vipers. No one tells them of the thirteenth.'),
            ("Serpent's Form", [('AttackSpeed', 5), ('EvasionPercent', 12)], 'The form every initiate masters before any other.'),
            ("Pilgrim's Alms", [('ManaOnKill', 6), ('ManaRegen', 10)], 'A monk on the road eats what the road gives.'),
        ]},
    ],
}

BORDER_CLUSTERS = {
    'Zakarum Cloister': [
        {'key': 'z_holy', 'band': 'inner', 'title': 'Holy Fire', 'small': [('FireDamage', 4)], 'notables': [
            ('Light of Zakarum', [('FireDamage', 12), ('MeleeDamage', 8), ('IgniteChance', 10)], 'The Zakarum preached that the Light burns away all evil.'),
            ("Akarat's Faith", [('FireResist', 10), ('LifeRegenRate', 6)], 'Faith in the prophet Akarat kept the paladins standing.'),
            ('Lightforge', [('MeleeDamage', 10), ('FireDamage', 10), ('SpellDamage', 10)], 'A forgotten weapon of light and fire from the old Tristram lists.'),
        ]},
        {'key': 'z_cloister', 'band': 'mid', 'title': 'Cloister Guard', 'small': [('ArmorPercent', 7), ('EnergyShieldPercent', 7)], 'notables': [
            ('Cloister Wall', [('ArmorPercent', 20), ('EnergyShieldPercent', 20), ('BlockChance', 2)], 'The cloister walls never fell while a priest still prayed behind them.'),
            ('Holy Defender', [('BlockChance', 4), ('FireResist', 10)], 'A large shield blessed against fire and quick to rise against a blow.'),
            ('Hand of Zakarum', [('AttackSpeed', 5), ('CastSpeed', 5), ('BlockChance', 2)], 'The battle priest strikes and prays in one breath.'),
            ("Naj's Light Plate", [('ArmorPercent', 15), ('EnergyShieldPercent', 15), ('ManaPercent', 6)], 'Plate light enough for a mage to wear into battle.'),
        ]},
        {'key': 'z_litany', 'band': 'mid', 'title': 'Litanies', 'small': [('Spirit', 2)], 'small_alt': [('LifeRegenRate', 2)], 'notables': [
            ('Tithe of the Cloister', [('Spirit', 10), ('ManaPercent', -10)], 'The church takes its tithe, and the faithful are stronger for paying it.'),
            ('Sanctuary of Light', [('LifeRegenRate', 8), ('ArmorPercent', 10)], 'Stand in the Light and be mended.'),
            ('Sermon of Light', [('SpellDamage', 12), ('BlockChance', 2), ('LifeRegenRate', 4)], 'Every dawn the priests of Tristram preached to the faithful.'),
            ('Litany of Mending', [('LifeRegenRate', 6), ('ManaRegen', 12)], 'Chanted over the wounded until the bleeding stops.'),
        ]},
        {'key': 'z_corrupt', 'band': 'outer', 'title': 'Corrupted Faith', 'small': [('SpellDamage', 4), ('MeleeDamage', 4)], 'notables': [
            ("Lazarus's Lie", [('SpellDamage', 20), ('AllResist', -10)], 'Power, offered by a traitor. It always costs.'),
            ("Que-Hegan's Fall", [('FireDamage', 20), ('FireResist', -15)], 'Mephisto took the Que-Hegan\'s body and ruled the Zakarum from within.'),
        ]},
    ],
    'Khanduras Muster': [
        {'key': 'k_guard', 'band': 'inner', 'title': "Leoric's Levies", 'small': [('ArmorPercent', 7), ('EvasionPercent', 7)], 'notables': [
            ('Militia Drill', [('ArmorPercent', 20), ('EvasionPercent', 20)], "Every able man of Tristram drilled on the green before Leoric's march."),
            ("Farnham's Courage", [('StunThreshold', 15), ('LifePercent', 4)], 'Farnham saw what lies below and lived. Barely.'),
            ('Captain of the Levy', [('Strength', 8), ('Dexterity', 8), ('Damage', 8)], 'A captain fights with whatever the armoury had left.'),
        ]},
        {'key': 'k_supply', 'band': 'mid', 'title': 'Supply Wagons', 'small': [('FlaskRecovery', 6)], 'notables': [
            ("Ogden's Provisions", [('FlaskRecovery', 30), ('FlaskCharges', -15)], 'Ogden poured strong for anyone with the nerve to go below, and never had enough to go round.'),
            ("Gillian's Hearth", [('LifeOnKill', 8), ('FlaskCharges', 15)], 'Gillian kept the Rising Sun warm for whoever came back up.'),
            ("Adria's Brews", [('FlaskRecovery', 15), ('ManaRegen', 10)], 'The witch by the river sells what Pepin will not.'),
        ]},
        {'key': 'k_march', 'band': 'mid', 'title': 'The March North', 'small': [('Damage', 4)], 'notables': [
            ('Levy Shieldmen', [('BlockChance', 4), ('MeleeDamage', 10)], "Leoric's levies marched north with a sword in hand and a shield on the arm."),
            ('Arbalest of the Levy', [('BowDamage', 16), ('ArmorPercent', 10)], 'Crossbows for the men who had never drawn a bow.'),
            ('Marching Order', [('AttackSpeed', 5), ('ArmorPercent', 10), ('EvasionPercent', 10)], 'Keep pace, keep formation.'),
            ('The March on Westmarch', [('AllResist', 6), ('MaxElementalResist', 1)], 'Leoric sent his army north against Westmarch. Few came home.'),
        ]},
        {'key': 'k_risen', 'band': 'outer', 'title': 'The Risen Guard', 'small': [('MinionDamage', 6), ('MinionLife', 6)], 'notables': [
            ("Leoric's Legion", [('MinionDamage', 20), ('MinionLife', 20)], "Leoric's knights rose again. Now yours do."),
            ('Royal Crypt', [('MinionLife', 15), ('ArmorPercent', 15)], 'The crypt kept its dead in good armour.'),
        ]},
    ],
    'Sightless Eye': [
        {'key': 'e_precision', 'band': 'inner', 'title': 'Discipline', 'small': [('ToHit', 5)], 'notables': [
            ('Monastery Discipline', [('ToHit', 10), ('AttackSpeed', 5)], 'The monastery of the Sightless Eye trains hand and eye until they are one.'),
            ('Swift Strikes', [('AttackSpeed', 5), ('EvasionPercent', 12)], 'Strike and be gone.'),
            ('Wizardspike', [('MeleeDamage', 8), ('SpellDamage', 8), ('CastSpeed', 5)], "A mage's dagger, as quick to a spell as to a throat."),
        ]},
        {'key': 'e_sight', 'band': 'mid', 'title': 'Second Sight', 'small': [('EvasionPercent', 6), ('EnergyShieldPercent', 6)], 'notables': [
            ('Inner Sight', [('EvasionVsSpells', 5), ('EvasionPercent', 15)], 'The Sisters learned from Eastern teachers to see without eyes.'),
            ('Tracker', [('DamageVsRareUnique', 5), ('LightRadius', 1), ('ToHit', 8)], 'The Sisters read the road east like a book.'),
            ("Blood Raven's Warning", [('Damage', 16), ('LifePercent', -8)], 'Remember what became of the best of the Sisters.'),
            ('The Monastery Gate', [('ArmorPercent', 12), ('EvasionPercent', 12), ('BlockChance', 2)], "The Sisters' monastery guarded the only pass between West and East."),
        ]},
        {'key': 'e_storm', 'band': 'mid', 'title': 'Gathering Storm', 'small': [('LightningDamage', 4)], 'notables': [
            ('Storm Callers', [('LightningDamage', 12), ('ShockChance', 15), ('ManaOnKill', 3)], 'To a Sister, a storm is only another kind of arrow.'),
            ('Lightning Reflexes', [('EvasionPercent', 15), ('AttackSpeed', 4), ('CastSpeed', 4)], 'Faster than the bolt.'),
            ('Storm Rider', [('LightningDamage', 12), ('ManaOnKill', 6)], 'Each kill feeds the next bolt.'),
        ]},
        {'key': 'e_celestial', 'band': 'outer', 'title': 'True Aim', 'small': [('ToHit', 4)], 'notables': [
            ('Celestial Aim', [('ToHit', 12), ('Damage', 8)], 'An arrow loosed by the inner eye finds its own way.'),
            ('Eye of the Storm', [('ShockChance', 20), ('EvasionVsSpells', 5)], 'In the middle of the storm, everything is still and clear.'),
        ]},
    ],
    'Horadric Archive': [
        {'key': 'h_scroll', 'band': 'inner', 'title': 'Scrolls', 'small': [('ManaRegen', 6), ('SpellDamage', 3)], 'notables': [
            ('Horadric Scrolls', [('SpellDamage', 12), ('ManaRegen', 15)], 'The Horadrim wrote down everything they learned of the Prime Evils.'),
            ("Cain's Learning", [('Magic', 12), ('ManaCost', -6), ('LightRadius', 1)], 'The last of the Horadrim reads by candlelight.'),
            ("Naj's Puzzler", [('CastSpeed', 6), ('ManaCost', 8)], 'A staff for a mage in a hurry.'),
        ]},
        {'key': 'h_archive', 'band': 'mid', 'title': 'Horadric Magic', 'small': [('ColdDamage', 4), ('StaffDamage', 3)], 'notables': [
            ('Frost of the Archive', [('ColdDamage', 16), ('FreezeBuildup', 10), ('ManaCost', -5)], 'The Horadrim fought demons with pure arcane force.'),
            ('Horadric Staff', [('SpellDamage', 16), ('CastSpeed', 5)], "Staff and amulet joined, the key the Horadrim left to Tal Rasha's tomb."),
            ('Soulstone Ward', [('EnergyShieldPercent', 15), ('MaxMagicResist', 1)], 'Soulstones held two Prime Evils for centuries.'),
            ("Tyrael's Counsel", [('ManaCost', -8), ('CastSpeed', 5)], 'Tyrael gathered the best of the mage clans and named them Horadrim.'),
        ]},
        {'key': 'h_forbidden', 'band': 'mid', 'title': 'Forbidden Lore', 'small': [('SpellDamage', 4)], 'notables': [
            ("Zhar's Library", [('SpellDamage', 16), ('ManaCost', 10)], "'Here, take this and leave me to my work. Trouble me no more!'"),
            ('Chamber of Bone', [('ColdDamage', 16), ('SpellDamage', 8)], 'Arcane knowledge gained!'),
            ("Zoltun's Insight", [('Magic', 15), ('SpellDamage', 8)], 'Zoltun Kulle knew more than any mage should.'),
        ]},
        {'key': 'h_golem', 'band': 'outer', 'title': 'Clay and Will', 'small': [('MinionDamage', 6), ('MinionLife', 6)], 'notables': [
            ('Golem Binding', [('MinionLife', 20), ('Spirit', 8)], 'A golem raised by magic fights for its maker and no other.'),
            ("Tal Rasha's Sacrifice", [('MaxElementalResist', 1), ('MaxMagicResist', 1), ('ManaPercent', -10)], 'He took the Lord of Destruction into his own flesh so the others could seal the tomb.'),
        ]},
    ],
}

# Shrines: a big boon with a real cost, named after Diablo's shrines. 'owner' is a region or border zone.
# 'message' is the shrine's exact in-game message (quoted as it is, no full stop added); 'rules' is the plain rule text.
SHRINES = [
    {'name': 'Gloomy Shrine', 'owner': 'Warrior',
     'stats': [('ArmorPercent', 40), ('ArmourVsElements', 20), ('NoEvasion', 1), ('Damage', -20)],
     'message': 'Those who defend seldom attack', 'rules': 'Armour also applies to fire, cold and lightning hits. You cannot evade. Less attack damage.'},
    {'name': 'Weird Shrine', 'owner': 'Warrior',
     'stats': [('FifthAttackRepeat', 100), ('Damage', -20)],
     'message': 'The sword of justice is swift and sharp', 'rules': 'Every fifth attack strikes twice, but all your attacks deal 20% reduced damage.'},
    {'name': 'Eldritch Shrine', 'owner': 'Sorcerer',
     'stats': [('SpellsSpendEnergyShield', 25), ('LessEnergyShieldRecharge', 50)],
     'message': 'Crimson and Azure become as the sun', 'rules': 'Spells spend energy shield before mana, and deal up to 25% more damage for it. Energy shield recharges half as fast.'},
    {'name': 'Magical Shrine', 'owner': 'Sorcerer',
     'stats': [('HitDamageFromMana', 25), ('LessManaRecovery', 30)],
     'message': 'While the spirit is vigilant the body thrives', 'rules': 'A quarter of hit damage is taken from mana first. Less mana recovery.'},
    {'name': 'Glimmering Shrine', 'owner': 'Rogue',
     'stats': [('DamageVsRareUnique', 25), ('LessDamageVsNormal', 15)],
     'message': 'Mysteries are revealed in the light of reason', 'rules': 'More damage against rare and unique monsters, less against the rest.'},
    {'name': 'Abandoned Shrine', 'owner': 'Rogue',
     'stats': [('EvasionVsSpells', 20), ('AllResist', -15)],
     'message': 'The hands of men may be guided by fate', 'rules': 'Evasion also dodges spells. Lower resistances.'},
    {'name': 'Quiet Shrine', 'owner': 'Monk',
     'stats': [('LifeRecoveryToShield', 60), ('NoEnergyShieldRecharge', 1)],
     'message': 'The essence of life flows from within', 'rules': 'Life recovery also recovers 60% as much energy shield, even at full life. Energy shield never recharges.'},
    {'name': 'Shimmering Shrine', 'owner': 'Monk',
     'stats': [('SpiritDamage', 2), ('Spirit', 10), ('LessMaxLife', 10)],
     'message': 'Spiritual energy is restored.', 'rules': '2% increased damage per 10 unreserved Spirit. Less maximum life.'},
    {'name': 'Ornate Shrine', 'owner': 'Zakarum Cloister',
     'stats': [('PhysicalToFire', 100), ('FireDamage', 15), ('ManaPercent', -20)],
     'message': 'Salvation comes at the cost of wisdom', 'rules': 'All physical attack damage becomes fire. Less maximum mana.'},
    {'name': 'Mysterious Shrine', 'owner': 'Khanduras Muster',
     'stats': [('MinionDamage', 40), ('MinionLife', 20), ('Damage', -25), ('SpellDamage', -25)],
     'message': 'Some are weakened as one grows strong', 'rules': 'Your minions deal more damage and have more life. Your own attacks and spells deal less damage.'},
    {'name': 'Secluded Shrine', 'owner': 'Sightless Eye',
     'stats': [('DistanceRule', 25), ('MovingVulnerability', 20)],
     'message': 'The way is made clear when viewed from above', 'rules': 'More damage while no enemy is near you. You take more damage for a moment after moving.'},
    {'name': 'Enchanted Shrine', 'owner': 'Horadric Archive',
     'stats': [('SpellElementCycle', 30), ('OffElementLess', 15)],
     'message': 'Magic is not always what it seems to be', 'rules': 'Your spells cycle fire, cold and lightning. Spells of the current element deal more damage, the others less.'},
]

# Jewel sockets, in socket-ordinal order: this order is save data (jewels are stored by ordinal), never reorder it.
JEWEL_SOCKETS = ['Warrior', 'Monk', 'Sorcerer', 'Sightless Eye', 'Rogue', 'Zakarum Cloister', 'Khanduras Muster', 'Horadric Archive']

# The two links out of each start: the first small node on each, like POE2's start nodes.
START_EXITS = {
    'Warrior': [[('ArmorPercent', 8)], [('MeleeDamage', 5)]],
    'Sorcerer': [[('EnergyShieldPercent', 8)], [('SpellDamage', 4)]],
    'Rogue': [[('EvasionPercent', 8)], [('BowDamage', 5)]],
    'Monk': [[('EvasionPercent', 8), ('EnergyShieldPercent', 8)], [('StaffDamage', 5)]],
}

# Travel: "+5 to any Attribute" nodes (Strength, Dexterity, Magic or Vitality, the player picks).
ANY_ATTRIBUTE = [('AnyAttribute', 5)]

# Points: one per level after the first, plus 2 per completed quest, up to 24 (POE2: 12 quests x 2).
QUEST_POINTS_EACH = 2
QUEST_POINTS_MAX = 24

# Distance rules for stage 1, in steps from a class start (the checker enforces these).
DISTANCE_RULES = {
    'own_first_notable': (4, 5),
    'own_notable_median': (10, 12),
    'own_farthest_notable': (16, 18),
    'own_shrines': (8, 13),
    'border_first_notable': (6, 7),
    'border_equal': True,             # a border zone's hub is the same number of steps from both neighbours
    'border_shrine': (11, 13),
    'neighbour_first_notable_min': 14,
    'neighbour_shrine_min': 17,
    'opposite_first_notable': (22, 25),
    'opposite_shrine_min': 24,
    'class_jewel': (9, 11),
    'border_jewel': (12, 15),
    'attribute_share': (0.07, 0.09),  # "+5 any attribute" nodes as a share of the main tree
    'notable_share': (0.22, 0.26),    # notables as a share of the main tree
}

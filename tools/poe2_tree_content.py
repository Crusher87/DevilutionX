"""What the POE2 Mashup passive tree holds: regions, clusters, notables, shrines, jewel sockets, and (stage 3) the
hand-authored layout anchors, travel rules and notable roles of the rework (research/tree-rework/design-spec.md).

tools/poe2_passive_tree.py lays these out, links them and checks the result; this file is only content and anchors,
so lore, balance and layout can be reviewed and changed separately. Version 4: stage 3 of the tree rework, with the
round-2 lore fixes (no class words in lore lines, Floating Sky and Westmarch removed, Leoric capped, the Profane names).

Regions sit on a compass around Tristram, in POE2's attribute order:
  Sorcerer (Magic) top, Warrior (Strength) right, Rogue (Dexterity) bottom, Monk (Dexterity + Magic) left.
Between each pair of neighbours is a shared border zone that mixes both styles.

Each region is a list of clusters (families). In the stage 3 tree a family's notables are dealt into the notable
slots the generator places (see ROLES below), so 'band' is now only a preference for how far out they go:
  band:      'inner' (the opening notables at 5), 'mid' or 'outer'
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
                      'theme': 'the monastery between West and East: precision, inner sight, speed, and the eastern storm'},
    'Horadric Archive': {'between': ('Monk', 'Sorcerer'), 'angle': -135,
                         'theme': 'Horadric magic: staff casting, mana, cold as pure arcane force, lore, golems'},
}

CLUSTERS = {
    'Warrior': [
        {'key': 'w_armour', 'band': 'inner', 'title': "Griswold's Forge", 'small': [('ArmorPercent', 8)], 'notables': [
            ("Griswold's Temper", [('ArmorPercent', 20), ('LifeRegen', 2)], 'Griswold the smith tempered every blade and breastplate the town owned.'),
        ]},
        {'key': 'w_melee', 'band': 'inner', 'title': 'Brute Force', 'small': [('MeleeDamage', 5)], 'notables': [
            ("Lachdanan's Oath", [('MeleeDamage', 12), ('LifeOnKill', 6)], "Lachdanan, captain of Leoric's knights, fought the king's curse with all the honour he had left."),
        ]},
        {'key': 'w_twohand', 'band': 'mid', 'title': 'Two-Handed Mastery', 'small': [('TwoHandDamage', 6)], 'notables': [
            ('Executioner', [('TwoHandDamage', 15), ('MoreVsStunned', 10)], "The headsman's axe of Leoric's court fell slowly, and only on men already brought to their knees."),
            ('Cleaving Blow', [('TwoHandDamage', 15), ('StunBuildup', 15)], 'A great swing that staggers whatever it does not cut.'),
            ('Reckless Swings', [('AttackSpeed', 6), ('TwoHandDamage', 8), ('ToHit', -12)], 'Swing twice where others swing once, and trust that one of them lands.'),
        ]},
        {'key': 'w_butchery', 'band': 'mid', 'title': 'Butchery', 'small': [('MeleeDamage', 4)], 'small_alt': [('LifeRegenRate', 2)], 'notables': [
            ('The Mangler', [('TwoHandDamage', 20), ('Strength', 10)], 'A great axe that does terrible work in strong hands.'),
            ('Eater of Souls', [('LifeOnKill', 10), ('LifePercent', 4)], 'A great sword that steals life from all it cuts.'),
            ('Singing Steel', [('Spirit', 8), ('MeleeDamage', 8)], "'My blade sings for your blood, mortal, and by my dark masters it shall not be denied.'"),
        ]},
        {'key': 'w_onehand', 'band': 'mid', 'title': 'Blade and Mace', 'small': [('OneHandDamage', 6)], 'notables': [
            ("Griswold's Edge", [('OneHandDamage', 16), ('AttackSpeed', 4)], 'Griswold forged one blade from the Anvil of Fury, and it was quick in the hand.'),
            ('Anvil of Fury', [('OneHandDamage', 12), ('StunBuildup', 15)], 'Griswold needed only the Anvil of Fury to forge a blade fit for the labyrinth.'),
            ("The Gladiator's Bane", [('OneHandDamage', 8), ('ArmorPercent', 15)], 'Studded leather that takes the sting out of every blow.'),
        ]},
        {'key': 'w_shield', 'band': 'mid', 'title': 'Shield Line', 'small': [('BlockChance', 1), ('ArmorPercent', 4)], 'notables': [
            ('Defender of Tristram', [('BlockChance', 5), ('ArmorPercent', 15)], "With the army lost in Westmarch, Tristram's few defenders held their own doors."),
            ("King's Guard", [('BlockChance', 4), ('StunThreshold', 20)], "Leoric's household knights were drilled to never give a step."),
            ('Split Skull Shield', [('BlockChance', 3), ('StunBuildup', 20)], 'Catch the blow on the shield, then return it with the rim.'),
            ('Stormshield', [('BlockChance', 4), ('LifePercent', 4)], 'A great shield that turns the storm aside and keeps the one behind it standing.'),
        ]},
        {'key': 'w_vitality', 'band': 'mid', 'title': 'Vitality', 'small': [('Vitality', 7)], 'notables': [
            ('Unbroken', [('Vitality', 15), ('LifeRegenRate', 4)], 'Some men simply refuse to fall.'),
            ('Blood of Westmarch', [('LifePercent', 6), ('Vitality', 8)], "Leoric's men left their blood on the fields of Westmarch. Those who came home were harder for it."),
            ('Golden Elixir', [('LifePercent', 4), ('LifeRegenRate', 6)], "The elixir Lachdanan begged for, to lift a king's curse."),
        ]},
        {'key': 'w_regen', 'band': 'mid', 'title': 'Battle Recovery', 'small': [('LifeRegenRate', 2)], 'notables': [
            ("Pepin's Poultice", [('LifeRegenRate', 8), ('FlaskRecovery', 10)], 'Pepin sent everyone who went below away with a pouch of his salves.'),
            ('Second Wind', [('LifeOnKill', 10), ('Vitality', 8)], 'Each foe that falls puts breath back in your lungs.'),
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
            ('Might of Khanduras', [('Strength', 15), ('MeleeDamage', 8)], 'Before the madness, Leoric had always been the holiest of men, and the strongest.'),
            ('Drums of Westmarch', [('AttackSpeed', 6), ('MeleeDamage', 8)], 'Leoric marched on Westmarch to the drums. His veterans still swing in time.'),
            ('Bloodied Veteran', [('Vitality', 10), ('MaxElementalResist', 1)], 'Scarred by every element and still standing.'),
        ]},
    ],
    'Sorcerer': [
        {'key': 's_shield', 'band': 'inner', 'title': 'Energy Shield', 'small': [('EnergyShieldPercent', 8)], 'notables': [
            ('Azure Ward', [('EnergyShieldPercent', 25), ('ManaRegen', 10)], 'Raw mana, wrapped about the body like a second skin.'),
        ]},
        {'key': 's_spell', 'band': 'inner', 'title': 'Spell Power', 'small': [('SpellDamage', 4)], 'notables': [
            ('Vizjerei Lore', [('SpellDamage', 12), ('ManaOnKill', 4)], 'The Vizjerei clan taught that every spell is a bargain struck with power.'),
        ]},
        {'key': 's_mana', 'band': 'mid', 'title': 'Mana Flow', 'small': [('ManaRegen', 8)], 'small_alt': [('MinionDamage', 8)], 'notables': [
            ('Thinking Cap', [('ManaRegen', 25), ('ManaPercent', 6)], 'A plain skull cap that holds more mana than many a staff.'),
            ('Arcane Thirst', [('ManaOnKill', 6), ('ManaRegen', 12)], 'Each death leaves a little magic in the air for the taking.'),
            ('Vizjerei Adept', [('Spirit', 8), ('ManaPercent', 6)], 'An adept keeps a part of his will apart, always bound to some working.'),
        ]},
        {'key': 's_fire', 'band': 'mid', 'title': 'Fire', 'small': [('FireDamage', 5)], 'notables': [
            ('Immolator', [('FireDamage', 20), ('IgniteChance', 15)], 'Fire is the first school a Vizjerei apprentice learns, and the last he forgets.'),
            ('Kindling', [('FireDamage', 16), ('FireResist', 10)], 'A Vizjerei who plays with fire learns not to be burned.'),
            ('Ash and Ember', [('FireDamage', 12), ('ManaOnKill', 4)], 'Burn them, and drink what rises from the ash.'),
        ]},
        {'key': 's_lightning', 'band': 'mid', 'title': 'Storm', 'small': [('LightningDamage', 5)], 'notables': [
            ('Storm Spire', [('LightningDamage', 20), ('ShockChance', 15)], 'A war staff from the depths that hums before every storm.'),
            ('Static Field', [('LightningDamage', 18), ('ShockChance', 10), ('LightningResist', -10)], 'The air around you crackles and bites, you included.'),
            ('Maelstrom', [('CastSpeed', 8), ('LightningDamage', 12)], 'Faster words, louder thunder.'),
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
            ('Vizjerei Binding', [('MinionDamage', 24), ('MinionLife', 15)], 'The Vizjerei bound demons to their will, and called it scholarship.'),
            ("Horazon's Servants", [('MinionLife', 25), ('Spirit', 8)], 'Horazon kept demons as servants in his sanctuary.'),
            ('Heart of the Golem', [('MinionLife', 25), ('ManaRegen', 10)], "A golem is only as strong as the heart its maker gives it."),
        ]},
        {'key': 's_barrier', 'band': 'outer', 'title': 'Barrier', 'small': [('EnergyShieldPercent', 10)], 'notables': [
            ('Clan Warding', [('EnergyShieldPercent', 30), ('EnergyShieldRecharge', 15)], 'A clan ward is drawn once and renewed with every breath.'),
            ('Mana Before Flesh', [('HitDamageFromMana', 10), ('ManaPercent', 10)], 'A Vizjerei can let the blow fall on his mana instead of his flesh.'),
            ('The Ward Reknits', [('EnergyShieldRecharge', 25), ('StunThreshold', 15)], 'The shield knits itself back the moment you step clear.'),
            ('Mindcry', [('EnergyShieldRecharge', 20), ('CastSpeed', 4)], 'A staff that clears the mind, so the ward mends while the next spell forms.'),
        ]},
        {'key': 's_archmage', 'band': 'outer', 'title': 'Archmage', 'small': [('ManaPercent', 4)], 'notables': [
            ('The Deeper Well', [('ManaSpellDamage', 2), ('ManaPercent', 10)], 'The deeper the well, the stronger the spell.'),
            ('Elements of the Clans', [('SpellDamage', 14), ('AllResist', 5)], 'Fire, storm and ice bow to a master, and spare him.'),
        ]},
        {'key': 's_wards', 'band': 'outer', 'title': 'Wards', 'small': [('AllResist', 3)], 'notables': [
            ('Warding Circle', [('AllResist', 8), ('MaxMagicResist', 2)], 'Chalk, salt and old words keep the worst of it out.'),
            ("Wisdom's Wrap", [('EnergyShieldPercent', 20), ('HitDamageFromMana', 5)], 'A mage\'s robe that adds to his mana and softens every blow.'),
        ]},
        {'key': 's_clans', 'band': 'outer', 'title': 'Mage Clans', 'small': [('SpellDamage', 4)], 'notables': [
            ('Mage Clan Wars', [('SpellDamage', 16), ('IgniteChance', 10)], 'When the clans of Kehjistan turned on each other, whole cities burned.'),
            ('Arcane Sanctuary', [('EnergyShieldPercent', 20), ('EvasionVsSpells', 5)], "Horazon hid in a sanctum of his own making, out of the world's reach."),
        ]},
    ],
    'Rogue': [
        {'key': 'r_evasion', 'band': 'inner', 'title': 'Evasion', 'small': [('EvasionPercent', 8)], 'notables': [
            ('Highland Stride', [('EvasionPercent', 25), ('StunThreshold', 10)], 'A Sister of the Sightless Eye is never where the blow lands.'),
        ]},
        {'key': 'r_bow', 'band': 'inner', 'title': 'Archery', 'small': [('BowDamage', 5)], 'notables': [
            ('Sisterhood Volley', [('BowDamage', 15), ('ToHit', 5)], 'The Sisters loose together, and nothing stands in the field before them.'),
        ]},
        {'key': 'r_knife', 'band': 'mid', 'title': 'Knife Work', 'small': [('MeleeDamage', 5)], 'notables': [
            ("Gonnagal's Dirk", [('MeleeDamage', 15), ('AttackSpeed', 4)], 'A short blade from the labyrinth, quick and mean.'),
            ("Stalker's Step", [('AttackSpeed', 6), ('EvasionPercent', 17)], 'Step out of the dark, strike, and step back into it.'),
            ('Cold Steel', [('ColdDamage', 15), ('MeleeDamage', 8)], 'Freeze them first, and the knife does the rest.'),
        ]},
        {'key': 'r_fletching', 'band': 'mid', 'title': 'Fletching', 'small': [('BowDamage', 6)], 'notables': [
            ('Blackoak Fletching', [('BowDamage', 16), ('AttackSpeed', 4)], 'Arrows of black oak fly straight and true.'),
            ('Eaglehorn Draw', [('BowDamage', 12), ('DamageVsRareUnique', 6)], 'A battle bow drawn to the ear.'),
            ('Fleshstinger', [('BowDamage', 18), ('LifeOnKill', 4)], 'Every kill it makes buys the archer another breath.'),
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
            ('Rainbow Cloak', [('AllResist', 6), ('LifePercent', 4)], 'A cloak of many colours that turns aside fire, lightning and magic alike.'),
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
            ('Hoarfrost Shafts', [('ColdDamage', 18), ('FreezeBuildup', 15)], 'Shafts left out on the monastery walls overnight.'),
            ('Highland Frost', [('ColdDamage', 12), ('EvasionPercent', 12)], 'On the Tamoe Highland a Sister learns to strike and keep moving.'),
        ]},
        {'key': 'r_shadow', 'band': 'outer', 'title': 'Shadows', 'small': [('EvasionPercent', 10)], 'notables': [
            ('Halls of the Blind', [('BowDamage', 12), ('StationaryDefences', 15)], "'Shadows move where light should be.' In the Halls of the Blind an archer holds her ground and shoots at what moves."),
            ('Elusive', [('EvasionPercent', 25), ('EvasionVsSpells', 5)], 'Walk where the light does not reach.'),
            ('Nightscape', [('EvasionPercent', 12), ('FlaskRecovery', 15)], 'A dark cape for those who fight from the shadows and mend in them.'),
        ]},
        {'key': 'r_blades', 'band': 'mid', 'title': 'Back-Alley Blades', 'small': [('MeleeDamage', 5)], 'notables': [
            ('Black Razor', [('OneHandDamage', 20), ('AttackSpeed', 5)], 'A thin black dagger that cuts far deeper than its size.'),
            ('Shadowhawk', [('MeleeDamage', 12), ('EvasionPercent', 10)], 'A broadsword that keeps its bearer in shadow.'),
        ]},
        {'key': 'r_hunter', 'band': 'outer', 'title': 'Monster Hunter', 'small': [('Dexterity', 7)], 'notables': [
            ("Kashya's Command", [('Dexterity', 15), ('BowDamage', 8)], 'Kashya leads the Sisters with a hard voice and a harder bow.'),
            ('Keen Eyes', [('LightRadius', 2), ('ToHit', 12), ('EvasionPercent', -10)], 'You see them; they see you.'),
            ("Sisters' Vigil", [('Spirit', 8), ('DamageVsRareUnique', 4)], 'One Sister always watches while the others sleep.'),
        ]},
    ],
    'Monk': [
        {'key': 'm_hybrid', 'band': 'inner', 'title': 'Balanced Body', 'small': [('EvasionPercent', 8), ('EnergyShieldPercent', 8)], 'notables': [
            ('The Bending Bough', [('EvasionPercent', 23), ('EnergyShieldPercent', 22)], 'The body moves; the mind shields.'),
        ]},
        {'key': 'm_staff', 'band': 'inner', 'title': 'Staff Training', 'small': [('StaffDamage', 5)], 'notables': [
            ('Way of the Staff', [('StaffDamage', 15), ('StunBuildup', 10)], 'The Brotherhood carries no blade, only the staff.'),
        ]},
        {'key': 'm_palm', 'band': 'mid', 'title': 'Empty Hand', 'small': [('MeleeDamage', 5)], 'notables': [
            ('Open Palm', [('MeleeDamage', 15), ('AttackSpeed', 4)], 'An empty hand hides nothing and needs nothing.'),
            ('Shun the Blade', [('MeleeDamage', 12), ('EvasionPercent', 15)], 'The Brotherhood fights without metal weapons, and is lighter for it.'),
        ]},
        {'key': 'm_flow', 'band': 'mid', 'title': 'Flow', 'small': [('StaffDamage', 5)], 'notables': [
            ('Flowing Strikes', [('AttackSpeed', 5), ('CastSpeed', 5)], 'Attack and prayer are one movement.'),
            ('Hundred Palms', [('AttackSpeed', 5), ('StaffDamage', 8)], 'Strike faster than the eye can follow.'),
            ('Wind of Ivgorod', [('AttackSpeed', 6), ('Dexterity', 10)], 'The fastest hands in the East.'),
        ]},
        {'key': 'm_stillness', 'band': 'mid', 'title': 'Stillness', 'small': [('ManaRegen', 8)], 'notables': [
            ('Inner Stillness', [('ManaRegen', 25), ('EnergyShieldRecharge', 15)], 'Breathe, and the well refills.'),
            ("Patriarch's Discipline", [('ManaCost', -8), ('Magic', 8)], 'The Patriarchs of Ivgorod waste nothing, least of all prayer.'),
            ('Fast of the Patriarchs', [('LifeRegenRate', 8), ('StunThreshold', 15)], 'A body that has learned to want nothing heals itself.'),
        ]},
        {'key': 'm_storm', 'band': 'mid', 'title': 'Storm Hands', 'small': [('LightningDamage', 4)], 'notables': [
            ('Thunder Palm', [('LightningDamage', 15), ('MeleeDamage', 10)], 'A strike that carries the storm in it.'),
            ('Thundercall', [('LightningDamage', 12), ('StaffDamage', 12)], 'A staff that answers the sky.'),
            ('Storm Hymn', [('Spirit', 8), ('LightningDamage', 8)], 'Sung to the gods of the storm, who sometimes listen.'),
        ]},
        {'key': 'm_frost', 'band': 'mid', 'title': 'Frost Forms', 'small': [('ColdDamage', 4)], 'notables': [
            ('Frost Kata', [('ColdDamage', 16), ('AttackSpeed', 4)], 'Learned in the icy winds that batter the high eastern passes.'),
            ('Glacier Stance', [('ColdDamage', 14), ('FreezeBuildup', 15)], 'Plant your feet in the frost you called.'),
            ('Winter Calm', [('ColdDamage', 10), ('EvasionPercent', 12)], 'Cold hands, cold mind, no wasted motion.'),
        ]},
        {'key': 'm_body', 'band': 'outer', 'title': 'Hardened Body', 'small': [('Vitality', 7)], 'notables': [
            ('Iron Body', [('Vitality', 12), ('StunThreshold', 20)], 'Years of training make the body a weapon and a wall.'),
            ('Wood Against Iron', [('Dexterity', 15), ('Magic', 15)], 'The Brotherhood forswore iron; the staff and the trained body had to be enough.'),
        ]},
        {'key': 'm_spirit', 'band': 'outer', 'title': 'Spirit', 'small': [('Spirit', 2), ('MinionDamage', 6)], 'notables': [
            ('Veradani Vow', [('Spirit', 8), ('AllResist', 5)], 'These holy men undergo intense mental and physical training.'),
            ('Thousand and One Gods', [('Spirit', 8), ('MinionDamage', 15)], 'The faithful of Ivgorod find a god in the hearth fire and the river.'),
            ('Gleamsong', [('ManaPercent', 10), ('CastSpeed', 4), ('Vitality', -5)], 'A short staff that deepens the mana and weakens the body.'),
        ]},
        {'key': 'm_quarterstaff', 'band': 'outer', 'title': 'Staff of the Bough', 'small': [('StaffDamage', 6)], 'notables': [
            ('Daibo Form', [('StaffDamage', 20), ('ToHit', 8)], 'The daibo is the first weapon an initiate of Ivgorod learns, and often the last.'),
            ('Sweeping Staff', [('StunBuildup', 15), ('AttackSpeed', 4)], 'A wide arc that knocks them off their feet.'),
            ('The Protector', [('StaffDamage', 12), ('EvasionPercent', 15)], 'A staff held across the body turns as many blows as it gives.'),
        ]},
        {'key': 'm_harmony', 'band': 'outer', 'title': 'Harmony', 'small': [('EvasionPercent', 10), ('EnergyShieldPercent', 10)], 'notables': [
            ('Perfect Harmony', [('EvasionPercent', 25), ('ChaosResist', 7)], 'Order and Chaos, held level, as the Patriarchs hold them.'),
            ('The Quiet Cell', [('LifeRegenRate', 6), ('ManaRegen', 15)], 'A night in a quiet cell mends what a day of fighting broke.'),
            ('Unseen Hand', [('EvasionVsSpells', 5), ('Dexterity', 10)], 'Spells pass where you no longer stand.'),
        ]},
        {'key': 'm_vipers', 'band': 'outer', 'title': 'Pit of Vipers', 'small': [('LifeRegenRate', 2)], 'notables': [
            ('The Thirteenth Viper', [('EvasionPercent', 20), ('LifeOnKill', 8)], 'Initiates are thrown into the Pit of Twelve Vipers. No one tells them of the thirteenth.'),
            ("Serpent's Form", [('AttackSpeed', 5), ('EvasionPercent', 12)], 'The form every initiate masters before any other.'),
            ("Pilgrim's Alms", [('ManaOnKill', 6), ('ManaRegen', 10)], 'A pilgrim on the road eats what the road gives.'),
        ]},
    ],
}

BORDER_CLUSTERS = {
    'Zakarum Cloister': [
        {'key': 'z_holy', 'band': 'inner', 'title': 'Holy Fire', 'small': [('FireDamage', 4)], 'notables': [
            ('Light of Zakarum', [('FireDamage', 12), ('MeleeDamage', 10)], 'The Zakarum preached that the Light burns away all evil.'),
            ("Akarat's Faith", [('FireResist', 10), ('LifeRegenRate', 6)], 'Faith in the prophet Akarat kept the paladins standing.'),
            ('Lightforge', [('FireDamage', 12), ('SpellDamage', 12)], 'A forgotten weapon of light and fire from the old Tristram lists.'),
        ]},
        {'key': 'z_cloister', 'band': 'mid', 'title': 'Cloister Guard', 'small': [('ArmorPercent', 7), ('EnergyShieldPercent', 7)], 'notables': [
            ('Cloister Wall', [('ArmorPercent', 20), ('EnergyShieldPercent', 20)], 'The cloister walls never fell while a priest still prayed behind them.'),
            ('Holy Defender', [('BlockChance', 4), ('FireResist', 10)], 'A large shield blessed against fire and quick to rise against a blow.'),
            ('Hand of Zakarum', [('AttackSpeed', 5), ('CastSpeed', 5)], 'The battle priest strikes and prays in one breath.'),
            ("Naj's Light Plate", [('EnergyShieldPercent', 15), ('ManaPercent', 8)], 'Plate light enough for a mage to wear into battle.'),
        ]},
        {'key': 'z_litany', 'band': 'mid', 'title': 'Litanies', 'small': [('Spirit', 2)], 'small_alt': [('LifeRegenRate', 2)], 'notables': [
            ('Tithe of the Cloister', [('Spirit', 10), ('ManaPercent', -10)], 'The church takes its tithe, and the faithful are stronger for paying it.'),
            ('Sanctuary of Light', [('LifeRegenRate', 8), ('ArmorPercent', 10)], 'Stand in the Light and be mended.'),
            ('Sermon of Light', [('SpellDamage', 12), ('BlockChance', 2), ('LifeRegenRate', 4)], 'Every dawn the priests of Tristram preached to the faithful.'),
            ('Litany of Mending', [('LifeRegenRate', 6), ('ManaRegen', 12)], 'Chanted over the wounded until the bleeding stops.'),
        ]},
        {'key': 'z_corrupt', 'band': 'outer', 'title': 'Corrupted Faith', 'small': [('SpellDamage', 4), ('MeleeDamage', 4)], 'notables': [
            ("Que-Hegan's Fall", [('FireDamage', 20), ('FireResist', -15)], 'Mephisto took the Que-Hegan\'s body and ruled the Zakarum from within.'),
        ]},
    ],
    'Khanduras Muster': [
        {'key': 'k_guard', 'band': 'inner', 'title': "Leoric's Levies", 'small': [('ArmorPercent', 7), ('EvasionPercent', 7)], 'notables': [
            ('Militia Drill', [('ArmorPercent', 20), ('EvasionPercent', 20)], "Every able man of Tristram drilled on the green before Leoric's march."),
            ("Farnham's Courage", [('StunThreshold', 15), ('LifePercent', 4)], 'Farnham saw what lies below and lived. Barely.'),
            ('Captain of the Levy', [('Damage', 10), ('Strength', 8)], 'A captain fights with whatever the armoury had left.'),
        ]},
        {'key': 'k_supply', 'band': 'mid', 'title': 'Supply Wagons', 'small': [('FlaskRecovery', 6)], 'notables': [
            ("Ogden's Provisions", [('FlaskRecovery', 30), ('FlaskCharges', -15)], 'Ogden poured strong for anyone with the nerve to go below, and never had enough to go round.'),
            ("Gillian's Hearth", [('LifeOnKill', 8), ('FlaskCharges', 15)], 'Gillian kept the Rising Sun warm for whoever came back up.'),
            ("Adria's Brews", [('FlaskRecovery', 15), ('ManaRegen', 10)], 'The witch by the river sells what Pepin will not.'),
        ]},
        {'key': 'k_march', 'band': 'mid', 'title': 'The March North', 'small': [('Damage', 4)], 'notables': [
            ('Levy Shieldmen', [('BlockChance', 4), ('MeleeDamage', 10)], "Leoric's levies marched north with a sword in hand and a shield on the arm."),
            ('Arbalest of the Levy', [('BowDamage', 16), ('ArmorPercent', 10)], 'Crossbows for the men who had never drawn a bow.'),
            ('Marching Order', [('AttackSpeed', 5), ('ArmorPercent', 12)], 'Keep pace, keep formation.'),
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
            ('Wizardspike', [('MeleeDamage', 10), ('SpellDamage', 10)], "A mage's dagger, as quick to a spell as to a throat."),
        ]},
        {'key': 'e_sight', 'band': 'mid', 'title': 'Second Sight', 'small': [('EvasionPercent', 6), ('EnergyShieldPercent', 6)], 'notables': [
            ('Inner Sight', [('EvasionVsSpells', 5), ('EvasionPercent', 15)], 'The Sisters learned from Eastern teachers to see without eyes.'),
            ('Tracker', [('DamageVsRareUnique', 6), ('ToHit', 8)], 'The Sisters read the road east like a book.'),
            ('The Monastery Gate', [('ArmorPercent', 14), ('EvasionPercent', 14)], "The Sisters' monastery guarded the only pass between West and East."),
        ]},
        {'key': 'e_storm', 'band': 'mid', 'title': 'Gathering Storm', 'small': [('LightningDamage', 4)], 'notables': [
            ('Storm Callers', [('LightningDamage', 12), ('ShockChance', 15)], 'To a Sister, a storm is only another kind of arrow.'),
            ('Lightning Reflexes', [('EvasionPercent', 15), ('CastSpeed', 5)], 'Faster than the bolt.'),
            ('Storm Rider', [('LightningDamage', 10), ('ManaOnKill', 5), ('AttackSpeed', 4)], 'Each kill feeds the next bolt.'),
        ]},
        {'key': 'e_celestial', 'band': 'outer', 'title': 'True Aim', 'small': [('ToHit', 4)], 'notables': [
            ('Celestial Aim', [('ToHit', 12), ('Damage', 8)], 'An arrow loosed by the inner eye finds its own way.'),
            ('Eye of the Storm', [('ShockChance', 20), ('EvasionVsSpells', 5)], 'In the middle of the storm, everything is still and clear.'),
        ]},
    ],
    'Horadric Archive': [
        {'key': 'h_scroll', 'band': 'inner', 'title': 'Scrolls', 'small': [('ManaRegen', 6), ('SpellDamage', 3)], 'notables': [
            ('Horadric Scrolls', [('SpellDamage', 12), ('ManaRegen', 15)], 'The Horadrim wrote down everything they learned of the Prime Evils.'),
            ("Cain's Learning", [('Magic', 12), ('ManaCost', -6)], 'The last of the Horadrim reads by candlelight.'),
            ("Naj's Puzzler", [('CastSpeed', 6), ('ManaCost', 8)], 'A staff for a mage in a hurry.'),
        ]},
        {'key': 'h_archive', 'band': 'mid', 'title': 'Horadric Magic', 'small': [('ColdDamage', 4), ('StaffDamage', 3)], 'notables': [
            ('Frost of the Archive', [('ColdDamage', 16), ('FreezeBuildup', 12)], 'The Horadrim fought demons with pure arcane force.'),
            ('Horadric Staff', [('SpellDamage', 16), ('CastSpeed', 5)], "Staff and amulet joined, the key the Horadrim left to Tal Rasha's tomb."),
            ('Soulstone Ward', [('EnergyShieldPercent', 15), ('ChaosResist', 8)], 'Soulstones held two Prime Evils for centuries.'),
            ("Tyrael's Counsel", [('ManaCost', -8), ('Spirit', 8)], 'Tyrael gathered the best of the mage clans and named them Horadrim.'),
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

# Keystones (kind "Keystone"; until the start redesign they were "Shrine" nodes named after Diablo's shrines): a big
# boon with a real cost. 'owner' is a region or border zone. 'message' is the Diablo shrine's exact in-game message,
# kept as the lore line (quoted as it is, no full stop added); 'rules' is the plain rule text.
SHRINES = [
    {'name': 'Iron Patience', 'owner': 'Warrior',
     'stats': [('ArmorPercent', 40), ('ArmourVsElements', 20), ('NoEvasion', 1), ('Damage', -20)],
     'message': 'Those who defend seldom attack', 'rules': 'Armour also applies to fire, cold and lightning hits. You cannot evade. Less attack damage.'},
    {'name': 'Swift Justice', 'owner': 'Warrior',
     'stats': [('FifthAttackRepeat', 100), ('Damage', -20)],
     'message': 'The sword of justice is swift and sharp', 'rules': 'Every fifth attack strikes twice, but all your attacks deal 20% reduced damage.'},
    {'name': 'Crimson and Azure', 'owner': 'Sorcerer',
     'stats': [('SpellsSpendEnergyShield', 25), ('LessEnergyShieldRecharge', 50)],
     'message': 'Crimson and Azure become as the sun', 'rules': 'Spells spend energy shield before mana, and deal up to 25% more damage for it. Energy shield recharges half as fast.'},
    {'name': 'Vigilant Spirit', 'owner': 'Sorcerer',
     'stats': [('HitDamageFromMana', 25), ('LessManaRecovery', 30)],
     'message': 'While the spirit is vigilant the body thrives', 'rules': 'A quarter of hit damage is taken from mana first. Less mana recovery.'},
    {'name': 'Light of Reason', 'owner': 'Rogue',
     'stats': [('DamageVsRareUnique', 25), ('LessDamageVsNormal', 15)],
     'message': 'Mysteries are revealed in the light of reason', 'rules': 'More damage against rare and unique monsters, less against the rest.'},
    {'name': 'Guided by Fate', 'owner': 'Rogue',
     'stats': [('EvasionVsSpells', 20), ('AllResist', -15)],
     'message': 'The hands of men may be guided by fate', 'rules': 'Evasion also dodges spells. Lower resistances.'},
    {'name': 'Inner Wellspring', 'owner': 'Monk',
     'stats': [('LifeRecoveryToShield', 60), ('NoEnergyShieldRecharge', 1)],
     'message': 'The essence of life flows from within', 'rules': 'Life recovery also recovers 60% as much energy shield, even at full life. Energy shield never recharges.'},
    {'name': 'Unspent Spirit', 'owner': 'Monk',
     'stats': [('SpiritDamage', 2), ('Spirit', 10), ('LessMaxLife', 10)],
     'message': 'Spiritual energy is restored.', 'rules': '2% increased damage per 10 unreserved Spirit. Less maximum life.'},
    {'name': "Salvation's Price", 'owner': 'Zakarum Cloister',
     'stats': [('PhysicalToFire', 100), ('FireDamage', 15), ('ManaPercent', -20)],
     'message': 'Salvation comes at the cost of wisdom', 'rules': 'All physical attack damage becomes fire. Less maximum mana.'},
    {'name': 'Borrowed Strength', 'owner': 'Khanduras Muster',
     'stats': [('MinionDamage', 40), ('MinionLife', 20), ('Damage', -25), ('SpellDamage', -25)],
     'message': 'Some are weakened as one grows strong', 'rules': 'Your minions deal more damage and have more life. Your own attacks and spells deal less damage.'},
    {'name': 'The High Vantage', 'owner': 'Sightless Eye',
     'stats': [('DistanceRule', 25), ('MovingVulnerability', 20)],
     'message': 'The way is made clear when viewed from above', 'rules': 'More damage while no enemy is near you. You take more damage for a moment after moving.'},
    {'name': 'Horadric Cycle', 'owner': 'Horadric Archive',
     'stats': [('SpellElementCycle', 30), ('OffElementLess', 15)],
     'message': 'Magic is not always what it seems to be', 'rules': 'Your spells cycle fire, cold and lightning. Spells of the current element deal more damage, the others less.'},
]


# The two links out of each start: the first small node on each, like POE2's start nodes. Side 0 (anticlockwise)
# first; each exit matches the opening column it leads into (OPENERS), so the Rogue's and Monk's no longer cross.
START_EXITS = {
    'Warrior': [[('MeleeDamage', 5)], [('ArmorPercent', 8)]],
    'Sorcerer': [[('SpellDamage', 4)], [('EnergyShieldPercent', 8)]],
    'Rogue': [[('BowDamage', 5)], [('EvasionPercent', 8)]],
    'Monk': [[('StaffDamage', 5)], [('EvasionPercent', 8), ('EnergyShieldPercent', 8)]],
}

# The opening column's smalls (start redesign 4.2), by the opener family the column leads to: the two lead-ins
# (2 and 3 points) and the two smalls after the first notable (6 and 7). Felt numbers, never speed (plan 4.1 rule 6).
COLUMN_WORDS = {
    'w_melee': ([('MeleeDamage', 5)], [('StunBuildup', 10)], [('MeleeDamage', 5)], [('LifeOnKill', 2)]),
    'w_armour': ([('ArmorPercent', 8)], [('LifeRegen', 1)], [('ArmorPercent', 8)], [('StunThreshold', 10)]),
    's_spell': ([('SpellDamage', 4)], [('ManaOnKill', 2)], [('SpellDamage', 4)], [('ManaPercent', 4)]),
    's_shield': ([('EnergyShieldPercent', 8)], [('ManaRegen', 10)], [('EnergyShieldPercent', 8)], [('AllResist', 3)]),
    'm_staff': ([('StaffDamage', 5)], [('StunBuildup', 10)], [('StaffDamage', 5)], [('ToHit', 5)]),
    'm_hybrid': ([('EvasionPercent', 8), ('EnergyShieldPercent', 8)], [('ManaRegen', 10)],
                 [('EvasionPercent', 8), ('EnergyShieldPercent', 8)], [('AllResist', 3)]),
    'r_bow': ([('BowDamage', 5)], [('ToHit', 5)], [('BowDamage', 5)], [('LifeOnKill', 2)]),
    'r_evasion': ([('EvasionPercent', 8)], [('StunThreshold', 10)], [('EvasionPercent', 8)], [('LifeRegen', 1)]),
}
# The three smalls of the +5 loop beside each class socket (between its two +5 nodes).
LOOP_SMALLS = [[('LifePercent', 3)], [('AllResist', 3)], [('LifePercent', 3)]]
# Felt smalls that take the place of a +5 node or travel small wherever the placing rule moves one out (the corner fix
# and any stat pair or stat line within 12 points of a start), dealt in turn per class.
FELT_SMALLS = {
    'Warrior': [[('LifeRegen', 1)], [('StunBuildup', 10)], [('StunThreshold', 10)]],
    'Sorcerer': [[('ManaRegen', 10)], [('ManaOnKill', 2)], [('StunThreshold', 10)]],
    'Rogue': [[('ToHit', 5)], [('LifeOnKill', 2)], [('LifeRegen', 1)]],
    'Monk': [[('StunBuildup', 10)], [('ManaRegen', 10)], [('ToHit', 5)]],
}
# The tree keeps this many "+5 to any attribute" nodes (plan 4.3): what the start redesign moves out comes back as
# single +5 nodes on the through roads, 13 to 30 points from a start, spread over every class side.
ATTRIBUTE_NODES = 213
ROAD_SINGLE_COSTS = (13, 30)
ROAD_SINGLE_GAP = 5

# "+5 to any Attribute" nodes (Strength, Dexterity, Magic or Vitality, the player picks).
ANY_ATTRIBUTE = [('AnyAttribute', 5)]

# Points: one per level after the first, plus 2 per completed quest, up to 24 (POE2: 12 quests x 2).
QUEST_POINTS_EACH = 2
QUEST_POINTS_MAX = 24

# ==================================================================================================================
# Stage 3 layout tables (design-spec.md §2-§9, §11 "Content"; round-2 review fixes). Read by tools/poe2_passive_tree.py.
#
# Coordinates here are MATHS style, as in the Stage 2 sketch: y up, angles anticlockwise from the right, so the
# Warrior axis is 0, Sorcerer 90, Monk 180, Rogue 270. The TSV and the game use screen coordinates (y down); the
# generator flips y when it writes. Screen angle = -maths angle.
# Local class coordinates (u, v): u runs out along the class axis from the centre, v sideways, + is anticlockwise.
# ==================================================================================================================

ORDER = ['Warrior', 'Sorcerer', 'Monk', 'Rogue']                     # anticlockwise
CLASS_AXIS = {'Warrior': 0, 'Sorcerer': 90, 'Monk': 180, 'Rogue': 270}  # maths degrees
# Border zones: (clockwise class whose anticlockwise kite corner joins, anticlockwise class whose clockwise corner joins)
BORDER_SIDES = {'Zakarum Cloister': ('Warrior', 'Sorcerer'), 'Horadric Archive': ('Sorcerer', 'Monk'),
                'Sightless Eye': ('Monk', 'Rogue'), 'Khanduras Muster': ('Rogue', 'Warrior')}
# Area names, shown on hover only (decision 3). The TSV region column keeps the class key.
AREA_NAMES = {'Warrior': "Leoric's Manor", 'Sorcerer': 'Yshari Sanctum', 'Rogue': 'Tamoe Highland',
              'Monk': 'Brotherhood of the Bough', 'Zakarum Cloister': 'Zakarum Cloister',
              'Khanduras Muster': 'Khanduras Muster', 'Sightless Eye': 'Sightless Eye',
              'Horadric Archive': 'Horadric Archive'}

# Jewel sockets, in socket-ordinal order: this order is save data (jewels are stored by ordinal), never reorder it.
# The first 8 keep today's order; the 4 deep sockets (outer ring, near each class axis) follow. The generator gives
# the socket nodes ids in exactly this order.
JEWEL_SOCKETS = ['Warrior', 'Monk', 'Sorcerer', 'Sightless Eye', 'Rogue', 'Zakarum Cloister', 'Khanduras Muster',
                 'Horadric Archive', 'Warrior deep', 'Monk deep', 'Sorcerer deep', 'Rogue deep']

# ---------------------------------------------------------------- kite anchors (per class, local u, v)
# Topology is identical for every class (so every cost is identical); only the drawing differs.
# Start redesign (Curtis 2026-10-22): no sideways +5 roads. Each side is one column: E the exit (1), two lead-in
# smalls (2, 3), a "+5 to any attribute" (4), the opening notable (5), two smalls (6, 7), the kite corner C (8, +5).
#   T: frame tops (9), J: the own socket at the kite apex (10). Index 0 is the anticlockwise side, the defence side.
#   Column drawing: with N given, 'pre' (exit to notable) and 'post' (notable to corner) are curves; without it,
#   'col' is one curve from the exit to the corner and the notable sits 4/7 of the way. A curve is ('bez', control
#   points), ('arc', sagitta, + left of the way the column runs) or the class wheel.
#   Warrior, the gatehouse: two short posts from the exits to the notables, then each flares out to its corner.
#   Sorcerer, the pentagon: the exits are the bottom edge, the notables the side corners, the corners the upper ones.
#   Rogue, the drawn bow: the defence side is the limb (one long curve), the other the string (straight, a slight sag).
#   Monk, the wheel: a true circle through the start; both columns run round it and meet at the socket on top.
#   loop_side: the frame top the +5 loop hangs off; loop_r, loop_turn: its radius and how far it turns off straight out
#   (turned 50 degrees so the class set pieces keep their ground, 60 the other way for the Rogue's crossing links).
#   X: mid-ring junction (13), D: deep socket (19); sp1/sp2: spoke guide points and zigzag.
CLS = {
    'Warrior': dict(  # gatehouse: two posts, then an arch that flares to the shoulders; pointed apex
        E=[(232, 46), (230, -46)], N=[(396, 60), (394, -58)],
        pre=[('bez', [(314, 50)]), ('bez', [(312, -50)])], post=[('bez', [(480, 110)]), ('bez', [(476, -116)])],
        C=[(410, 316), (402, -322)], T=[(600, 230), (632, -170)], J=(760, 80), loop_side=1, loop_turn=-50,
        X=(860, 10), D=(1030, -206), sp1=[(790, 40)], sp1zig=24, sp2=[(930, -110)], sp2zig=36),
    'Sorcerer': dict(  # pentagon: exits on the bottom edge, notables the side corners; the socket sits flat in the top
        E=[(256, 52), (258, -50)], N=[(332, 168), (336, -170)],
        pre=[('arc', -6), ('arc', 6)], post=[('arc', -8), ('arc', 8)],
        C=[(452, 190), (448, -196)], T=[(600, 128), (606, -122)], J=(610, -58), loop_side=1, loop_turn=-50,
        X=(860, -40), D=(1085, -110), sp1=[(760, -40)], sp1zig=20, sp2=[(980, -60)], sp2zig=30),
    'Rogue': dict(  # drawn bow: the limb curves away to its tip, the string runs straight; they meet at the socket
        E=[(268, 4), (251, -33)], col=[('arc', -70), ('arc', 8)],
        C=[(410, 226), (440, -196)], T=[(598, 252), (640, -186)], J=(755, 83), loop_side=1, loop_turn=60,
        X=(930, 10), D=(1050, -235), sp1=[(860, 60)], sp1zig=20, sp2=[(1010, -120)], sp2zig=30),
    'Monk': dict(  # wheel: a true circle whose centre sits 8 degrees off the axis; the start is on its rim
        wheel=(455, 8), phi=dict(E=(-5, 21), C=(-99, 109), T=(-140, 145), J=180), loop_side=1, loop_turn=-50,
        X=(880, -75), D=(1015, 150), sp1=[(770, 15)], sp1zig=22, sp2=[(930, 120)], sp2zig=40, sp2signs=[-1, 1, -1]),
}

# ---------------------------------------------------------------- border anchors (polar: radius, maths degrees)
# B: border socket (15 from both neighbours), XB: mid-ring junction (17), OB: outer junction (21), spine: guide
# point of the outer spine, s16: the spine node the border shrine hangs from (16), shrine: where the shrine sits.
# Sockets sit 5-12 degrees off the diagonals at four radii, so no two mirror each other about a class axis and the
# quadrilateral they make is not a rectangle (handmade-look must-fix 4).
# lat: per neighbour, the lateral from its kite corner: p9 (9), L (10, where the ladder splits), Q (14, where it
# rejoins next to the socket). The two strands (A t A and t A t) bow out by wa and wb (signed, units) with their
# peaks at fractions pa and pb along the ladder; a rung joins equal-cost nodes. The eight ladders differ in length,
# width and position; 'pocket' ladders are wide enough to hold a notable cluster between their strands.
BRD = {
    'Zakarum Cloister': dict(B=(800, 34), XB=(948, 46), OB=(1100, 47), spine=[(1025, 47)], s16=(866, 38),
                             lat={'Warrior': dict(p9=(585, 38), L=(625, 37), Q=(750, 30), wa=-20, wb=-80, pa=0.45, pb=0.55, pocket=True),
                                  'Sorcerer': dict(p9=(540, 62), L=(600, 60), Q=(760, 45), wa=-24, wb=20, pa=0.6, pb=0.4)},
                             shrine=(930, 52), scenic='Crypt Arch'),
    'Horadric Archive': dict(B=(770, 124), XB=(880, 136), OB=(1040, 130), spine=[(965, 133)], s16=(836, 133),
                             lat={'Sorcerer': dict(p9=(490, 117), L=(530, 118), Q=(700, 123), wa=56, wb=-52, pa=0.5, pb=0.45, pocket=True),
                                  'Monk': dict(p9=(640, 157), L=(668, 152), Q=(780, 141), wa=-22, wb=24, pa=0.4, pb=0.6)},
                             shrine=(880, 146), scenic='Bridge Arc'),
    'Sightless Eye': dict(B=(860, 237), XB=(985, 229), OB=(1125, 232), spine=[(1060, 227)], s16=(925, 238),
                          lat={'Monk': dict(p9=(600, 216), L=(670, 218), Q=(815, 227), wa=80, wb=-74, pa=0.5, pb=0.5, pocket=True),
                               'Rogue': dict(p9=(545, 247), L=(610, 247), Q=(810, 247), wa=-26, wb=22, pa=0.35, pb=0.65)},
                          shrine=(990, 240), scenic='Crypt Arch'),
    'Khanduras Muster': dict(B=(760, 309), XB=(905, 313), OB=(1062, 306), spine=[(990, 311)], s16=(828, 306),
                             lat={'Rogue': dict(p9=(530, 298), L=(570, 299), Q=(712, 301), wa=62, wb=-56, pa=0.55, pb=0.45, pocket=True),
                                  'Warrior': dict(p9=(585, 320), L=(625, 320), Q=(712, 318), wa=-22, wb=20, pa=0.5, pb=0.45)},
                             shrine=(835, 296), scenic='Crypt Arch'),
}
# Ring halves: (class, border) -> guide bumps (fraction along, radius change), vertex zigzag, braid, and the jogs
# (radius shift of the first/last vertex) so the two halves meet their junction at different radii.
MID = {
    ('Warrior', 'Zakarum Cloister'): dict(b=[(0.35, -60), (0.75, 40)], zig=22, j=(38, -20)),
    ('Sorcerer', 'Zakarum Cloister'): dict(b=[(0.3, -50), (0.7, 20)], zig=22, j=(-45, 45)),
    ('Sorcerer', 'Horadric Archive'): dict(b=[(0.35, -15), (0.8, 38)], zig=22, braid=True, j=(-10, 0)),
    ('Monk', 'Horadric Archive'): dict(b=[(0.25, -50), (0.65, 75)], zig=22, braid=True, j=(-35, 58)),
    ('Monk', 'Sightless Eye'): dict(b=[(0.5, 30)], zig=26, j=(40, -25)),
    ('Rogue', 'Sightless Eye'): dict(b=[(0.4, -30), (0.75, 40)], zig=24, j=(10, 45)),
    ('Rogue', 'Khanduras Muster'): dict(b=[(0.45, -60), (0.85, 20)], zig=22, braid=True, j=(50, 35)),
    ('Warrior', 'Khanduras Muster'): dict(b=[(0.45, 75)], zig=24, j=(-40, 30)),
}
OUT = {
    ('Warrior', 'Zakarum Cloister'): dict(b=[(0.35, 60), (0.8, -20)], zig=24, j=(45, 20)),
    ('Sorcerer', 'Zakarum Cloister'): dict(b=[(0.6, -40)], zig=24, j=(12, 92)),
    ('Sorcerer', 'Horadric Archive'): dict(b=[(0.3, 30), (0.75, -30)], zig=14, braid=True, j=(40, 10)),
    ('Monk', 'Horadric Archive'): dict(b=[(0.5, 40)], zig=24, j=(10, 70)),
    ('Monk', 'Sightless Eye'): dict(b=[(0.35, 50), (0.75, -45)], zig=14, braid=True, j=(45, -12)),
    ('Rogue', 'Sightless Eye'): dict(b=[(0.55, -75)], zig=26, j=(12, 45)),
    ('Rogue', 'Khanduras Muster'): dict(b=[(0.3, 50), (0.7, -30)], zig=24, j=(45, 90)),
    ('Warrior', 'Khanduras Muster'): dict(b=[(0.45, -80)], zig=24, j=(10, 65)),
}
# Own shrine 1 hangs off the clockwise kite corner (radius, angle off the axis); own shrine 2 off the anticlockwise
# outer half (radius, angle change from its attach vertex).
SHR1 = {'Warrior': (560, -30), 'Sorcerer': (520, -32), 'Rogue': (470, -36), 'Monk': (655, -16)}
SHR2 = {'Warrior': (935, -14), 'Sorcerer': (995, -10), 'Rogue': (950, 5), 'Monk': (945, -12)}
# Rim spurs to the two capstones per class: (half, vertex index from the deep socket, reach, angle drift, spur
# pattern: from vertex 2 (22 points) 'tAt' puts the capstone at 29 and 't' at 27; from vertex 4 (24) 't' gives 29). One
# capstone sits on each side of the class axis, so each style family's road ends at its own capstone (fun).
RIM = {'Warrior': [('cw', 2, 45, -9, 'tAt'), ('acw', 2, 30, 10, 't')],
       'Sorcerer': [('cw', 2, 40, -10, 'tAt'), ('acw', 2, 30, 9, 't')],
       'Rogue': [('cw', 2, 30, -6, 'tAt'), ('acw', 2, 30, 24, 't')],
       'Monk': [('cw', 2, 50, -9, 'tAt'), ('acw', 2, 30, 10, 't')]}
# The ragged rim (angle, width, depth), dark pockets (radius, angle, size) and one dense knot per area.
BAYS = [(9, 7, 120), (31, 6, 70), (64, 8, 150), (79, 5, 60), (101, 7, 130), (126, 6, 90), (152, 8, 160), (171, 5, 70),
        (197, 7, 110), (218, 6, 150), (247, 8, 90), (263, 5, 130), (289, 7, 160), (318, 6, 80), (337, 8, 120), (353, 5, 60)]
POCKETS = [(770, -24, 80), (1075, 18, 70), (985, 112, 75), (700, 160, 70), (770, 196, 80), (1010, 250, 70),
           (760, 285, 75), (1040, 334, 70), (1040, 42, 65), (1000, 148, 65), (1050, 214, 65), (985, 296, 65)]
KNOTS = {'Warrior': (900, -30), 'Sorcerer': (860, 115), 'Monk': (900, 200), 'Rogue': (870, 250),
         'Zakarum Cloister': (1000, 47), 'Horadric Archive': (960, 120), 'Sightless Eye': (1000, 228), 'Khanduras Muster': (960, 312)}

# ---------------------------------------------------------------- travel smalls (balance must-fix 3)
# Travel smalls are the lattice beads. Values: Life 3%, All res 2%, Mana 4%. On the landmark spine (exits, rails,
# spokes, laterals, border spines, rim spurs) they rotate strictly Res, Life, Mana in the order a player meets them,
# identically for every class. Only the rings and braids carry an area's flavour stat (up to about half). The start
# redesign took the sideways roads out, so the rotation starts on Res to keep each class's reference builds on the
# same Life, Res and Mana counts as before (check 3c).
TRAVEL_STATS = {'Life': [('LifePercent', 3)], 'Res': [('AllResist', 2)], 'Mana': [('ManaPercent', 4)]}
TRAVEL_ROTATION = ['Res', 'Life', 'Mana']
TRAVEL_FLAVOUR = {'Warrior': 'Life', 'Sorcerer': 'Mana', 'Rogue': 'Res', 'Monk': 'Life',
                  'Zakarum Cloister': 'Life', 'Horadric Archive': 'Mana', 'Sightless Eye': 'Res', 'Khanduras Muster': 'Res'}

# ---------------------------------------------------------------- roles for the content pass (fun, balance)
# The generator tags every notable with a role. Roles that need new content get a placeholder name
# '<role: family, side>' and no stats; the content pass writes the name, stats and lore.
#   opener      the two opening notables at 5 (existing: 'inner' clusters)
#   style       two-notable style clusters at 9, one weapon or skill family per rail (fun must-fix 1)
#   family      the family road: at least 4 more notables of the style family on the same side, 11-25 points,
#               ending at that side's capstone (fun must-fix 2)
#   defence     the defence cluster at 9
#   crossover   two per class, facing each border: 15 points from the owner, exactly 25 from the neighbour;
#               crossover-weapon is the one per class that enables a second weapon type (fun must-fix 3)
#   capstone    two per class on the rim spurs, one per side (27 / 29 points); each changes how its side's family plays
#   profane     the Profane notable of each area (names below; upside equal to nearby notables, a drawback that
#               hurts every build: resistances, % life, % mana or less damage)
#   set         the notable of a set piece, worth the detour
#   socket      the notable of the big orbit sweep round each jewel socket (the socket's surroundings, §8)
#   ladder      a notable cluster held between the two strands of a lateral ladder
#   new         any other new notable (balance: aim them at the parity gaps listed in NEW_NOTABLE_AIMS)
# Style families per class: (anticlockwise rail, clockwise rail). Keys are CLUSTERS keys; a family's road takes the
# notables of the listed clusters first.
STYLE = {
    'Warrior': (('w_twohand', 'two-hand', ['w_twohand', 'w_butchery', 'w_stun', 'w_warlord']),
                ('w_shield', 'sword and board', ['w_shield', 'w_onehand', 'w_fortress', 'w_steadfast'])),
    'Sorcerer': (('s_fire', 'elements', ['s_fire', 's_lightning', 's_cold', 's_archmage']),
                 ('s_minions', 'bound servants', ['s_minions', 's_mana', 's_clans', 's_cast'])),
    'Monk': (('m_quarterstaff', 'staff', ['m_quarterstaff', 'm_flow', 'm_storm', 'm_frost']),
             ('m_palm', 'empty hand', ['m_palm', 'm_vipers', 'm_body', 'm_spirit'])),
    'Rogue': (('r_fletching', 'bow volley', ['r_fletching', 'r_aim', 'r_speed', 'r_lightning', 'r_fire', 'r_frost']),
              ('r_knife', 'knife work', ['r_knife', 'r_blades', 'r_shadow', 'r_hunter'])),
}
# The opener on each side matches that side's style pair (start redesign 4.1 rule 5): the Warrior's sword post
# carries the two-hand pair, the Sorcerer's spell side the fire pair. The defence notable hangs on DEFENCE_SIDE.
OPENERS = {'Warrior': ('w_melee', 'w_armour'), 'Sorcerer': ('s_spell', 's_shield'),
           'Rogue': ('r_bow', 'r_evasion'), 'Monk': ('m_staff', 'm_hybrid')}
DEFENCE_SIDE = {'Warrior': 1, 'Sorcerer': 1, 'Monk': 1, 'Rogue': 0}
DEFENCE = {'Warrior': 'w_vitality', 'Sorcerer': 's_barrier', 'Rogue': 'r_survival', 'Monk': 'm_harmony'}
# The crossover that enables a second weapon type: (border it faces, the weapon it opens). Fun must-fix 3.
CROSSOVER_WEAPON = {'Warrior': ('Khanduras Muster', 'bows'), 'Sorcerer': ('Zakarum Cloister', 'attacks with fire (bows, melee)'),
                    'Rogue': ('Sightless Eye', 'staves'), 'Monk': ('Sightless Eye', 'bows')}
# Balance's parity aims for the new notables (design-spec §11 Content).
NEW_NOTABLE_AIMS = {'Rogue': '+5 defensive, 2 of them life or recovery', 'Sorcerer': '+3, 1 life or ES recovery',
                    'Monk': '+2', 'Horadric Archive': '+3, including 2 staff-attack notables', 'Sightless Eye': '+2',
                    'Warrior': 'lean to offence', 'all': 'about a third carry tempo: on-kill recovery, attack or cast speed, stun buildup'}

# Profane Path notables (design-spec §7, lore must-fix 6: The Maiden's Whisper moves to the Rogue, the Sightless Eye
# border takes Blood of the Countess). Names are settled; upside and drawback are content (placeholders).
PROFANE = {'Warrior': "The Black King's Wrath", 'Sorcerer': "Bartuc's Hunger", 'Rogue': "The Maiden's Whisper",
           'Monk': 'The Dark Sickness', 'Zakarum Cloister': "Lazarus's Lie", 'Khanduras Muster': "Gharbad's Bargain",
           'Sightless Eye': 'Blood of the Countess', 'Horadric Archive': 'The Red Soulstone'}
PROFANE_TOOLTIP = 'Profane Path: the short way passes through here'

# Profane Roads, design v8 (research/tree-rework/profane-road/design-v8.md, Curtis approved 2026-10-18): four one-way
# roads in a pinwheel, each from a door 15 points from its owner's start into the next class's ground. Each is 8
# nodes, r1 to r8 from the door: small, small, toll notable, small, small, pact notable, greater small, greater small
# (1.5 times a small). A small is its stats; a notable is (name, stats, lore). The flag stats (value 1) are the
# roads' rules, read in game code. These stats are final: the content scales skip them.
ROADS = {
    'leoric': dict(owner='Warrior', nodes=[
        [('TwoHandDamage', 6)],
        [('ArmorPercent', 9)],
        ("The Black King's Wrath", [('TwoHandDamage', 12), ('Cull', 10), ('DeadDoNotRest', 1)],
         'In the end the King trusted only the axe.'),
        [('BowDamage', 6)],
        [('TwoHandDamage', 4), ('BowDamage', 4)],
        ('The Black Curse', [('KingsDeadPact', 1), ('ArrowDamageMod', 100)],
         "Freeing the King's spirit lifted his curse. Whoever walks this road takes it up again."),
        [('BowDamage', 9)],
        [('Damage', 6)],
    ]),
    'bartuc': dict(owner='Sorcerer', nodes=[
        [('SpellDamage', 5)],
        [('LifePercent', 3)],
        ("Bartuc's Hunger", [('SpellDamage', 12), ('CastSpeed', 5), ('LifeOnKill', 10), ('BartucsHunger', 1)],
         'Bartuc bathed in the blood of his foes and called it strength.'),
        [('Magic', 7)],
        [('TwoHandDamage', 6)],
        ('Paid in Blood', [('TwoHandDamage', 10), ('PaidInBlood', 1)],
         'Bartuc paid for every stroke in blood. His own came first.'),
        [('TwoHandDamage', 9)],
        [('LifePercent', 5)],
    ]),
    # The Vizjerei Road (the Sickness Road until 2026-10-30; the key and the DarkSickness/HiveBreath flags keep their
    # old names so saved trees load unchanged). research/tree-rework/profane-road/sickness-rework/spec-v1.md.
    'sickness': dict(owner='Monk', nodes=[
        [('MeleeDamage', 5)],
        [('IgniteChance', 5), ('ShockChance', 5), ('FreezeBuildup', 5)],
        ('Stolen Elements', [('MeleeDamage', 12), ('KillBurst', 10), ('DarkSickness', 1)],
         'In the Horadric Archive, a brother found Vizjerei scrolls on raw elements. His order forbade him to read them.'),
        [('FireDamage', 4), ('ColdDamage', 4), ('LightningDamage', 4)],
        [('SpellDamage', 5)],
        ('The Vizjerei Pact', [('HiveBreath', 1)],
         'The Vizjerei warned that mortal hands that hold raw elements are burned by them. He held them anyway.'),
        [('SpellDamage', 8)],
        [('EnergyShieldPercent', 12)],
    ]),
    'maiden': dict(owner='Rogue', nodes=[
        [('BowDamage', 6)],
        [('EvasionPercent', 12)],
        ("The Maiden's Whisper", [('BowDamage', 10), ('FirstStrike', 40), ('EyesInTheDark', 1)],
         'A voice in the dark promises a Sister everything she has ever wanted.'),
        [('Damage', 4)],
        [('StaffDamage', 6)],
        ('Out of Darkness', [('OutOfDarkness', 1)],
         'You would do well to tread lightly upon the dark path you have chosen to explore.'),
        [('LightningDamage', 8)],
        [('StaffDamage', 9)],
    ]),
}

# ---------------------------------------------------------------- ascendancies (moved here from the generator)
# Two per class, drawn one at a time in the sanctum (decision 5). Lore must-fixes 3 and 4: Knight of Westmarch is
# now Knight of Khanduras (Westmarch Steel: Khanduran Steel); Sky Tower Ascetic is now Exile of the Bough and takes
# the Wreath island, the Ivgorod Patriarch takes the Tower.
ASCENDANCIES = {
    'Warrior': [
        ('Knight of Khanduras', [('Strength', 10)], [
            ('Shield Bearer', [('ArmorPercent', 30)]),
            ('Khanduran Steel', [('LifePercent', 10), ('Strength', 10)]),
            ("Crusader's Blow", [('Damage', 25)]),
            ('Holy Vigil', [('Damage', 20), ('ToHit', 10)]),
            ('Bulwark of Light', [('LifePercent', 15), ('ArmorPercent', 20)]),
            ('Never Kneel', [('Vitality', 20), ('LifeRegen', 5)]),
        ]),
        ('Blood Warlord', [('Vitality', 10)], [
            ('War Cry', [('Damage', 20)]),
            ('Battle Frenzy', [('AttackSpeed', 10)]),
            ('Scarred Veteran', [('AllResist', 10)]),
            ('Bloodlust', [('LifeRegen', 8)]),
            ('Iron Hide', [('ArmorPercent', 40)]),
            ("Butcher's Cleaver", [('Damage', 30), ('ToHit', 15)]),
        ]),
    ],
    'Rogue': [
        ('Sister of the Sightless Eye', [('Dexterity', 10)], [
            ('Far Sight', [('Damage', 20)]),
            ('Eye of the Sightless', [('ToHit', 20)]),
            ('Rapid Volley', [('AttackSpeed', 10)]),
            ('Through the Heart', [('Damage', 25)]),
            ('Blessed Aim', [('Dexterity', 15), ('EvasionPercent', 20)]),
            ('Hail of Arrows', [('Damage', 30)]),
        ]),
        ("Viz-Jaq'taar", [('Vitality', 10)], [
            ('Shadow Step', [('EvasionPercent', 30)]),
            ('Venom Lore', [('LifeRegen', 6)]),
            ('Mage Slayer', [('AllResist', 12)]),
            ('Poisoned Blades', [('Damage', 20), ('LightningResist', 10)]),
            ('Death Mark', [('Damage', 30)]),
            ('Cloak of Shadows', [('LifePercent', 12), ('ManaPercent', 12)]),
        ]),
    ],
    'Sorcerer': [
        ('Taan Elementalist', [('Magic', 10)], [
            ('Storm Channel', [('CastSpeed', 10)]),
            ('Lightning Unchained', [('SpellLevels', 1)]),
            ('Tempest Ward', [('LightningResist', 20), ('ManaPercent', 15)]),
            ('Mana Shield', [('EnergyShieldPercent', 30)]),
            ('Inferno', [('Damage', 25)]),
            ('Elemental Dominion', [('SpellLevels', 1), ('ManaRegen', 30)]),
        ]),
        ('Vizjerei Summoner', [('Vitality', 10)], [
            ('Pact of Fire', [('FireResist', 20), ('LifePercent', 10)]),
            ('Binding Rites', [('Spirit', 30)]),
            ('Golem Maker', [('MinionDamage', 40)]),
            ('Demonic Pact', [('Damage', 20)]),
            ('Blood Ward', [('LifeRegen', 6), ('EnergyShieldPercent', 20)]),
            ('Lord of the Bound', [('MinionDamage', 60), ('Spirit', 20)]),
        ]),
    ],
    'Monk': [
        ('Exile of the Bough', [('Dexterity', 10)], [
            ('Flowing Water', [('EvasionPercent', 25), ('EnergyShieldPercent', 25)]),
            ('Inner Peace', [('AllResist', 10)]),
            ('Thousand Fists', [('Damage', 25)]),
            ('Whirlwind Kata', [('AttackSpeed', 10)]),
            ('Blessed Staff', [('Damage', 20), ('ToHit', 15)]),
            ('Perfect Form', [('Damage', 30)]),
        ]),
        ('Ivgorod Patriarch', [('Magic', 10)], [
            ('Faith Ward', [('MagicResist', 20)]),
            ('Meditation', [('ManaRegen', 40)]),
            ('Smite', [('Damage', 20)]),
            ('Spirit of Ivgorod', [('ManaPercent', 20)]),
            ('Iron Skin', [('EnergyShieldPercent', 30)]),
            ('Divine Wrath', [('Damage', 30), ('CastSpeed', 10)]),
        ]),
    ],
}

# ---------------------------------------------------------------- name rules (check 26, lore)
CLASS_WORDS = ['warrior', 'sorcerer', 'sorceress', 'rogue', 'monk', 'barbarian', 'bard']   # never in a name or lore line
HERO_NAMES = ['Aidan', 'Moreina', 'Jazreth', 'Blood Raven']
MECHANIC_WORDS = ['Mastery', 'Recovery', 'Awareness', 'Energy', 'Overload', 'Flow', 'Rhythm']
NAME_MAX = 24
# Canon figures a name may cite at most twice in the whole tree (lore: at most 2 notables per figure).
CANON_FIGURES = ['Leoric', 'Griswold', 'Lachdanan', 'Horazon', 'Bartuc', 'Tal Rasha', 'Zoltun', 'Akara', 'Kashya', 'Cain',
                 'Ogden', 'Gillian', 'Pepin', 'Adria', 'Farnham', 'Lazarus', 'Zhar', 'Naj', 'Arkaine', 'Gharbad', 'Tyrael',
                 'Akarat', 'Que-Hegan', 'Countess', 'Maiden', 'Butcher', 'Albrecht', 'Lester', 'Wirt',
                 'Snotspill', 'Na-Krul', 'Fara', 'Warlord']

# ---------------------------------------------------------------- content pass (stage 3)
# The new notables that fill the generator's placeholder slots (notable_roles.tsv). They are ordinary families, but
# each has a 'side' (0 anticlockwise, 1 clockwise, as seen from the area's own axis): the dealer gives a family only to
# slots on its side, so two-hand content lands on the two-hand road and the Sorcerer-facing half of a border gets the
# Sorcerer-facing content. Values are today's notable values for the same stats; the power audit (check 3d) sets any
# global scale on top. Aims (NEW_NOTABLE_AIMS): Rogue +7 defensive (3 life or recovery), Sorcerer +5 (2 life,
# 2 energy shield recovery), Monk +3, Horadric +2 staff attacks and 2 defensive, Sightless Eye +2 defensive, Warrior
# mostly offence; about a third carry tempo (on-kill, speed, stun).
CONTENT_PASS = {
    'Warrior': [
        {'key': 'w_warlord_road', 'side': 0, 'band': 'mid', 'title': "Warlord's Road", 'small': [('TwoHandDamage', 6)], 'notables': [
            ('The Armories of Hell', [('TwoHandDamage', 18), ('LifeOnKill', 8)], 'The armories of Hell are home to the Warlord of Blood. In his wake lie the bodies of thousands.'),
            ('Heavy Momentum', [('TwoHandDamage', 12), ('AttackSpeed', 4)], 'A great blade is slow to start and slower to stop.'),
            ('The Wounded Townsman', [('StunBuildup', 20), ('LifeOnKill', 6)], "'Avenge us! Find this Butcher and slay him so that our souls may finally rest...'"),
            ('Burning Zeal', [('FireDamage', 12), ('TwoHandDamage', 10)], "The cloister's fire burns hottest along a long blade."),
        ]},
        {'key': 'w_deep', 'side': 0, 'band': 'outer', 'title': 'Hardened in the Deep', 'small': [('Vitality', 7)], 'notables': [
            ('Labyrinth Veteran', [('TwoHandDamage', 12), ('LifePercent', 5)], 'Sixteen levels down, a man either grows harder or is carried out.'),
            ('Overhead Chop', [('StunBuildup', 20), ('MoreVsStunned', 10)], 'Lift it high and let its weight do the work.'),
        ]},
        {'key': 'w_square', 'side': 1, 'band': 'mid', 'title': 'Defence of the Square', 'small': [('OneHandDamage', 6)], 'notables': [
            ('Rim and Edge', [('OneHandDamage', 12), ('BlockChance', 3)], 'The palace knights were never handed a sword without a shield to go with it.'),
            ("Snotspill's Banner", [('OneHandDamage', 12), ('LifeOnKill', 6)], '"You give! Yes, good! Go now, we strong. We kill all with big Magic!"'),
            ('Quick Guard', [('AttackSpeed', 5), ('BlockChance', 2), ('OneHandDamage', 6)], 'The shield comes down, the sword goes in.'),
            ("Albrecht's Champion", [('OneHandDamage', 12), ('DamageVsRareUnique', 8)], 'Lazarus carried the prince below. Someone had to go down after him.'),
        ]},
        {'key': 'w_anvil', 'side': 1, 'band': 'outer', 'title': 'Hammer and Anvil', 'small': [('ArmorPercent', 10)], 'notables': [
            ('Hammer and Anvil', [('OneHandDamage', 14), ('Strength', 8)], 'The old smiths say the anvil teaches more than the hammer.'),
            ('Broken Fealty', [('StunThreshold', 15), ('LifePercent', 5)], "Leoric's own knights were forced to kill him. He cursed them for it."),
            ('Tristram by Night', [('ArmorPercent', 15), ('LifePercent', 5)], 'When demons ran through the village at night, every barred door was a wall worth holding.'),
            ('Rising Sun Brawler', [('MeleeDamage', 10), ('Vitality', 8)], 'Ogden kept a club behind the bar of the Rising Sun, and never needed it twice for the same man.'),
        ]},
    ],
    'Sorcerer': [
        {'key': 's_kehjistan', 'side': 0, 'band': 'mid', 'title': 'Clan Towers', 'small': [('LightningDamage', 5)], 'notables': [
            ('Thunder over Kehjistan', [('LightningDamage', 18), ('ManaOnKill', 4)], 'Over the clan towers of Kehjistan the sky never quite cleared.'),
            ('Warding Glyphs', [('AllResist', 6), ('EnergyShieldRecharge', 15)], 'Glyphs chalked on the sleeve flare and fade as the ward knits.'),
            ('Clan Gemcutter', [('EnergyShieldPercent', 15), ('SpellDamage', 10)], 'The clans cut their gems to hold a spell, not to catch the light.'),
        ]},
        {'key': 's_circle', 'side': 1, 'band': 'mid', 'title': 'The Summoning Circle', 'small': [('MinionDamage', 8)], 'notables': [
            ('The Summoning Circle', [('MinionDamage', 20), ('CastSpeed', 4)], 'Draw the circle quickly. What waits beyond it is not patient.'),
            ('Blood-Bound', [('MinionLife', 15), ('LifePercent', 5)], 'Bind it with blood, and its strength runs both ways.'),
            ("The Master's Share", [('ManaOnKill', 6), ('MinionDamage', 10)], 'The servants kill. The master eats.'),
            ('Bound in Brimstone', [('FireDamage', 12), ('MinionDamage', 12)], 'Brimstone is the one gift a demon brings without being asked.'),
        ]},
        {'key': 's_veiled', 'side': 1, 'band': 'outer', 'title': 'The Unspoken Name', 'small': [('SpellDamage', 5)], 'notables': [
            ('The Unspoken Name', [('Spirit', 8), ('EnergyShieldPercent', 15)], 'No Vizjerei told a demon his true name. Some, in time, forgot it themselves.'),
            ('Kehjistan Demonology', [('SpellDamage', 14), ('MinionDamage', 10)], 'In Kehjistan, demonology was taught beside geometry, and with less care.'),
            ("Bartuc's Lessons", [('SpellDamage', 12), ('Magic', 10)], 'Bartuc outpaced every Vizjerei of his age. The demons were patient teachers.'),
            ('Zhar the Mad', [('CastSpeed', 6), ('ManaPercent', 6)], "'All these interruptions are enough to make one insane.'"),
        ]},
        {'key': 's_quiet', 'side': 1, 'band': 'outer', 'title': 'Quiet Study', 'small': [('EnergyShieldPercent', 10)], 'notables': [
            ('Sound Body', [('ManaPercent', 8), ('LifePercent', 5)], 'A mage who forgets the body soon has no use for the mind.'),
            ('Calm Between Spells', [('EnergyShieldRecharge', 20), ('ManaRegen', 15)], 'Let the hands fall quiet, and the ward closes over you again.'),
            ('Ammann Sanctum', [('SpellDamage', 10), ('EnergyShieldRecharge', 10)], 'The Ammann taught that a mage\'s first ward is patience, and the second is a locked door.'),
        ]},
    ],
    'Rogue': [
        {'key': 'r_moor', 'side': 0, 'band': 'mid', 'title': 'The Blood Moor', 'small': [('BowDamage', 6)], 'notables': [
            ('Blood Moor Squall', [('LightningDamage', 14), ('AttackSpeed', 4)], 'Squalls roll in over the Blood Moor faster than a rider.'),
            ('Burning the Den', [('BowDamage', 12), ('FireDamage', 12)], 'The Sisters cleanse a den the old way: bar the door and light the thatch.'),
            ('Cold Plains Patrol', [('EvasionPercent', 15), ('LifePercent', 5)], 'The Sisters walked the Cold Plains in pairs, and came back in pairs.'),
            ('Scavenged Mail', [('ArmorPercent', 15), ('EvasionPercent', 15)], 'Taken off the dead on the Burial Grounds, and mended twice.'),
        ]},
        {'key': 'r_close', 'side': 1, 'band': 'mid', 'title': 'Close Quarters', 'small': [('MeleeDamage', 6)], 'notables': [
            ('Close Quarters', [('MeleeDamage', 12), ('ToHit', 10)], "Inside a bow's reach, the knife is the faster weapon."),
            ('Never Still', [('AttackSpeed', 5), ('Dexterity', 8)], "A Sister's hands are never at rest."),
            ('Blood on the Blade', [('LifeOnKill', 8), ('MeleeDamage', 10)], 'Wipe it after. Never during.'),
            ('Head of the Pack', [('DamageVsRareUnique', 5), ('MeleeDamage', 10)], 'Cut down the one they follow and the rest scatter.'),
        ]},
        {'key': 'r_hidden', 'side': 1, 'band': 'outer', 'title': 'Hidden Ways', 'small': [('EvasionPercent', 10)], 'notables': [
            ('Stony Field', [('StunThreshold', 15), ('LifePercent', 4)], 'The Sisters train on the Stony Field, where any step can turn an ankle.'),
            ("Hunter's Rest", [('EvasionPercent', 12), ('LifeRegenRate', 6)], 'Sleep light, heal fast, move at dawn.'),
            ('Field Rations', [('FlaskRecovery', 15), ('LifeOnKill', 6)], "Bread, salt and one of the priestess's draughts. Save the draught for last."),
            ('Poisoned Water', [('ChaosResist', 12), ('EvasionPercent', 10)], "Something fouled Tristram's water at its source. Pepin begged someone to go down and look."),
            ('Spare Frame', [('Dexterity', 10), ('LifePercent', 4)], "A Sister carries her own weight and her bow's, and nothing else."),
            ('Slip Away', [('ManaOnKill', 4), ('EvasionPercent', 12)], 'Leave nothing behind but the body.'),
            ('The Palisade Watch', [('BowDamage', 10), ('Vitality', 8)], 'The Sisters keep a watch on the palisade every night, and every night the moor tests it.'),
            ('The Black Mushroom', [('FlaskCharges', 15), ('EvasionPercent', 10)], 'Adria wanted a big black mushroom for a brew, and never said what it was for.'),
        ]},
    ],
    'Monk': [
        {'key': 'm_exile', 'side': 0, 'band': 'mid', 'title': "The Exile's Road", 'small': [('StaffDamage', 6)], 'notables': [
            ("Pilgrim's Staff", [('StaffDamage', 15), ('LifeOnKill', 6)], 'A walking staff on the road, a weapon at the inn.'),
            ('Sky-Struck Staff', [('StaffDamage', 12), ('ShockChance', 15)], 'Ash wood, struck once by lightning. The Patriarchs say it remembers.'),
            ("Exile's Road", [('StaffDamage', 12), ('Dexterity', 8)], 'Conquered and driven into the wilderness, the Brotherhood kept only their staves.'),
            ('Rooted Stance', [('EvasionPercent', 18), ('StunThreshold', 15)], 'The bough bends. The root does not.'),
        ]},
        {'key': 'm_living', 'side': 1, 'band': 'mid', 'title': 'Flesh Over Steel', 'small': [('MeleeDamage', 6)], 'notables': [
            ('Ice in the Veins', [('ColdDamage', 12), ('ManaRegen', 10)], 'A calm heart strikes colder.'),
            ('Flesh Over Steel', [('MeleeDamage', 12), ('LifeOnKill', 6)], 'Steel can be taken from you. The hand cannot.'),
            ('Swift Prayer', [('AttackSpeed', 5), ('Magic', 8)], 'A prayer said on the move still reaches the gods.'),
            ('Oath of the Bough', [('MeleeDamage', 10), ('SpellDamage', 10)], 'Conquered once, the Brotherhood swore to master every way of fighting.'),
        ]},
        {'key': 'm_west', 'side': 1, 'band': 'outer', 'title': 'The Long Walk West', 'small': [('Vitality', 7)], 'notables': [
            ('The Long Walk West', [('Vitality', 10), ('LifeRegenRate', 4)], 'From the East to Khanduras is a long way on foot. Few brothers walk it back.'),
            ('Weathered Body', [('EvasionPercent', 10), ('LifePercent', 6)], 'Sun, snow and the switch have left nothing soft.'),
            ('Breath and Blow', [('ManaRegen', 15), ('MeleeDamage', 8)], 'Breathe out as you strike, and the next breath comes easier.'),
            ("Hearth God's Blessing", [('Spirit', 8), ('LifeRegenRate', 4)], 'Even the smallest god of Ivgorod keeps a fire going for the faithful.'),
            ('Will of the Bough', [('StaffDamage', 10), ('Vitality', 8)], 'A bough that bends in the storm is still on the tree in the morning.'),
        ]},
    ],
    'Zakarum Cloister': [
        {'key': 'z_inquisition', 'side': 0, 'band': 'mid', 'title': 'The Inquisition', 'small': [('FireDamage', 5)], 'notables': [
            ('Purifying Flame', [('FireDamage', 12), ('CastSpeed', 4)], 'The priests burned what they could not cleanse.'),
            ('The Inquisition', [('FireDamage', 10), ('Spirit', 8)], 'In the Time of Troubles the Zakarum asked their questions with fire.'),
            ('Candlelit Rites', [('CastSpeed', 5), ('FireResist', 10)], 'A rite read quickly by candlelight, before the candle gutters.'),
        ]},
        {'key': 'z_vespers', 'side': 0, 'band': 'outer', 'title': 'Vespers', 'small': [('EnergyShieldPercent', 7), ('ArmorPercent', 7)], 'notables': [
            ('Bells of Tristram', [('EnergyShieldPercent', 15), ('LifeRegenRate', 4)], 'The cathedral bells once rang each evening over Tristram. No one has rung them since.'),
            ('Vows of the Order', [('ManaPercent', 8), ('ArmorPercent', 10)], 'Poverty, obedience, and a mail shirt under the robe.'),
        ]},
        {'key': 'z_penance', 'side': 1, 'band': 'mid', 'title': 'Penance', 'small': [('ArmorPercent', 7), ('LifeRegenRate', 2)], 'notables': [
            ("Fara's Penance", [('MeleeDamage', 10), ('LifeRegenRate', 4)], 'Fara was once a devout champion of Zakarum. Now she works a forge, and does not say why.'),
            ('Cornerstone of the World', [('ArmorPercent', 15), ('FireResist', 10)], 'The first cathedral stood on a cornerstone of Antyrael. When it fell, the stone was lost.'),
        ]},
    ],
    'Khanduras Muster': [
        {'key': 'k_garrison', 'side': 0, 'band': 'mid', 'title': 'The Old Garrison', 'small': [('ArmorPercent', 7), ('Damage', 3)], 'notables': [
            ('Bones of the Garrison', [('MinionDamage', 12), ('ArmorPercent', 15)], 'The old garrison was buried beneath the cathedral. Not all of it stayed buried.'),
            ("Lester's Orchard", [('Damage', 10), ('LifeOnKill', 6)], "\"At the edge of my orchard... there's a horrible thing swelling out of the ground!\""),
            ("Quartermaster's Ledger", [('FlaskCharges', 15), ('ArmorPercent', 10)], 'Every arrow, every potion and every boot, counted twice.'),
            ('The Talsande Ford', [('StunBuildup', 15), ('StunThreshold', 15)], 'The levy held the ford on the Talsande for a day and a night.'),
        ]},
        {'key': 'k_woods', 'side': 1, 'band': 'mid', 'title': 'Woods of Khanduras', 'small': [('BowDamage', 5), ('EvasionPercent', 5)], 'notables': [
            ('Poachers of Khanduras', [('BowDamage', 12), ('LifeOnKill', 6)], "The king's foresters hanged poachers. In the war they made them archers."),
            ("Wirt's Wares", [('EvasionPercent', 15), ('FlaskRecovery', 12)], 'Wirt asks a fortune and will not say where any of it came from.'),
            ('Grave Matters', [('ChaosResist', 10), ('LifePercent', 4)], "'I'd keep my mind on what lies below the cathedral and not what lies below our topsoil.'"),
        ]},
    ],
    'Sightless Eye': [
        {'key': 'e_unclouded', 'side': 0, 'band': 'mid', 'title': 'The Unclouded Eye', 'small': [('ToHit', 5)], 'notables': [
            ('Blindfold Drill', [('Dexterity', 12), ('ToHit', 10)], 'The blindfold stays on until the stones stop hitting you.'),
            ('The Road to Lut Gholein', [('EvasionPercent', 15), ('LifeRegenRate', 4)], 'Snow closes the pass by autumn. Whoever is still on it stays till spring.'),
            ("Kashya's Drill", [('AttackSpeed', 5), ('Damage', 8)], 'Kashya drills the young Sisters until the bow moves before the thought does.'),
        ]},
        {'key': 'e_eastern', 'side': 1, 'band': 'mid', 'title': 'The Eastern Storm', 'small': [('LightningDamage', 5)], 'notables': [
            ('Never Where It Struck', [('EvasionPercent', 12), ('LightningDamage', 10)], 'Move with the storm, and it takes you for weather.'),
            ('Red Storm', [('ShockChance', 15), ('AttackSpeed', 4)], 'The air around fast hands begins to spark.'),
            ('Mountain Thunder', [('LightningDamage', 12), ('StaffDamage', 10)], 'Up in the passes you hear the storm in your teeth.'),
        ]},
        {'key': 'e_disciplines', 'side': 1, 'band': 'outer', 'title': 'Two Disciplines', 'small': [('EvasionPercent', 6), ('EnergyShieldPercent', 6)], 'notables': [
            ('Two Disciplines', [('Dexterity', 8), ('Magic', 8)], 'Eye and breath are taught in the same courtyard, by the same switch.'),
            ('Still Waters', [('EnergyShieldPercent', 15), ('EvasionPercent', 15)], 'Stand still enough and the blow has nowhere to land.'),
        ]},
    ],
    'Horadric Archive': [
        {'key': 'h_order', 'side': 0, 'band': 'mid', 'title': "The Order's Staff", 'small': [('StaffDamage', 5), ('ColdDamage', 3)], 'notables': [
            ("The Order's Staff", [('StaffDamage', 14), ('ColdDamage', 10)], 'A Horadric staff was a scholar\'s tool, until a demon came too close.'),
            ('Strike and Spell', [('StaffDamage', 12), ('CastSpeed', 5)], 'A blow of the staff, a word of power, and again.'),
            ('Horadric Vigil', [('ManaPercent', 8), ('LifePercent', 4)], 'Father to son, the Horadrim kept the watch. Then only Cain was left.'),
            ("Jered Cain's Watch", [('Magic', 10), ('ManaRegen', 15)], 'Jered Cain buried the soulstone in Khanduras and set the order to watch over it.'),
        ]},
        {'key': 'h_pages', 'side': 1, 'band': 'mid', 'title': 'Warded Pages', 'small': [('SpellDamage', 5)], 'notables': [
            ('Horadric Winter', [('ColdDamage', 14), ('CastSpeed', 4)], 'The Horadrim did not burn their demons. They froze them still, and took notes.'),
            ('Warded Pages', [('SpellDamage', 12), ('EnergyShieldPercent', 12)], 'Every book in the archive is warded. Some of the wards are on the reader.'),
            ('Captured Essence', [('ManaOnKill', 5), ('SpellDamage', 10)], 'What the soulstones did to demons, the order learned to do in small.'),
        ]},
    ],
}
for _area, _fams in CONTENT_PASS.items():
    (CLUSTERS if _area in CLUSTERS else BORDER_CLUSTERS)[_area].extend(_fams)

# Rule stats the content pass adds. The game learns them in stage 4 (Source/poe2/passive_tree.hpp and the rule
# code); until then they are only names in the TSV. Each one is a single number, explained here.
NEW_RULES = {
    'TwoHandKillFrenzy': 'kills with two-handed melee weapons grant a Frenzy Charge for 4 seconds, up to 2',
    'BlockRiposte': 'after you block, your next melee attack within 3 seconds deals N% more damage',
    'ElementRotation': 'spells deal N% more damage if your previous spell hit was a different element, for 3 seconds',
    'MinionSpellDamage': 'your minions get N% of your increased spell damage from all sources as increased damage, at most 40%',
    'StaffToLightning': 'N% of the physical damage of your staff attacks is converted to lightning',
    'StaffArc': 'a staff hit on a shocked enemy arcs to one other enemy nearby for N% of its damage',
    'UnarmedHitStacks': 'while unarmed, each hit gives a stack and refreshes them all for N seconds, up to 10: 2% more evasion each, and 10% increased attack speed per 5 stacks',
    'ArrowPierce': 'your arrows pierce N enemies',
    'SwapStrike': 'after you swap to a weapon set with a different weapon type, your next 3 hits within 4 seconds deal N% more damage; once every 4 seconds',
    'ArrowDamageMod': 'your arrows add N% of your character damage bonus, in place of 50%',
    'SpellDamageToFireAttacks': 'N% of your increased spell damage also applies to the fire damage of your attacks',
    'StaffAppliesToBow': 'staff damage passives also apply to bows, and bows take the staff attribute damage bonus',
    'Cull': 'your hits kill normal and magic monsters left at N% life or less',
    'FirstStrike': 'your first hit on an enemy at full life deals N% more damage',
    'KillBurst': 'an enemy you kill with a melee hit bursts for N% of its maximum life as physical damage to enemies next to it',
    'FlaskFrenzy': 'drinking a flask grants a Frenzy Charge for 4 seconds, up to 2',
    'MoreVsStunned': 'your hits deal N% more damage to stunned enemies',
}

# Capstones (design-spec §11): two per class, one per side; each changes how its side's family plays. Weaker than a
# shrine. Keyed (class, side).
CAPSTONES = {
    ('Warrior', 0): ('Fresh Meat', [('TwoHandKillFrenzy', 1), ('TwoHandDamage', 10)], "'Ah, fresh meat!'"),
    ('Warrior', 1): ("Lachdanan's Guard", [('BlockRiposte', 60), ('AttackSpeed', 4)], "'Lachdanan was an honorable and just man who served his King faithfully for years.'"),
    ('Sorcerer', 0): ('Wheel of the Elements', [('ElementRotation', 20), ('CastSpeed', 4)], 'Fire, then frost, then storm. Then fire again.'),
    ('Sorcerer', 1): ("Na-Krul's Jailer", [('MinionSpellDamage', 50), ('MinionLife', 10)], 'Spells, threats and bargains all failed on Na-Krul. Lesser demons bind more easily.'),
    ('Monk', 0): ('Staff of the Storm Gods', [('StaffToLightning', 50), ('ShockChance', 15), ('StaffArc', 30), ('LightningDamage', 10)], 'In Ivgorod every storm has its god, and every god keeps a staff.'),
    ('Monk', 1): ('Ceaseless Hands', [('UnarmedHitStacks', 2), ('MeleeDamage', 8)], 'Stop striking and you start thinking. Never stop.'),
    ('Rogue', 0): ('One Shaft, Many Fallen', [('ArrowPierce', 2), ('BowDamage', 10)], 'Two Fallen, one arrow. The Sisters call that thrift.'),
    ('Rogue', 1): ('Knife in the Dark', [('SwapStrike', 50), ('MeleeDamage', 8)], 'Let them watch the bow. The knife comes from the other hand.'),
}

# Crossover notables (design-spec §11), keyed (class, the border they face). The CROSSOVER_WEAPON one of each class
# opens a second weapon type; the other mixes the class with that border.
CROSSOVERS = {
    ('Warrior', 'Zakarum Cloister'): ('Sword of the Cloister', [('MeleeDamage', 10), ('FireDamage', 10)], "The cloister's knights carried their faith on the edge."),
    ('Warrior', 'Khanduras Muster'): ('Iron Draw', [('ArrowDamageMod', 65), ('BowDamage', 10)], 'The longbowmen of the levy drew with their backs, not their fingers.'),
    ('Sorcerer', 'Horadric Archive'): ('Staff and Scroll', [('SpellDamage', 10), ('StaffDamage', 10)], 'The Horadrim read with one hand and held the staff with the other.'),
    ('Sorcerer', 'Zakarum Cloister'): ('Fire in the Hand', [('PhysicalToFire', 25), ('SpellDamageToFireAttacks', 50)], 'The battle priests never saw why fire should stay in a spell.'),
    ('Rogue', 'Khanduras Muster'): ('Shoulder to Shoulder', [('BowDamage', 10), ('ArmorPercent', 12)], "In the winter after Diablo fell, levy shields and Sisters' bows held the roads of Khanduras together."),
    ('Rogue', 'Sightless Eye'): ('Eastern Teachers', [('StaffDamage', 14), ('AttackSpeed', 4)], 'The Sisters took their inner sight from Eastern teachings. The staff came with it.'),
    ('Monk', 'Horadric Archive'): ('Pilgrim of the Archive', [('MeleeDamage', 10), ('ColdDamage', 10)], 'Pilgrims who copied the Horadric books learned the cold spells in the margins.'),
    ('Monk', 'Sightless Eye'): ("The Sisters' Gift", [('StaffAppliesToBow', 1), ('BowDamage', 10)], 'At the monastery the Brotherhood borrowed the bow, and taught the staff in return.'),
}

# Profane notables (design-spec §7): the upside equals the nearby notables, the drawback hurts every build and scales
# with the road's saving (about -6% resistances or -5% life at 4 points, -4% / -3% at 2). Keyed by area; the name is
# PROFANE above, the lore line is followed in game by PROFANE_TOOLTIP.
PROFANE_CONTENT = {
    'Warrior': ([('TwoHandDamage', 15), ('Cull', 10), ('AllResist', -6)], 'In the end the King trusted only the axe.'),
    'Sorcerer': ([('SpellDamage', 15), ('CastSpeed', 6), ('LifeOnKill', 10), ('LifePercent', -5)], 'Bartuc bathed in the blood of his foes and called it strength.'),
    'Rogue': ([('MeleeDamage', 15), ('FirstStrike', 40), ('LifePercent', -5)], 'A voice in the dark promises a Sister everything she has ever wanted.'),
    'Monk': ([('MeleeDamage', 12), ('KillBurst', 10), ('LifePercent', -5)], "A dark sickness killed many in the Brotherhood's homeland. Lone brothers went searching for its like abroad."),
    'Zakarum Cloister': ([('FireDamage', 14), ('Spirit', 6), ('AllResist', -4)], "Lazarus led Tristram's men below to save the prince. Few came back up."),
    'Khanduras Muster': ([('FlaskRecovery', 15), ('FlaskFrenzy', 1), ('AllResist', -4)], 'Gharbad begged for his life and promised a gift. In the end it had to be taken from his corpse.'),
    'Sightless Eye': ([('Damage', 10), ('LifeOnKill', 8), ('LifePercent', -3)], "The Countess hoarded gold in her tower's cellars. Her servants are down there too."),
    'Horadric Archive': ([('SpellLevels', 1), ('AllResist', -5)], 'To hold Diablo, the hero of Tristram drove the stone into his own brow.'),
}


# Notables cut on purpose (balance content review, must-fix 1). The class areas have 39 notable slots each; with the
# content pass these were the weakest or most repeated. Check C2 wants every other content notable placed once.
CUT = {
    'Warrior': ['Reckless Swings', 'Bone Breaker', 'Second Wind', 'Singing Steel', 'Quick Guard', 'Burning Zeal',
                'Hammer and Anvil', 'Golden Elixir', 'The Undead Crown'],
    'Sorcerer': ['Static Field', 'Vizjerei Adept', 'Kindling', 'Blood-Bound', 'Sound Body', "The Master's Share",
                 'Ring of the Mystics'],
    'Rogue': ['The Needler', 'Blackoak Fletching', 'Keen Eyes', "Sisters' Vigil", "Hunter's Patience", 'Highland Frost', 'Slip Away',
              'Head of the Pack', 'Close Quarters', 'Thunderhead', 'Flamedart'],
    'Monk': ['Hundred Palms', 'Storm Hymn', "Patriarch's Discipline", 'Gleamsong', 'Thousand and One Gods', 'Swift Prayer',
             "Hearth God's Blessing", 'Breath and Blow'],
    # The design-v8 class roads run through the border ground, and the border areas are filled in matched fours, so
    # each border has room for three fewer notables (2026-10-18): the last three of each, from its outer families.
    'Zakarum Cloister': ['Sermon of Light', 'Litany of Mending', "Que-Hegan's Fall"],
    'Khanduras Muster': ['The March on Westmarch', "Leoric's Legion", 'Royal Crypt'],
    'Sightless Eye': ['Storm Rider', 'Celestial Aim', 'Eye of the Storm'],
    'Horadric Archive': ["Zoltun's Insight", 'Golem Binding', "Tal Rasha's Sacrifice"],
}

# Scales on the new tree's notables and cluster smalls (not travel, exits or shrines), rounded half up (balance content
# review, must-fix 3-4 and should-fix 1). The lattice carries life where today's spokes carried each class's defence;
# these put main defence back near today's. NOTABLE_ONLY scales touch notables only.
STAT_SCALE = {'ArmorPercent': 1.3, 'EnergyShieldPercent': 1.1, 'EvasionPercent': 1.15, 'MinionDamage': 0.9}
NOTABLE_SCALE = {'AttackSpeed': 1.25, 'CastSpeed': 1.25}

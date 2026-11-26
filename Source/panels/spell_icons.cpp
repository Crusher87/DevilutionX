#include "panels/spell_icons.hpp"

#include <cstdint>
#include <optional>

#include "engine/load_cel.hpp"
#include "engine/load_clx.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "game_mode.hpp"
#include "poe2/hd.hpp"

namespace devilution {

namespace {

#ifdef UNPACKED_MPQS
OptionalOwnedClxSpriteList LargeSpellIconsBackground;
OptionalOwnedClxSpriteList SmallSpellIconsBackground;
#endif

OptionalOwnedClxSpriteList SmallSpellIcons;
OptionalOwnedClxSpriteList LargeSpellIcons;

uint8_t SplTransTbl[256];

/** POE2 Mashup: the colours SetSpellTrans last set: an icon's HD art has its stone plate in each of them. */
SpellType SplTransType = SpellType::Skill;

/**
 * POE2 Mashup: tells the HD renderer an icon is about to be drawn (`drawn` false) and that it has been (true), so the
 * icon's painted picture can show over it (hd::RecordUiSpellIconHook). The game's own icon is always drawn.
 */
void NoteIconForHd(const Surface &out, Point position, ClxSprite sprite, uint8_t frame, bool large, bool drawn)
{
	if (hd::RecordUiSpellIconHook == nullptr)
		return;
	const Rectangle box { Point { position.x, position.y - sprite.height() + 1 }, Size { sprite.width(), sprite.height() } };
	hd::RecordUiSpellIconHook(out, box, frame, large, static_cast<uint8_t>(SplTransType), drawn);
}

/** Maps from SpellID to spelicon.cel frame number. */
const SpellIcon SpellITbl[] = {
	// clang-format off
/* SpellID::Null             */ SpellIcon::Empty,
/* SpellID::Firebolt         */ SpellIcon::Firebolt,
/* SpellID::Healing          */ SpellIcon::Healing,
/* SpellID::Lightning        */ SpellIcon::Lightning,
/* SpellID::Flash            */ SpellIcon::Flash,
/* SpellID::Identify         */ SpellIcon::Identify,
/* SpellID::FireWall         */ SpellIcon::FireWall,
/* SpellID::TownPortal       */ SpellIcon::TownPortal,
/* SpellID::StoneCurse       */ SpellIcon::StoneCurse,
/* SpellID::Infravision      */ SpellIcon::Infravision,
/* SpellID::Phasing          */ SpellIcon::Phasing,
/* SpellID::ManaShield       */ SpellIcon::ManaShield,
/* SpellID::Fireball         */ SpellIcon::Fireball,
/* SpellID::Guardian         */ SpellIcon::DoomSerpents,
/* SpellID::ChainLightning   */ SpellIcon::ChainLightning,
/* SpellID::FlameWave        */ SpellIcon::FlameWave,
/* SpellID::DoomSerpents     */ SpellIcon::DoomSerpents,
/* SpellID::BloodRitual      */ SpellIcon::BloodRitual,
/* SpellID::Nova             */ SpellIcon::Nova,
/* SpellID::Invisibility     */ SpellIcon::Invisibility,
/* SpellID::Inferno          */ SpellIcon::Inferno,
/* SpellID::Golem            */ SpellIcon::Golem,
/* SpellID::Rage             */ SpellIcon::BloodBoil,
/* SpellID::Teleport         */ SpellIcon::Teleport,
/* SpellID::Apocalypse       */ SpellIcon::Apocalypse,
/* SpellID::Etherealize      */ SpellIcon::Etherealize,
/* SpellID::ItemRepair       */ SpellIcon::ItemRepair,
/* SpellID::StaffRecharge    */ SpellIcon::StaffRecharge,
/* SpellID::TrapDisarm       */ SpellIcon::TrapDisarm,
/* SpellID::Elemental        */ SpellIcon::Elemental,
/* SpellID::ChargedBolt      */ SpellIcon::ChargedBolt,
/* SpellID::HolyBolt         */ SpellIcon::HolyBolt,
/* SpellID::Resurrect        */ SpellIcon::Resurrect,
/* SpellID::Telekinesis      */ SpellIcon::Telekinesis,
/* SpellID::HealOther        */ SpellIcon::HealOther,
/* SpellID::BloodStar        */ SpellIcon::BloodStar,
/* SpellID::BoneSpirit       */ SpellIcon::BoneSpirit,
/* SpellID::Mana             */ SpellIcon::Mana,
/* SpellID::Magi             */ SpellIcon::Mana,
/* SpellID::Jester           */ SpellIcon::Jester,
/* SpellID::LightningWall    */ SpellIcon::LightningWall,
/* SpellID::Immolation       */ SpellIcon::Immolation,
/* SpellID::Warp             */ SpellIcon::Warp,
/* SpellID::Reflect          */ SpellIcon::Reflect,
/* SpellID::Berserk          */ SpellIcon::Berserk,
/* SpellID::RingOfFire       */ SpellIcon::RingOfFire,
/* SpellID::Search           */ SpellIcon::Search,
/* SpellID::RuneOfFire       */ SpellIcon::PentaStar,
/* SpellID::RuneOfLight      */ SpellIcon::PentaStar,
/* SpellID::RuneOfNova       */ SpellIcon::PentaStar,
/* SpellID::RuneOfImmolation */ SpellIcon::PentaStar,
/* SpellID::RuneOfStone      */ SpellIcon::PentaStar,
	// clang-format on
};

} // namespace

std::expected<void, std::string> LoadLargeSpellIcons()
{
#ifdef UNPACKED_MPQS
	LargeSpellIcons = LoadOptionalClx("data\\spelicon_fg.clx");
	LargeSpellIconsBackground = LoadOptionalClx("data\\spelicon_bg.clx");
	if (!LargeSpellIcons.has_value() || !LargeSpellIconsBackground.has_value()) {
		ASSIGN_OR_RETURN(LargeSpellIcons, LoadClxWithStatus("ctrlpan\\spelicon_fg.clx"));
		ASSIGN_OR_RETURN(LargeSpellIconsBackground, LoadClxWithStatus("ctrlpan\\spelicon_bg.clx"));
	}
#else
	LargeSpellIcons = LoadOptionalCel("data\\spelicon", SPLICONLENGTH);
	if (!LargeSpellIcons.has_value()) {
		ASSIGN_OR_RETURN(LargeSpellIcons, LoadCelWithStatus("ctrlpan\\spelicon", SPLICONLENGTH));
	}
#endif
	SetSpellTrans(SpellType::Skill);
	return {};
}

void FreeLargeSpellIcons()
{
#ifdef UNPACKED_MPQS
	LargeSpellIconsBackground = std::nullopt;
#endif
	LargeSpellIcons = std::nullopt;
}

std::expected<void, std::string> LoadSmallSpellIcons()
{
#ifdef UNPACKED_MPQS
	ASSIGN_OR_RETURN(SmallSpellIcons, LoadClxWithStatus("data\\spelli2_fg.clx"));
	ASSIGN_OR_RETURN(SmallSpellIconsBackground, LoadClxWithStatus("data\\spelli2_bg.clx"));
#else
	ASSIGN_OR_RETURN(SmallSpellIcons, LoadCelWithStatus("data\\spelli2", 37));
#endif
	return {};
}

void FreeSmallSpellIcons()
{
#ifdef UNPACKED_MPQS
	SmallSpellIconsBackground = std::nullopt;
#endif
	SmallSpellIcons = std::nullopt;
}

uint8_t GetSpellIconFrame(SpellID spell)
{
	return static_cast<uint8_t>(SpellITbl[static_cast<int8_t>(spell)]);
}

void DrawLargeSpellIcon(const Surface &out, Point position, SpellID spell)
{
	const uint8_t frame = GetSpellIconFrame(spell);
	const ClxSprite sprite = (*LargeSpellIcons)[frame];
	NoteIconForHd(out, position, sprite, frame, /*large=*/true, /*drawn=*/false);
#ifdef UNPACKED_MPQS
	ClxDrawTRN(out, position, (*LargeSpellIconsBackground)[0], SplTransTbl);
#endif
	ClxDrawTRN(out, position, sprite, SplTransTbl);
	NoteIconForHd(out, position, sprite, frame, /*large=*/true, /*drawn=*/true);
}

void DrawSmallSpellIcon(const Surface &out, Point position, SpellID spell)
{
	DrawSmallSpellIcon(out, position, static_cast<SpellIcon>(GetSpellIconFrame(spell)));
}

void DrawSmallSpellIcon(const Surface &out, Point position, SpellIcon icon)
{
	const auto frame = static_cast<uint8_t>(icon);
	const ClxSprite sprite = (*SmallSpellIcons)[frame];
	NoteIconForHd(out, position, sprite, frame, /*large=*/false, /*drawn=*/false);
#ifdef UNPACKED_MPQS
	ClxDrawTRN(out, position, (*SmallSpellIconsBackground)[0], SplTransTbl);
#endif
	ClxDrawTRN(out, position, sprite, SplTransTbl);
	NoteIconForHd(out, position, sprite, frame, /*large=*/false, /*drawn=*/true);
}

void DrawLargeSpellIconBorder(const Surface &out, Point position, uint8_t color)
{
	const int width = (*LargeSpellIcons)[0].width();
	const int height = (*LargeSpellIcons)[0].height();
	const Rectangle box { Point { position.x, position.y - height + 1 }, Size { width, height } };
	UnsafeDrawBorder2px(out, box, color);
	// POE2 Mashup: the icon's HD art leaves the border out.
	if (hd::CoverUiSpellIconHook != nullptr)
		hd::CoverUiSpellIconHook(out, box);
}

void DrawSmallSpellIconBorder(const Surface &out, Point position)
{
	const int width = (*SmallSpellIcons)[0].width();
	const int height = (*SmallSpellIcons)[0].height();
	const Rectangle box { Point { position.x, position.y - height + 1 }, Size { width, height } };
	UnsafeDrawBorder2px(out, box, SplTransTbl[PAL8_YELLOW + 2]);
	// POE2 Mashup: the icon's HD art leaves the border out.
	if (hd::CoverUiSpellIconHook != nullptr)
		hd::CoverUiSpellIconHook(out, box);
}

void SetSpellTrans(SpellType t)
{
	SplTransType = t;
	if (t == SpellType::Skill) {
		for (int i = 0; i < 128; i++)
			SplTransTbl[i] = i;
	}
	for (int i = 128; i < 256; i++)
		SplTransTbl[i] = i;
	SplTransTbl[255] = 0;

	switch (t) {
	case SpellType::Spell:
		SplTransTbl[PAL8_YELLOW] = PAL16_BLUE + 1;
		SplTransTbl[PAL8_YELLOW + 1] = PAL16_BLUE + 3;
		SplTransTbl[PAL8_YELLOW + 2] = PAL16_BLUE + 5;
		for (int i = PAL16_BLUE; i < PAL16_BLUE + 16; i++) {
			SplTransTbl[PAL16_BEIGE - PAL16_BLUE + i] = i;
			SplTransTbl[PAL16_YELLOW - PAL16_BLUE + i] = i;
			SplTransTbl[PAL16_ORANGE - PAL16_BLUE + i] = i;
		}
		break;
	case SpellType::Scroll:
		SplTransTbl[PAL8_YELLOW] = PAL16_BEIGE + 1;
		SplTransTbl[PAL8_YELLOW + 1] = PAL16_BEIGE + 3;
		SplTransTbl[PAL8_YELLOW + 2] = PAL16_BEIGE + 5;
		for (int i = PAL16_BEIGE; i < PAL16_BEIGE + 16; i++) {
			SplTransTbl[PAL16_YELLOW - PAL16_BEIGE + i] = i;
			SplTransTbl[PAL16_ORANGE - PAL16_BEIGE + i] = i;
		}
		break;
	case SpellType::Charges:
		SplTransTbl[PAL8_YELLOW] = PAL16_ORANGE + 1;
		SplTransTbl[PAL8_YELLOW + 1] = PAL16_ORANGE + 3;
		SplTransTbl[PAL8_YELLOW + 2] = PAL16_ORANGE + 5;
		for (int i = PAL16_ORANGE; i < PAL16_ORANGE + 16; i++) {
			SplTransTbl[PAL16_BEIGE - PAL16_ORANGE + i] = i;
			SplTransTbl[PAL16_YELLOW - PAL16_ORANGE + i] = i;
		}
		break;
	case SpellType::Invalid:
		SplTransTbl[PAL8_YELLOW] = PAL16_GRAY + 1;
		SplTransTbl[PAL8_YELLOW + 1] = PAL16_GRAY + 3;
		SplTransTbl[PAL8_YELLOW + 2] = PAL16_GRAY + 5;
		for (int i = PAL16_GRAY; i < PAL16_GRAY + 15; i++) {
			SplTransTbl[PAL16_BEIGE - PAL16_GRAY + i] = i;
			SplTransTbl[PAL16_YELLOW - PAL16_GRAY + i] = i;
			SplTransTbl[PAL16_ORANGE - PAL16_GRAY + i] = i;
		}
		SplTransTbl[PAL16_BEIGE + 15] = 0;
		SplTransTbl[PAL16_YELLOW + 15] = 0;
		SplTransTbl[PAL16_ORANGE + 15] = 0;
		break;
	case SpellType::Skill:
		break;
	}
}

} // namespace devilution

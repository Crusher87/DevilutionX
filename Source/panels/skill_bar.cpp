#include "panels/skill_bar.hpp"

#include <optional>
#include <string>

#include "control/control.hpp"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "diablo.h"
#include "effects.h"
#include "engine/palette.h"
#include "engine/rectangle.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "options.h"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr Size SlotSize { 37, 38 };
constexpr int SlotGap = 2;
constexpr int BarWidth = (static_cast<int>(NumSkillSlots) * (SlotSize.width + SlotGap)) - SlotGap;

bool Overlaps(const Rectangle &a, const Rectangle &b)
{
	return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
	    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
}

/** @brief The bar sits just above the right end of the main panel, or the left end if the inventory covers that. */
Rectangle SkillBarRect()
{
	const Rectangle &panel = GetMainPanel();
	const int y = panel.position.y - SlotSize.height - 4;
	Rectangle bar { { panel.position.x + panel.size.width - BarWidth - 4, y }, { BarWidth, SlotSize.height } };
	if (IsRightPanelOpen() && Overlaps(bar, GetRightPanel()))
		bar.position.x = panel.position.x + 4;
	return bar;
}

Rectangle SlotRect(size_t slot)
{
	const Rectangle bar = SkillBarRect();
	return { { bar.position.x + (static_cast<int>(slot) * (SlotSize.width + SlotGap)), bar.position.y }, SlotSize };
}

std::optional<size_t> SlotAt(Point position)
{
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		if (SlotRect(slot).contains(position))
			return slot;
	}
	return std::nullopt;
}

std::string SlotKeyName(size_t slot)
{
	return std::string(GetOptions().Keymapper.KeyNameForAction(StrCat("SkillSlot", slot + 1)));
}

SpellIcon AttackIcon(WeaponSkill skill)
{
	switch (skill) {
	case WeaponSkill::Cleave:
		return SpellIcon::Berserk;
	case WeaponSkill::GroundSlam:
		return SpellIcon::PentaStar;
	case WeaponSkill::Barrage:
		return SpellIcon::Sentinel;
	case WeaponSkill::WhirlingStaff:
		return SpellIcon::Immolation;
	case WeaponSkill::Flurry:
		return SpellIcon::Jester;
	case WeaponSkill::LeapSlam:
		return SpellIcon::RedSkull;
	case WeaponSkill::RainOfArrows:
		return SpellIcon::FireCloud;
	default:
		return SpellIcon::Empty;
	}
}

std::string MissingRequirements(const Player &player, SkillSlot skill)
{
	const std::optional<size_t> gemIndex = SkillGemItemIndex(skill);
	if (!gemIndex)
		return {};
	const ItemData &gem = AllItemsList[*gemIndex];
	std::string text;
	const auto add = [&text](int needed, int have, std::string_view stat) {
		if (have >= needed)
			return;
		StrAppend(text, text.empty() ? "" : ", ", FormatRuntime(_("{:d} {:s}"), needed, stat));
	};
	add(gem.iMinStr, player._pStrength, _("Strength"));
	add(gem.iMinMag, player._pMagic, _("Magic"));
	add(gem.iMinDex, player._pDexterity, _("Dexterity"));
	return FormatRuntime(_("Needs {:s}"), text);
}

void PlayGemSound(const Item &gem)
{
	PlaySFX(ItemInvSnds[ItemCAnimTbl[gem._iCurs]]);
}

void HoldItem(Player &player, Item &&item)
{
	player.HoldItem = std::move(item);
	NewCursor(player.HoldItem);
}

void SocketHeldSkillGem(Player &player, size_t slot)
{
	const SkillSlot skill = SkillForGem(player.HoldItem);
	if (const std::optional<size_t> existing = FindSkillSlot(player, skill); existing && *existing != slot) {
		EventPlrMsg(FormatRuntime(_("{:s} is already in the skill bar."), SkillName(skill)), UiFlags::ColorRed);
		return;
	}

	PlayGemSound(player.HoldItem);
	const SkillSlot previous = SocketSkill(player, slot, skill);
	player.HoldItem.clear();
	Item returned;
	if (!previous.isEmpty() && CreateSkillGemItem(returned, previous))
		HoldItem(player, std::move(returned));
	else
		NewCursor(CURSOR_HAND);
}

void LinkHeldSupportGem(Player &player, size_t slot)
{
	const SupportGem gem = SupportGemForItem(player.HoldItem);
	const SkillSlot skill = player.poe2.skillSlots[slot];
	const SpellID spell = skill.spell();
	SupportGem replaced = SupportGem::None;
	switch (LinkSupportGem(player.poe2, spell, gem, replaced)) {
	case SupportLinkResult::NoReadiedSpell:
	case SupportLinkResult::NotApplicable:
		EventPlrMsg(skill.isEmpty() ? std::string(_("Socket a skill gem in this slot first."))
		                            : FormatRuntime(_("{:s} can't support {:s}."), SupportGemName(gem), SkillName(skill)),
		    UiFlags::ColorRed);
		return;
	case SupportLinkResult::AlreadyLinked:
		EventPlrMsg(FormatRuntime(_("{:s} is already linked to {:s}."), SupportGemName(gem), SkillName(skill)), UiFlags::ColorRed);
		return;
	case SupportLinkResult::Linked:
	case SupportLinkResult::Replaced:
		break;
	}

	PlayGemSound(player.HoldItem);
	player.HoldItem.clear();
	EventPlrMsg(FormatRuntime(_("{:s} linked to {:s}."), SupportGemName(gem), SkillName(skill)));
	Item returned;
	if (replaced != SupportGem::None && CreateSupportGemItem(returned, replaced))
		HoldItem(player, std::move(returned));
	else
		NewCursor(CURSOR_HAND);
}

} // namespace

bool IsSkillBarShown()
{
	// The speedbook opens in the same spot, so hide the bar while it's up.
	return gbIsPoe2 && MyPlayer != nullptr && !SpellSelectFlag;
}

bool IsSkillBarHovered()
{
	return IsSkillBarShown() && ControlMode == ControlTypes::KeyboardAndMouse && SkillBarRect().contains(MousePosition);
}

void DrawSkillBar(const Surface &out)
{
	if (!IsSkillBarShown())
		return;

	const Player &player = *MyPlayer;
	const std::optional<size_t> hovered = IsSkillBarHovered() ? SlotAt(MousePosition) : std::nullopt;
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		const Rectangle rect = SlotRect(slot);
		const Point bottomLeft { rect.position.x, rect.position.y + rect.size.height - 1 };
		const SkillSlot skill = player.poe2.skillSlots[slot];

		DrawHalfTransparentRectTo(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height);
		if (!skill.isEmpty()) {
			const bool ready = GetSkillUsability(player, skill) == SkillUsability::Ready;
			SetSpellTrans(!ready ? SpellType::Invalid : skill.kind == SkillKind::Spell ? SpellType::Spell
			                                                                           : SpellType::Skill);
			if (skill.kind == SkillKind::Spell)
				DrawSmallSpellIcon(out, bottomLeft, skill.spell());
			else
				DrawSmallSpellIcon(out, bottomLeft, AttackIcon(skill.attack()));
		}
		if (hovered == slot)
			DrawSmallSpellIconBorder(out, bottomLeft);

		const std::string key = SlotKeyName(slot);
		if (key.length() <= 3)
			DrawString(out, key, { rect.position + Displacement { 2, 1 }, { rect.size.width - 4, 12 } }, { .flags = UiFlags::ColorWhite | UiFlags::Outlined });
	}
}

void SetSkillBarInfo()
{
	const std::optional<size_t> slot = SlotAt(MousePosition);
	if (!slot)
		return;

	const Player &player = *MyPlayer;
	const SkillSlot skill = player.poe2.skillSlots[*slot];
	const std::string key = SlotKeyName(*slot);
	InfoString = StringOrView {};
	if (skill.isEmpty()) {
		InfoColor = UiFlags::ColorWhite;
		InfoString = FormatRuntime(_("Empty skill slot [{:s}]"), key);
		AddInfoBoxString(_("Right-click a skill gem to socket it"));
		return;
	}

	InfoColor = skill.kind == SkillKind::Spell ? UiFlags::ColorBlue : UiFlags::ColorWhitegold;
	InfoString = FormatRuntime(_("{:s} [{:s}]"), SkillName(skill), key);
	if (skill.kind == SkillKind::Spell) {
		AddInfoBoxString(FormatRuntime(_("Spell level {:d}, {:d} mana"), player.GetSpellLevel(skill.spell()), SkillManaCost(player, skill)));
		for (const SupportGem gem : player.poe2.supportsFor(skill.spell())) {
			if (gem != SupportGem::None)
				AddInfoBoxString(FormatRuntime(_("Support: {:s}"), SupportGemName(gem)));
		}
	} else {
		AddInfoBoxString(WeaponSkillDescription(skill.attack()));
		AddInfoBoxString(FormatRuntime(_("{:d} mana, needs a {:s}"), SkillManaCost(player, skill), WeaponSkillWeapons(skill.attack())));
	}

	switch (GetSkillUsability(player, skill)) {
	case SkillUsability::Requirements:
		InfoColor = UiFlags::ColorRed;
		AddInfoBoxString(MissingRequirements(player, skill));
		break;
	case SkillUsability::Weapon:
		InfoColor = UiFlags::ColorRed;
		break;
	default:
		if (invflag)
			AddInfoBoxString(_("Click to take out the gem"));
		break;
	}
}

bool HandleSkillBarClick(Point mousePosition)
{
	if (!IsSkillBarShown() || !SkillBarRect().contains(mousePosition))
		return false;
	const std::optional<size_t> slot = SlotAt(mousePosition);
	if (!slot)
		return true;

	Player &player = *MyPlayer;
	if (!player.HoldItem.isEmpty()) {
		if (IsSkillGem(player.HoldItem))
			SocketHeldSkillGem(player, *slot);
		else if (SupportGemForItem(player.HoldItem) != SupportGem::None)
			LinkHeldSupportGem(player, *slot);
		else
			EventPlrMsg(_("Only skill gems and support gems go in the skill bar."), UiFlags::ColorRed);
		return true;
	}

	const SkillSlot skill = player.poe2.skillSlots[*slot];
	if (invflag && !skill.isEmpty()) {
		Item gem;
		if (CreateSkillGemItem(gem, skill)) {
			SocketSkill(player, *slot, {});
			PlayGemSound(gem);
			HoldItem(player, std::move(gem));
		}
	}
	return true;
}

void UseSkillSlot(size_t slot)
{
	if (!IsSkillBarShown() || slot >= NumSkillSlots)
		return;

	Player &player = *MyPlayer;
	const SkillSlot skill = player.poe2.skillSlots[slot];
	switch (GetSkillUsability(player, skill)) {
	case SkillUsability::Empty:
		EventPlrMsg(_("No skill gem in that slot. Right-click a skill gem to socket it."), UiFlags::ColorRed);
		return;
	case SkillUsability::Requirements:
		EventPlrMsg(FormatRuntime(_("{:s}: {:s}"), SkillName(skill), MissingRequirements(player, skill)), UiFlags::ColorRed);
		return;
	default:
		break;
	}

	if (skill.kind == SkillKind::Attack) {
		TryUseWeaponSkill(player, skill.attack());
		return;
	}

	const SpellID spell = skill.spell();
	const PlayerActionType previousAction = LastPlayerAction;
	if (ControlMode != ControlTypes::KeyboardAndMouse)
		UpdateSpellTarget(spell);
	CheckPlrSpell(false, spell, SpellType::Spell);
	LastPlayerAction = previousAction;
}

bool SocketSkillGemFromInventory(Player &player, bool fromBelt, int index)
{
	const Item &gem = fromBelt ? player.SpdList[index] : player.InvList[index];
	const SkillSlot skill = SkillForGem(gem);
	if (FindSkillSlot(player, skill)) {
		EventPlrMsg(FormatRuntime(_("{:s} is already in the skill bar."), SkillName(skill)), UiFlags::ColorRed);
		return true;
	}
	const std::optional<size_t> slot = FirstFreeSkillSlot(player);
	if (!slot) {
		EventPlrMsg(_("The skill bar is full. Pick up the gem and click a slot to swap it in."), UiFlags::ColorRed);
		return false;
	}

	PlayGemSound(gem);
	SocketSkill(player, *slot, skill);
	EventPlrMsg(FormatRuntime(_("{:s} socketed. Use it with {:s}."), SkillName(skill), SlotKeyName(*slot)));
	if (fromBelt)
		player.RemoveSpdBarItem(index);
	else
		player.RemoveInvItem(index);
	return true;
}

} // namespace devilution

#include "panels/skill_bar.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

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
#include "panels/item_spawner.hpp"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
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
	case WeaponSkill::ShieldCharge:
		return SpellIcon::Reflect;
	case WeaponSkill::BattleCry:
		return SpellIcon::LongHorn;
	case WeaponSkill::LightningArrow:
		return SpellIcon::Elemental;
	case WeaponSkill::EscapeShot:
		return SpellIcon::Phasing;
	case WeaponSkill::EmberFusillade:
		return SpellIcon::BloodStar;
	case WeaponSkill::Spark:
		return SpellIcon::LightningWall;
	default:
		return SpellIcon::Empty;
	}
}

SpellIcon PersistentIcon(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
		return SpellIcon::FlameWave;
	case PersistentSkill::HeraldOfThunder:
		return SpellIcon::ChainLightning;
	case PersistentSkill::PurityOfElements:
		return SpellIcon::Pentagram;
	case PersistentSkill::SummonGolem:
		return SpellIcon::Golem;
	case PersistentSkill::SkeletalWarriors:
		return SpellIcon::BoneSpirit;
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

} // namespace

void GiveSupportGems(Player &player, const std::vector<SupportGem> &gems)
{
	for (const SupportGem gem : gems) {
		Item item;
		if (CreateSupportGemItem(item, gem))
			GiveItemToPlayer(player, item);
	}
}

SkillSlot ChangeSlotSkill(Player &player, size_t slot, SkillSlot skill)
{
	// Supports stay socketed if the new skill can use them, like moving them over in POE2; the rest come back out.
	std::vector<SupportGem> returned;
	for (SupportGem &gem : player.poe2.slotSupports[slot]) {
		if (gem != SupportGem::None && !SupportGemCanSupport(gem, skill))
			returned.push_back(std::exchange(gem, SupportGem::None));
	}
	const SkillSlot previous = SocketSkill(player, slot, skill);
	GiveSupportGems(player, returned);
	return previous;
}

std::string SupportSocketMessage(SupportSocketResult result, const Player &player, size_t slot, SupportGem gem)
{
	const SkillSlot skill = player.poe2.skillSlots[slot];
	switch (result) {
	case SupportSocketResult::NoSkill:
		return std::string(_("Socket a skill gem in this slot first."));
	case SupportSocketResult::NotApplicable:
		return FormatRuntime(_("{:s} can't support {:s}."), SupportGemName(gem), SkillName(skill));
	case SupportSocketResult::AlreadySocketed:
		return FormatRuntime(_("{:s} already supports {:s}."), SupportGemName(gem), SkillName(skill));
	case SupportSocketResult::Locked:
		return std::string(_("That socket opens at a higher level."));
	case SupportSocketResult::NoFreeSocket:
		return FormatRuntime(_("{:s} has no free support socket."), SkillName(skill));
	case SupportSocketResult::NoCapacity: {
		const GemColor color = SupportGemColor(gem);
		return FormatRuntime(_("Not enough {:s} for another {:s} support ({:d}/{:d})."), GemColorName(color), GemColorName(color), SupportsUsed(player.poe2, color), SupportCapacity(player, color));
	}
	default:
		return FormatRuntime(_("{:s} now supports {:s}."), SupportGemName(gem), SkillName(skill));
	}
}

bool SocketHeldSupportGem(Player &player, size_t slot, std::optional<size_t> socket)
{
	const SupportGem gem = SupportGemForItem(player.HoldItem);
	SupportGem replaced = SupportGem::None;
	const SupportSocketResult result = SocketSupportGem(player, slot, socket, gem, replaced);
	if (!IsAnyOf(result, SupportSocketResult::Socketed, SupportSocketResult::Replaced)) {
		EventPlrMsg(SupportSocketMessage(result, player, slot, gem), UiFlags::ColorRed);
		return false;
	}

	PlayGemSound(player.HoldItem);
	player.HoldItem.clear();
	EventPlrMsg(SupportSocketMessage(result, player, slot, gem));
	Item returned;
	if (replaced != SupportGem::None && CreateSupportGemItem(returned, replaced))
		HoldItem(player, std::move(returned));
	else
		NewCursor(CURSOR_HAND);
	return true;
}

namespace {

void SocketHeldSkillGem(Player &player, size_t slot)
{
	const SkillSlot skill = SkillForGem(player.HoldItem);
	if (const std::optional<size_t> existing = FindSkillSlot(player, skill); existing && *existing != slot) {
		EventPlrMsg(FormatRuntime(_("{:s} is already in the skill bar."), SkillName(skill)), UiFlags::ColorRed);
		return;
	}

	PlayGemSound(player.HoldItem);
	const SkillSlot previous = ChangeSlotSkill(player, slot, skill);
	player.HoldItem.clear();
	Item returned;
	if (!previous.isEmpty() && CreateSkillGemItem(returned, previous))
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

/** @brief Whether the bar is drawn: it hides when both side panels cover its spots. */
bool IsSkillBarVisible()
{
	return IsSkillBarShown() && !(IsLeftPanelOpen() && Overlaps(SkillBarRect(), GetLeftPanel()));
}

bool IsSkillBarHovered()
{
	return IsSkillBarVisible() && ControlMode == ControlTypes::KeyboardAndMouse && SkillBarRect().contains(MousePosition);
}

void DrawSkillBar(const Surface &out)
{
	if (!IsSkillBarVisible())
		return;

	const Player &player = *MyPlayer;
	const std::optional<size_t> hovered = IsSkillBarHovered() ? SlotAt(MousePosition) : std::nullopt;
	for (size_t slot = 0; slot < NumSkillSlots; slot++) {
		const Rectangle rect = SlotRect(slot);
		const Point bottomLeft { rect.position.x, rect.position.y + rect.size.height - 1 };
		const SkillSlot skill = player.poe2.skillSlots[slot];

		DrawHalfTransparentRectTo(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height);
		DrawSkillIcon(out, bottomLeft, skill, GetSkillUsability(player, skill) == SkillUsability::Ready);
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
	} else if (skill.kind == SkillKind::Persistent) {
		AddInfoBoxString(PersistentSkillDescription(skill.persistent()));
		AddInfoBoxString(FormatRuntime(_("Reserves {:d} Spirit ({:d}/{:d} reserved)"), SpiritReservation(skill.persistent()), ReservedSpirit(player), MaxSpirit(player)));
	} else {
		AddInfoBoxString(WeaponSkillDescription(skill.attack()));
		if (WeaponSkillNeedsWeapon(skill.attack()))
			AddInfoBoxString(FormatRuntime(_("{:d} mana, needs a {:s}"), SkillManaCost(player, skill), WeaponSkillWeapons(skill.attack())));
		else
			AddInfoBoxString(FormatRuntime(_("{:d} mana"), SkillManaCost(player, skill)));
	}
	for (const SupportGem gem : player.poe2.slotSupports[*slot]) {
		if (gem != SupportGem::None)
			AddInfoBoxString(FormatRuntime(_("Support: {:s}"), SupportGemName(gem)));
	}

	switch (GetSkillUsability(player, skill)) {
	case SkillUsability::Requirements:
		InfoColor = UiFlags::ColorRed;
		AddInfoBoxString(MissingRequirements(player, skill));
		break;
	case SkillUsability::Weapon:
		InfoColor = UiFlags::ColorRed;
		break;
	case SkillUsability::Spirit:
		InfoColor = UiFlags::ColorRed;
		AddInfoBoxString(_("Not enough Spirit: inactive"));
		break;
	default:
		if (invflag)
			AddInfoBoxString(_("Click to take out the gem"));
		break;
	}
}

bool HandleSkillBarClick(Point mousePosition)
{
	if (!IsSkillBarVisible() || !SkillBarRect().contains(mousePosition))
		return false;
	const std::optional<size_t> slot = SlotAt(mousePosition);
	if (!slot)
		return true;

	ClickSkillSlot(*MyPlayer, *slot, invflag);
	return true;
}

void ClickSkillSlot(Player &player, size_t slot, bool canTakeOut)
{
	if (!player.HoldItem.isEmpty()) {
		if (IsSkillGem(player.HoldItem))
			SocketHeldSkillGem(player, slot);
		else if (SupportGemForItem(player.HoldItem) != SupportGem::None)
			SocketHeldSupportGem(player, slot, std::nullopt);
		else
			EventPlrMsg(_("Only skill gems and support gems go in the skill bar."), UiFlags::ColorRed);
		return;
	}

	const SkillSlot skill = player.poe2.skillSlots[slot];
	if (canTakeOut && !skill.isEmpty()) {
		Item gem;
		if (CreateSkillGemItem(gem, skill)) {
			ChangeSlotSkill(player, slot, {});
			PlayGemSound(gem);
			HoldItem(player, std::move(gem));
		}
	}
}

void DrawSkillIcon(const Surface &out, Point bottomLeft, SkillSlot skill, bool ready)
{
	if (skill.isEmpty())
		return;
	SetSpellTrans(!ready ? SpellType::Invalid : skill.kind == SkillKind::Spell ? SpellType::Spell
	                                                                           : SpellType::Skill);
	if (skill.kind == SkillKind::Spell)
		DrawSmallSpellIcon(out, bottomLeft, skill.spell());
	else if (skill.kind == SkillKind::Persistent)
		DrawSmallSpellIcon(out, bottomLeft, PersistentIcon(skill.persistent()));
	else
		DrawSmallSpellIcon(out, bottomLeft, AttackIcon(skill.attack()));
}

std::string SkillSlotKeyName(size_t slot)
{
	return SlotKeyName(slot);
}

std::string MissingSkillRequirements(const Player &player, SkillSlot skill)
{
	return MissingRequirements(player, skill);
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

	if (skill.kind == SkillKind::Persistent) {
		if (GetSkillUsability(player, skill) == SkillUsability::Spirit)
			EventPlrMsg(FormatRuntime(_("{:s} needs {:d} Spirit; you have {:d} left."), SkillName(skill), SpiritReservation(skill.persistent()), MaxSpirit(player) - ReservedSpirit(player)), UiFlags::ColorRed);
		else
			EventPlrMsg(FormatRuntime(_("{:s} is always active while socketed."), SkillName(skill)));
		return;
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
	ChangeSlotSkill(player, *slot, skill);
	EventPlrMsg(FormatRuntime(_("{:s} socketed. Use it with {:s}."), SkillName(skill), SlotKeyName(*slot)));
	if (fromBelt)
		player.RemoveSpdBarItem(index);
	else
		player.RemoveInvItem(index);
	return true;
}

} // namespace devilution

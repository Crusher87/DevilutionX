#include "poe2/item_search.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#else
#include <SDL.h>
#endif

#include <ankerl/unordered_dense.h>

#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "diablo.h"
#include "engine/dx.h"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "items.h"
#include "panels/poe2_widgets.hpp"
#include "tables/itemdat.h"
#include "utils/display.h"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

ItemSearchBox StashSearch;
ItemSearchBox ShopSearch;

namespace {

std::string_view RarityWord(item_quality quality)
{
	switch (quality) {
	case ITEM_QUALITY_MAGIC: return "magic";
	case ITEM_QUALITY_RARE: return "rare";
	case ITEM_QUALITY_UNIQUE: return "unique";
	default: return "normal";
	}
}

std::string_view KindWords(ItemType type)
{
	switch (type) {
	case ItemType::Sword: return "sword weapon";
	case ItemType::Axe: return "axe weapon";
	case ItemType::Bow: return "bow weapon";
	case ItemType::Mace: return "mace weapon";
	case ItemType::Staff: return "staff weapon";
	case ItemType::Shield: return "shield";
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor: return "body armour armor";
	case ItemType::Helm: return "helmet helm";
	case ItemType::Ring: return "ring jewellery jewelry";
	case ItemType::Amulet: return "amulet jewellery jewelry";
	case ItemType::Gold: return "gold";
	default: return "";
	}
}

/** @brief A fingerprint of everything that changes an item's tooltip, to reuse its search text. */
uint64_t SearchKey(const Item &item)
{
	uint64_t hash = 14695981039346656037ULL;
	const auto mix = [&hash](const void *data, size_t size) {
		const auto *bytes = static_cast<const unsigned char *>(data);
		for (size_t i = 0; i < size; i++) {
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
	};
	mix(item._iIName, std::strlen(item._iIName));
	mix(&item._iSeed, sizeof(item._iSeed));
	mix(&item._iCreateInfo, sizeof(item._iCreateInfo));
	mix(&item.IDidx, sizeof(item.IDidx));
	mix(&item._iIdentified, sizeof(item._iIdentified));
	mix(&item._iMagical, sizeof(item._iMagical));
	mix(&item.dwBuff, sizeof(item.dwBuff));
	mix(&item._ivalue, sizeof(item._ivalue));
	mix(item._iPoe2Stats.data(), sizeof(item._iPoe2Stats));
	for (const Poe2ItemMod &mod : item._iPoe2Mods)
		mix(&mod, sizeof(mod));
	return hash;
}

ankerl::unordered_dense::map<uint64_t, std::string> SearchTextCache;
constexpr size_t MaxCachedSearchTexts = 4096;

const std::string &CachedSearchText(const Item &item)
{
	const uint64_t key = SearchKey(item);
	if (const auto it = SearchTextCache.find(key); it != SearchTextCache.end())
		return it->second;
	if (SearchTextCache.size() >= MaxCachedSearchTexts)
		SearchTextCache.clear();
	return SearchTextCache.emplace(key, ItemSearchText(item)).first->second;
}

} // namespace

std::vector<ItemSearchTerm> ParseItemSearch(std::string_view query)
{
	std::vector<ItemSearchTerm> terms;
	std::string word;
	bool quoted = false;
	const auto finish = [&]() {
		if (word.empty())
			return;
		ItemSearchTerm term;
		std::string_view rest = word;
		if (rest.front() == '!') {
			term.negated = true;
			rest.remove_prefix(1);
		}
		while (!rest.empty()) {
			const size_t bar = rest.find('|');
			const std::string_view alternative = rest.substr(0, bar);
			if (!alternative.empty())
				term.alternatives.push_back(AsciiStrToLower(alternative));
			if (bar == std::string_view::npos)
				break;
			rest.remove_prefix(bar + 1);
		}
		if (!term.alternatives.empty())
			terms.push_back(std::move(term));
		word.clear();
	};
	for (const char c : query) {
		if (c == '"') {
			quoted = !quoted;
			continue;
		}
		if (c == ' ' && !quoted) {
			finish();
			continue;
		}
		word += c;
	}
	finish();
	return terms;
}

bool ItemSearchTextMatches(std::string_view text, const std::vector<ItemSearchTerm> &terms)
{
	for (const ItemSearchTerm &term : terms) {
		const bool found = std::any_of(term.alternatives.begin(), term.alternatives.end(),
		    [text](const std::string &alternative) { return text.find(alternative) != std::string_view::npos; });
		if (found == term.negated)
			return false;
	}
	return true;
}

std::string ItemSearchText(const Item &item)
{
	std::string text = StrCat(item.getName().str(), "\n", item._iName, "\n", AllItemsList[item.IDidx].iName, "\n", RarityWord(item._iMagical), " ", KindWords(item._itype));
	if (!item._iIdentified && item._iMagical != ITEM_QUALITY_NORMAL)
		StrAppend(text, " unidentified");

	// The tooltip's own lines, written into the info box and taken back out, so the box is left as it was.
	StringOrView savedInfo = std::move(InfoString);
	StringOrView savedFloating = std::move(FloatingInfoString);
	std::vector<InfoLineStyle> savedStyles = std::move(FloatingInfoStyles);
	const InfoLineStyle savedStyle = CurrentInfoLineStyle;
	InfoString = StringOrView {};
	FloatingInfoString = StringOrView {};
	FloatingInfoStyles.clear();
	if (item._iIdentified)
		PrintItemDetails(item);
	else
		PrintItemDur(item);
	StrAppend(text, "\n", InfoString.str(), "\n", FloatingInfoString.str());
	InfoString = std::move(savedInfo);
	FloatingInfoString = std::move(savedFloating);
	FloatingInfoStyles = std::move(savedStyles);
	CurrentInfoLineStyle = savedStyle;

	AsciiStrToLower(text);
	return text;
}

ItemSearchBox::ItemSearchBox()
    : input_(TextInputState::Options {
          .value = buffer_,
          .cursor = &cursor_,
          .maxLength = sizeof(buffer_) - 1,
      })
{
}

std::string_view ItemSearchBox::query() const
{
	return input_.value();
}

bool ItemSearchBox::searching() const
{
	return query().find_first_not_of(" \"!|") != std::string_view::npos;
}

void ItemSearchBox::setQuery(std::string_view query)
{
	input_.assign(query);
}

void ItemSearchBox::focus()
{
	if (focused_)
		return;
	focused_ = true;
	ClearWasdMovementKeys();
	SDLC_StartTextInput(ghMainWnd);
}

void ItemSearchBox::unfocus()
{
	if (!focused_)
		return;
	focused_ = false;
	SDLC_StopTextInput(ghMainWnd);
}

bool ItemSearchBox::handleEvent(const SDL_Event &event, const Rectangle &box)
{
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (box.contains(MousePosition)) {
			if (event.button.button == SDL_BUTTON_RIGHT)
				input_.assign("");
			else
				focus();
			return true;
		}
		// A click elsewhere stops typing and goes on to whatever was clicked.
		unfocus();
		return false;
	}
	if (!focused_) {
		if (event.type == SDL_EVENT_KEY_DOWN && SDLC_EventKey(event) == SDLK_F && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
			focus();
			return true;
		}
		return false;
	}
	if (HandleTextInputEvent(event, input_))
		return true;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		switch (SDLC_EventKey(event)) {
		case SDLK_ESCAPE:
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			unfocus();
			break;
		default:
			break;
		}
		return true;
	case SDL_EVENT_KEY_UP:
#ifndef USE_SDL1
	case SDL_EVENT_TEXT_INPUT:
#endif
		return true;
	default:
		return false;
	}
}

void ItemSearchBox::draw(const Surface &out, const Rectangle &box) const
{
	poe2ui::FillRectangle(out, box, poe2ui::RowColor);
	poe2ui::DrawFrame(out, box, focused_ ? poe2ui::HoverColor : poe2ui::BorderColor);
	const Rectangle text { box.position + Displacement { 6, 0 }, { box.size.width - 12, box.size.height } };
	if (query().empty() && !focused_) {
		DrawString(out, _("Search (Ctrl+F): name, mod or rarity"), text, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
		return;
	}
	TextRenderOptions options { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite };
	if (focused_)
		options.cursorPosition = static_cast<int>(cursor_.position);
	DrawString(out, query(), text, options);
}

bool ItemSearchBox::matches(const Item &item) const
{
	if (!searching())
		return true;
	if (parsedQuery_ != query()) {
		parsedQuery_ = std::string(query());
		terms_ = ParseItemSearch(parsedQuery_);
	}
	return ItemSearchTextMatches(CachedSearchText(item), terms_);
}

void DrawItemSearchMark(const Surface &out, const Rectangle &rect, bool matched)
{
	if (matched) {
		poe2ui::DrawFrame(out, rect, PAL16_YELLOW + 2);
		return;
	}
	// Twice over: three quarters dark, so the matches stand out but the rest can still be made out.
	DrawHalfTransparentRectTo(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height);
	DrawHalfTransparentRectTo(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height);
}

} // namespace devilution

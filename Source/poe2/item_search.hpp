/**
 * @file poe2/item_search.hpp
 *
 * POE2 Mashup item search: the search box at the bottom of the shared stash and the shops. Typing highlights the
 * items that match and dims the rest, like POE2's stash and vendor search (Curtis 2026-10-19).
 *
 * A search is words separated by spaces, and an item matches when its name, base, rarity or any line of its
 * tooltip holds every word. "Quotes" keep words together, a|b matches either, and !word leaves out items with it.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/text_input.hpp"
#include "engine/rectangle.hpp"
#include "engine/surface.hpp"

namespace devilution {

struct Item;

/** @brief One word of a search: any of its alternatives must appear, or none of them when it's negated. */
struct ItemSearchTerm {
	std::vector<std::string> alternatives;
	bool negated = false;
};

/** @brief Splits a search into its words, lowercased. */
std::vector<ItemSearchTerm> ParseItemSearch(std::string_view query);
/** @brief Whether lowercased item text holds every word of the search. */
bool ItemSearchTextMatches(std::string_view text, const std::vector<ItemSearchTerm> &terms);
/** @brief Everything a search looks at for an item, lowercased: names, base, rarity, kind and tooltip lines. */
std::string ItemSearchText(const Item &item);

/** @brief A search box: what's typed, and whether the keyboard goes to it. */
class ItemSearchBox {
public:
	ItemSearchBox();
	ItemSearchBox(const ItemSearchBox &) = delete;
	ItemSearchBox &operator=(const ItemSearchBox &) = delete;

	[[nodiscard]] std::string_view query() const;
	/** @brief Whether anything is being searched for, so items get highlighted or dimmed. */
	[[nodiscard]] bool searching() const;
	[[nodiscard]] bool focused() const { return focused_; }
	void setQuery(std::string_view query);
	void focus();
	void unfocus();

	/**
	 * @brief Handles an event for the box drawn at this rectangle: a click on it starts typing, a right-click
	 * clears it, Ctrl+F starts typing, and while typing every key goes to it. Returns true when the event was used.
	 */
	bool handleEvent(const SDL_Event &event, const Rectangle &box);
	void draw(const Surface &out, const Rectangle &box) const;
	/** @brief Whether an item matches; every item matches an empty search. */
	[[nodiscard]] bool matches(const Item &item) const;

private:
	char buffer_[64] {};
	TextInputCursorState cursor_;
	TextInputState input_;
	bool focused_ = false;
	mutable std::string parsedQuery_;
	mutable std::vector<ItemSearchTerm> terms_;
};

/** @brief The search box of the shared stash. */
extern ItemSearchBox StashSearch;
/** @brief The search box of the shops. */
extern ItemSearchBox ShopSearch;

/**
 * @brief Marks an item drawn in this rectangle while something is searched for: a bright frame when it matches,
 * darkened when it doesn't.
 */
void DrawItemSearchMark(const Surface &out, const Rectangle &rect, bool matched);

} // namespace devilution

#include <cstdint>
#include <initializer_list>

#include <gtest/gtest.h>

#include "engine/surface.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/rune_icons.hpp"
#include "utils/sdl_compat.h"

namespace devilution {
namespace {

constexpr int Size = 48;
/** @brief A one-cell inventory box, as DrawItem gives a rune. */
const Rectangle Box { { 10, 10 }, { 28, 28 } };

uint8_t At(const OwnedSurface &surface, int x, int y)
{
	if (x < 0 || y < 0 || x >= Size || y >= Size)
		return 0;
	return static_cast<const uint8_t *>(surface.surface->pixels)[static_cast<ptrdiff_t>(y) * surface.surface->pitch + x];
}

bool Touches(const OwnedSurface &surface, int x, int y)
{
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if ((dx != 0 || dy != 0) && At(surface, x + dx, y + dy) != 0)
				return true;
		}
	}
	return false;
}

TEST(Poe2RuneOutlineTest, RingGoesRoundTheStoneAndItsGlow)
{
	// No glow, one ring of glow, and two.
	for (const AugmentId id : { AugmentId::LesserRal, AugmentId::Ral, AugmentId::GreaterRal, FirstRuneOfLegend }) {
		SCOPED_TRACE(static_cast<int>(id));
		OwnedSurface icon { Size, Size };
		OwnedSurface outline { Size, Size };
		SDL_FillSurfaceRect(icon.surface, nullptr, 0);
		SDL_FillSurfaceRect(outline.surface, nullptr, 0);
		DrawRuneIcon(icon, Box, id, /*dim=*/false);
		DrawRuneOutline(outline, Box, id, 1);
		int ring = 0;
		for (int y = 0; y < Size; y++) {
			for (int x = 0; x < Size; x++) {
				if (At(outline, x, y) == 0) {
					// Nothing beside the rune is left without the outline: it goes all the way round.
					if (At(icon, x, y) == 0 && (At(icon, x - 1, y) != 0 || At(icon, x + 1, y) != 0 || At(icon, x, y - 1) != 0 || At(icon, x, y + 1) != 0))
						ADD_FAILURE() << "gap at " << x << "," << y;
					continue;
				}
				ring++;
				// Never on the rune, always beside it.
				EXPECT_EQ(At(icon, x, y), 0) << x << "," << y;
				EXPECT_TRUE(Touches(icon, x, y)) << x << "," << y;
			}
		}
		EXPECT_GT(ring, 40);
	}
}

TEST(Poe2RuneOutlineTest, OutlineStaysByTheBox)
{
	// At most a pixel past the box, as the game's outlines go a pixel past an item's picture.
	const Rectangle grown { Box.position - Displacement { 1, 1 }, { Box.size.width + 2, Box.size.height + 2 } };
	OwnedSurface outline { Size, Size };
	SDL_FillSurfaceRect(outline.surface, nullptr, 0);
	DrawRuneOutline(outline, Box, AugmentId::GreaterRal, 1);
	for (int y = 0; y < Size; y++) {
		for (int x = 0; x < Size; x++) {
			if (At(outline, x, y) != 0)
				EXPECT_TRUE(grown.contains(x, y)) << x << "," << y;
		}
	}
}

} // namespace
} // namespace devilution

#include <gtest/gtest.h>

#include <SDL.h>

#include "poe2/hd.hpp"

// POE2 Mashup: under the game menu DrawView asks hd::Render to keep the HD world it drew (hd::KeepWorldPicture) and
// show that picture again. The ask is one frame's: the palette fades of quitting present frames without a DrawView,
// and those must draw the world as recorded, not lay a kept picture made in the fade's colours (which showed blocks).

namespace devilution {
namespace {

#if !defined(USE_SDL1) && !defined(USE_SDL3)

TEST(HdKeptWorld, RenderTakesTheAskOfItsFrame)
{
	SDL_Surface *screen = SDL_CreateRGBSurfaceWithFormat(0, 64, 48, 32, SDL_PIXELFORMAT_ARGB8888);
	ASSERT_NE(screen, nullptr);
	SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(screen);
	ASSERT_NE(renderer, nullptr);

	hd::KeepWorldPicture(true);
	EXPECT_TRUE(hd::KeepWorldPictureAsked());
	hd::Render(renderer, { 64, 48 }, { 64, 48 });
	EXPECT_FALSE(hd::KeepWorldPictureAsked());
	// A frame presented without a DrawView (a fade step) isn't asked to keep anything.
	hd::Render(renderer, { 64, 48 }, { 64, 48 });
	EXPECT_FALSE(hd::KeepWorldPictureAsked());

	hd::KeepWorldPicture(true);
	hd::KeepWorldPicture(false);
	EXPECT_FALSE(hd::KeepWorldPictureAsked());

	hd::ForgetWorldPicture();
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(screen);
}

#endif

} // namespace
} // namespace devilution

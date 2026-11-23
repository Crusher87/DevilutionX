#include "poe2/live_fire_sdl.hpp"

#if !defined(USE_SDL1) && !defined(USE_SDL3)

#include <initializer_list>
#include <utility>

namespace devilution {

namespace fire {

namespace {

void SetLinear(SDL_Texture *texture)
{
#if SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#else
	(void)texture;
#endif
}

} // namespace

FireLayer::FireLayer(FireSource source, FireSettings settings, uint32_t seed, int threads, const FireRecipe &recipe, bool warm)
    : fire_(std::make_unique<LiveFire>(std::move(source), settings, seed, threads, recipe))
{
	// It opens already burning.
	if (warm)
		fire_->WarmUp();
}

// Its textures go with their renderer: at the game's end that may already be gone (ReleaseTextures lets them go before).
FireLayer::~FireLayer() = default;

void FireLayer::ReleaseTextures()
{
	for (SDL_Texture *texture : { colour_, multiply_, add_, lit_ }) {
		if (texture != nullptr)
			SDL_DestroyTexture(texture);
	}
	ForgetTextures();
}

void FireLayer::ForgetTextures()
{
	colour_ = nullptr;
	multiply_ = nullptr;
	add_ = nullptr;
	lit_ = nullptr;
	litWidth_ = 0;
	litHeight_ = 0;
	renderer_ = nullptr;
	texturesFailed_ = false;
	litFailed_ = false;
	lightPainted_ = false;
}

SDL_FRect FireLayer::GridRect(const SDL_FRect &picture) const
{
	const float across = static_cast<float>(fire_->PictureWidth());
	const float pad = across > 0 ? static_cast<float>(fire_->Pad()) / across * picture.w : 0.F;
	return SDL_FRect { picture.x - pad, picture.y, picture.w + 2.F * pad, picture.h };
}

void FireLayer::Pointer(float x, float y, const SDL_FRect &rect, float seconds)
{
	if (rect.w <= 0 || rect.h <= 0) {
		pointerKnown_ = false;
		return;
	}
	const float w = static_cast<float>(fire_->Width());
	const float h = static_cast<float>(fire_->Height());
	const float gx = (x - rect.x) / rect.w * w;
	const float gy = (1.F - (y - rect.y) / rect.h) * h;
	const bool inside = gx >= 0 && gy >= 0 && gx <= w && gy <= h;
	if (inside && pointerKnown_)
		fire_->Push(pointerX_, pointerY_, gx, gy, seconds);
	pointerKnown_ = inside;
	pointerX_ = gx;
	pointerY_ = gy;
}

bool FireLayer::MakeTextures(SDL_Renderer *renderer)
{
	if (renderer != renderer_) {
		// A renderer of its own: the old one's textures went with it.
		ForgetTextures();
		renderer_ = renderer;
	}
	if (colour_ != nullptr)
		return true;
	if (texturesFailed_)
		return false;
	colour_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, fire_->Width(), fire_->Height());
	if (colour_ == nullptr) {
		texturesFailed_ = true;
		return false;
	}
	SDL_SetTextureBlendMode(colour_, SDL_BLENDMODE_ADD);
	SetLinear(colour_);
	if (SDL_RenderTargetSupported(renderer) == SDL_TRUE) {
		multiply_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, fire_->LightWidth(), fire_->LightHeight());
		add_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, fire_->LightWidth(), fire_->LightHeight());
	}
	if (multiply_ != nullptr && add_ != nullptr) {
		SetLinear(multiply_);
		SetLinear(add_);
		// Multiplied by twice the texture (so the light can brighten as well as darken), where the renderer can; the
		// letters' alpha stays as it is.
		const SDL_BlendMode twice = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_SRC_COLOR, SDL_BLENDOPERATION_ADD,
		    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
		doubled_ = SDL_SetTextureBlendMode(multiply_, twice) == 0;
		if (!doubled_)
			SDL_SetTextureBlendMode(multiply_, SDL_BLENDMODE_MOD);
		// The warm light on their dark parts: added as much as they are dark.
		const SDL_BlendMode dark = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
		    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
		addDark_ = SDL_SetTextureBlendMode(add_, dark) == 0;
	} else {
		if (multiply_ != nullptr)
			SDL_DestroyTexture(multiply_);
		if (add_ != nullptr)
			SDL_DestroyTexture(add_);
		multiply_ = nullptr;
		add_ = nullptr;
	}
	return true;
}

bool FireLayer::Draw(SDL_Renderer *renderer, const SDL_FRect &rect, float seconds, const uint8_t *cover)
{
	lightPainted_ = false;
	if (renderer == nullptr || !MakeTextures(renderer))
		return false;
	fire_->Advance(seconds);
	void *pixels = nullptr;
	int pitch = 0;
	if (SDL_LockTexture(colour_, nullptr, &pixels, &pitch) != 0)
		return false;
	fire_->Paint(static_cast<uint8_t *>(pixels), pitch, cover);
	if (afterPaint_)
		afterPaint_(static_cast<uint8_t *>(pixels), pitch);
	SDL_UnlockTexture(colour_);
	SDL_RenderCopyF(renderer, colour_, nullptr, &rect);
	if (multiply_ != nullptr && add_ != nullptr) {
		void *mul = nullptr;
		void *add = nullptr;
		int mulPitch = 0;
		int addPitch = 0;
		if (SDL_LockTexture(multiply_, nullptr, &mul, &mulPitch) == 0) {
			if (SDL_LockTexture(add_, nullptr, &add, &addPitch) == 0) {
				fire_->PaintLight(static_cast<uint8_t *>(mul), mulPitch, static_cast<uint8_t *>(add), addPitch, doubled_);
				SDL_UnlockTexture(add_);
				lightPainted_ = true;
			}
			SDL_UnlockTexture(multiply_);
		}
	}
	return true;
}

bool FireLayer::DrawLit(SDL_Renderer *renderer, SDL_Texture *picture, const SDL_FRect &dest, const SDL_FRect &fire)
{
	if (!lightPainted_ || litFailed_ || renderer != renderer_ || picture == nullptr)
		return false;
	int w = 0;
	int h = 0;
	if (SDL_QueryTexture(picture, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0)
		return false;
	if (lit_ == nullptr || litWidth_ != w || litHeight_ != h) {
		if (lit_ != nullptr)
			SDL_DestroyTexture(lit_);
		lit_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
		litWidth_ = w;
		litHeight_ = h;
		if (lit_ == nullptr) {
			litFailed_ = true;
			return false;
		}
		SDL_SetTextureBlendMode(lit_, SDL_BLENDMODE_BLEND);
		SetLinear(lit_);
	}
	SDL_Texture *before = SDL_GetRenderTarget(renderer);
	// Drawn into a picture of its own (the menu intro's), it goes back to that picture's scale after.
	float scaleX = 1;
	float scaleY = 1;
	SDL_RenderGetScale(renderer, &scaleX, &scaleY);
	if (SDL_SetRenderTarget(renderer, lit_) != 0)
		return false;
	// The picture as it is, then times the light, then the warm light added where it is dark.
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);
	SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
	SDL_GetTextureBlendMode(picture, &mode);
	SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(renderer, picture, nullptr, nullptr);
	SDL_SetTextureBlendMode(picture, mode);
	const SDL_FRect lightRect { fire.x * static_cast<float>(w), fire.y * static_cast<float>(h), fire.w * static_cast<float>(w), fire.h * static_cast<float>(h) };
	SDL_RenderCopyF(renderer, multiply_, nullptr, &lightRect);
	if (addDark_)
		SDL_RenderCopyF(renderer, add_, nullptr, &lightRect);
	SDL_SetRenderTarget(renderer, before);
	if (before != nullptr)
		SDL_RenderSetScale(renderer, scaleX, scaleY);
	SDL_RenderCopyF(renderer, lit_, nullptr, &dest);
	return true;
}

} // namespace fire

} // namespace devilution

#endif

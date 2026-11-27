#include "poe2/live_fire_sdl.hpp"

#if !defined(USE_SDL1) && !defined(USE_SDL3)

#include <algorithm>
#include <cstring>
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
	gridWidth_ = fire_->Width();
	gridHeight_ = fire_->Height();
}

// Its textures go with their renderer: at the game's end that may already be gone (ReleaseTextures lets them go before).
FireLayer::~FireLayer()
{
#ifdef POE2_FIRE_LAYER_THREAD
	if (thread_.joinable()) {
		{
			const std::lock_guard<std::mutex> guard(lock_);
			quit_ = true;
		}
		wake_.notify_all();
		thread_.join();
	}
#endif
}

void FireLayer::WaitForFrame()
{
#ifdef POE2_FIRE_LAYER_THREAD
	std::unique_lock<std::mutex> guard(lock_);
	done_.wait(guard, [this] { return !working_; });
#endif
}

void FireLayer::Loop()
{
#ifdef POE2_FIRE_LAYER_THREAD
	for (;;) {
		{
			std::unique_lock<std::mutex> guard(lock_);
			wake_.wait(guard, [this] { return quit_ || working_; });
			if (quit_)
				return;
		}
		// The fire is this thread's alone while it works (the game's waits in Fire()).
		const int pitch = gridWidth_ * 4;
		fire_->Advance(frameSeconds_);
		// (Moving on unseen, AdvanceAsync, it isn't painted.)
		if (framePainted_) {
			fire_->Paint(frameColour_.data(), pitch, frameHasCover_ ? frameCover_.data() : nullptr);
			if (afterPaint_)
				afterPaint_(frameColour_.data(), pitch);
			if (!frameMultiply_.empty())
				fire_->PaintLight(frameMultiply_.data(), fire_->LightWidth() * 4, frameAdd_.data(), fire_->LightWidth() * 4, doubled_);
		}
		{
			const std::lock_guard<std::mutex> guard(lock_);
			working_ = false;
			frameReady_ = framePainted_;
		}
		done_.notify_all();
	}
#endif
}

namespace {

void Upload(SDL_Texture *texture, const std::vector<uint8_t> &picture, int width, int height)
{
	if (texture == nullptr || picture.empty())
		return;
	void *pixels = nullptr;
	int pitch = 0;
	if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) != 0)
		return;
	const size_t row = static_cast<size_t>(width) * 4;
	for (int y = 0; y < height; y++)
		std::memcpy(static_cast<uint8_t *>(pixels) + static_cast<ptrdiff_t>(y) * pitch, picture.data() + y * row, row);
	SDL_UnlockTexture(texture);
}

} // namespace

void FireLayer::UploadFrame()
{
	Upload(colour_, frameColour_, gridWidth_, gridHeight_);
	if (multiply_ != nullptr && add_ != nullptr && !frameMultiply_.empty()) {
		Upload(multiply_, frameMultiply_, fire_->LightWidth(), fire_->LightHeight());
		Upload(add_, frameAdd_, fire_->LightWidth(), fire_->LightHeight());
		lightVersion_++;
	}
	frameReady_ = false;
	frameShown_ = true;
}

bool FireLayer::FrameIsDue(float owed, float seconds, float interval)
{
	// Up to so many frames a second (40 on the menus: the fire's slow movement needs no more, and it leaves the
	// computer's cores to the game). Within a third of the game's frame of the interval counts as there: 25 ms is two
	// frames at 80 a second, and two frames timed at 12.4 ms each must not wait for a third.
	return owed >= interval - 0.35F * std::max(seconds, 0.F);
}

void FireLayer::Pump(float seconds, const uint8_t *cover, const FirePace &pace, bool upload)
{
#ifdef POE2_FIRE_LAYER_THREAD
	owedSeconds_ += seconds;
	// Asked for more frames than the menus' steady 40 a second, it gives them only while its thread keeps up: a frame
	// that is due while the last is still being worked out is late, and a few of those close together put it back to
	// the steady pace for a while (longer each time), so it never runs now with the game's frames, now behind them.
	float interval = pace.interval;
	if (interval < SteadyInterval) {
		if (steadyFrames_ > 0) {
			steadyFrames_--;
			interval = SteadyInterval;
		}
		if (++lateWindow_ >= LateWindow) {
			lateWindow_ = 0;
			late_ = 0;
		}
	}
	bool start = false;
	{
		std::unique_lock<std::mutex> guard(lock_);
		// The first frame shown (or the first after its textures went): waited for, so it never shows empty.
		if (upload && working_ && !frameShown_)
			done_.wait(guard, [this] { return !working_; });
		if (!working_) {
			if (upload && frameReady_)
				UploadFrame();
			start = (upload && !frameShown_) || FrameIsDue(owedSeconds_, seconds, interval);
		} else if (interval < SteadyInterval && FrameIsDue(owedSeconds_, seconds, interval) && ++late_ >= LateLimit) {
			late_ = 0;
			lateWindow_ = 0;
			steadySpells_ = std::min(steadySpells_ + 1, 6);
			steadyFrames_ = SteadyFrames << (steadySpells_ - 1);
		}
	}
	if (!start)
		return;
	framePainted_ = upload;
	if (upload) {
		const size_t grid = static_cast<size_t>(gridWidth_) * gridHeight_;
		frameColour_.resize(grid * 4);
		const bool light = multiply_ != nullptr && add_ != nullptr;
		const size_t lightSize = light ? static_cast<size_t>(fire_->LightWidth()) * fire_->LightHeight() * 4 : 0;
		frameMultiply_.resize(lightSize);
		frameAdd_.resize(lightSize);
		frameHasCover_ = cover != nullptr;
		if (frameHasCover_)
			frameCover_.assign(cover, cover + grid);
	}
	for (const PendingPush &push : pushes_)
		fire_->Push(push.x0, push.y0, push.x1, push.y1, push.seconds);
	pushes_.clear();
	fire_->LimitWorkers(pace.workers);
	frameSeconds_ = std::min(owedSeconds_, 0.25F);
	owedSeconds_ = 0;
	if (beforeFrame_)
		beforeFrame_();
	const bool first = upload && !frameShown_;
	{
		const std::lock_guard<std::mutex> guard(lock_);
		working_ = true;
	}
	if (!thread_.joinable())
		thread_ = std::thread([this] { Loop(); });
	wake_.notify_all();
	if (first) {
		std::unique_lock<std::mutex> guard(lock_);
		done_.wait(guard, [this] { return !working_; });
		UploadFrame();
	}
#else
	(void)seconds;
	(void)cover;
	(void)pace;
	(void)upload;
#endif
}

bool FireLayer::DrawAsync(SDL_Renderer *renderer, const SDL_FRect &rect, float seconds, const uint8_t *cover, const FirePace &pace)
{
#ifndef POE2_FIRE_LAYER_THREAD
	(void)pace;
	return Draw(renderer, rect, seconds, cover);
#else
	if (renderer == nullptr || !MakeTextures(renderer))
		return false;
	Pump(seconds, cover, pace, true);
	if (!frameShown_)
		return false;
	SDL_RenderCopyF(renderer, colour_, nullptr, &rect);
	lightPainted_ = multiply_ != nullptr && add_ != nullptr && !frameMultiply_.empty();
	return true;
#endif
}

void FireLayer::AdvanceAsync(float seconds, const FirePace &pace)
{
#ifndef POE2_FIRE_LAYER_THREAD
	(void)pace;
	fire_->LimitWorkers(-1);
	fire_->Advance(seconds);
#else
	Pump(seconds, nullptr, pace, false);
#endif
}

void FireLayer::ReleaseTextures()
{
	WaitForFrame();
	if (frameShown_) {
		// Its last frame goes up again to the next textures.
		frameShown_ = false;
		frameReady_ = true;
	}
	for (SDL_Texture *texture : { colour_, multiply_, add_, lit_ }) {
		if (texture != nullptr)
			SDL_DestroyTexture(texture);
	}
	ForgetTextures();
}

void FireLayer::ForgetTextures()
{
	WaitForFrame();
	if (frameShown_) {
		frameShown_ = false;
		frameReady_ = true;
	}
	colour_ = nullptr;
	multiply_ = nullptr;
	add_ = nullptr;
	lit_ = nullptr;
	litWidth_ = 0;
	litHeight_ = 0;
	litValid_ = false;
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
	const float w = static_cast<float>(gridWidth_);
	const float h = static_cast<float>(gridHeight_);
	const float gx = (x - rect.x) / rect.w * w;
	const float gy = (1.F - (y - rect.y) / rect.h) * h;
	const bool inside = gx >= 0 && gy >= 0 && gx <= w && gy <= h;
	if (inside && pointerKnown_)
		pushes_.push_back({ pointerX_, pointerY_, gx, gy, seconds });
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
	WaitForFrame();
	for (const PendingPush &push : pushes_)
		fire_->Push(push.x0, push.y0, push.x1, push.y1, push.seconds);
	pushes_.clear();
	frameReady_ = false;
	frameShown_ = false;
	owedSeconds_ = 0;
	fire_->LimitWorkers(-1);
	if (beforeFrame_)
		beforeFrame_();
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
				lightVersion_++;
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
		litValid_ = false;
	}
	// The light as it was when last lit (DrawAsync sends it up only so often): the lit picture as it is.
	if (litValid_ && litVersion_ == lightVersion_ && litPicture_ == picture && litFire_.x == fire.x && litFire_.y == fire.y && litFire_.w == fire.w && litFire_.h == fire.h) {
		SDL_RenderCopyF(renderer, lit_, nullptr, &dest);
		return true;
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
	litValid_ = true;
	litVersion_ = lightVersion_;
	litPicture_ = picture;
	litFire_ = fire;
	SDL_RenderCopyF(renderer, lit_, nullptr, &dest);
	return true;
}

} // namespace fire

} // namespace devilution

#endif

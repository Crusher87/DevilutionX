#include "poe2/menu_intro.hpp"

#include <algorithm>
#include <cmath>

namespace devilution {

namespace intro {

namespace {

constexpr float Pi = 3.14159265358979F;

float Clamp01(float x)
{
	return std::clamp(x, 0.F, 1.F);
}

float Mix(float a, float b, float t)
{
	return a + (b - a) * t;
}

float Fract(float x)
{
	return x - std::floor(x);
}

/** @brief The reference's lattice hash (GLSL: fract-based, on whole lattice points). */
float Hash(float x, float y, float z)
{
	float px = Fract(x * 0.1031F);
	float py = Fract(y * 0.1030F);
	float pz = Fract(z * 0.0973F);
	const float d = px * (py + 33.33F) + py * (px + 33.33F) + pz * (pz + 33.33F);
	px += d;
	py += d;
	pz += d;
	return Fract((px + py) * pz);
}

float Smooth(float t)
{
	return t * t * (3.F - 2.F * t);
}

/**
 * @brief One octave of value noise over a patch of its plane at one time (z): the lattice worked out once, then looked
 * up for each pixel. As ValueNoise, (x, y) in lattice units.
 */
struct Plane {
	int x0 = 0;
	int y0 = 0;
	int nx = 0;
	int ny = 0;
	std::vector<float> v;

	void Prepare(float xMin, float xMax, float yMin, float yMax, float z)
	{
		x0 = static_cast<int>(std::floor(std::min(xMin, xMax)));
		y0 = static_cast<int>(std::floor(std::min(yMin, yMax)));
		nx = static_cast<int>(std::floor(std::max(xMin, xMax))) - x0 + 2;
		ny = static_cast<int>(std::floor(std::max(yMin, yMax))) - y0 + 2;
		v.resize(static_cast<size_t>(nx) * ny);
		const float zi = std::floor(z);
		const float fz = Smooth(z - zi);
		for (int j = 0; j < ny; j++) {
			for (int i = 0; i < nx; i++) {
				const float x = static_cast<float>(x0 + i);
				const float y = static_cast<float>(y0 + j);
				const float a = Hash(x, y, zi);
				const float b = Hash(x, y, zi + 1.F);
				v[static_cast<size_t>(j) * nx + i] = (Mix(a, b, fz) * 2.F - 1.F) * 1.73F;
			}
		}
	}

	[[nodiscard]] float At(float x, float y) const
	{
		const float fx = std::floor(x);
		const float fy = std::floor(y);
		const int i = std::clamp(static_cast<int>(fx) - x0, 0, nx - 2);
		const int j = std::clamp(static_cast<int>(fy) - y0, 0, ny - 2);
		const float wx = Smooth(Clamp01(x - fx));
		const float wy = Smooth(Clamp01(y - fy));
		const float *r0 = v.data() + static_cast<size_t>(j) * nx + i;
		const float *r1 = r0 + nx;
		const float a = Mix(r0[0], r0[1], wx);
		const float b = Mix(r1[0], r1[1], wx);
		return Mix(a, b, wy);
	}
};

/** @brief fbm2 over a patch: its two octaves (the second at twice the size, offset as the reference's). */
struct Fbm2Plane {
	Plane o[2];

	void Prepare(float xMin, float xMax, float yMin, float yMax, float z)
	{
		o[0].Prepare(xMin, xMax, yMin, yMax, z);
		o[1].Prepare(xMin * 2.F + 17.F, xMax * 2.F + 17.F, yMin * 2.F + 17.F, yMax * 2.F + 17.F, z + 7.F);
	}

	[[nodiscard]] float At(float x, float y) const
	{
		return (o[0].At(x, y) + 0.5F * o[1].At(x * 2.F + 17.F, y * 2.F + 17.F)) / 1.5F;
	}
};

uint8_t ToByte(float v)
{
	return static_cast<uint8_t>(std::clamp(v, 0.F, 255.F) + 0.5F);
}

} // namespace

float SmoothStep(float a, float b, float x)
{
	return Smooth(Clamp01((x - a) / (b - a)));
}

// ---- the timeline ----

float LogoScale(float t)
{
	if (t < FlightStart)
		return 0.F;
	if (t < Landing) {
		// From behind the viewer, far bigger than the screen (all of it fire), pulled back fast, then falling into place.
		const float u = (t - FlightStart) / (Landing - FlightStart);
		return 0.848F + 3.F * std::pow(1.F - u, 1.8F) + 17.F * std::pow(1.F - u, 6.F);
	}
	// A short overshoot and spring back, then a slow drift back over 5 s.
	const float d = t - Landing;
	return (1.F + 0.06F * std::pow(1.F - SmoothStep(0.F, 5.F, d), 1.6F)) * (1.F - 0.20F * std::exp(-d / 0.13F) * std::cos(d * 13.F));
}

float Glide(float t)
{
	const float x = Clamp01((t - GlideStart) / (GlideEnd - GlideStart));
	return x * x * x * (x * (x * 6.F - 15.F) + 10.F);
}

float Knock(float t)
{
	const float ts = t - Slash;
	return ts >= 0 ? 12.F * std::exp(-ts / 0.16F) * std::cos(ts * 26.F) : 0.F;
}

float FireAmount(float t)
{
	if (t < FlightStart - 0.6F)
		return 0.F;
	if (t < Landing)
		return 1.F + 0.7F * SmoothStep(FlightStart - 0.6F, FlightStart + 0.2F, t);
	float a = 1.F + 1.6F * std::exp(-(t - Landing) / 0.45F);
	if (t > Hush && t < Slash)
		a *= 1.F - 0.55F * std::sin(Pi * (t - Hush) / (Slash - Hush));
	if (t >= Slash)
		a *= 1.F + 0.2F * std::exp(-(t - Slash) / 0.35F);
	return a;
}

float BackLight(float t)
{
	if (t < FlightStart)
		return 0.F;
	if (t < Landing)
		return 0.7F;
	return 0.7F * std::exp(-(t - Landing) / 0.3F);
}

float Trail(float t)
{
	if (!(t > FlightStart && t < Landing + 0.4F))
		return 0.F;
	if (t < Landing)
		return std::pow(1.F - (t - FlightStart) / (Landing - FlightStart), 0.7F);
	return 0.25F * std::max(0.F, 1.F - std::abs(t - Landing) / 0.4F);
}

float RoomGlow(float t)
{
	if (t < 0)
		return 0.F;
	if (t < FlightStart)
		return 0.16F * (t / FlightStart) * (t / FlightStart);
	if (t < Landing)
		return 0.9F - 0.4F * (t - FlightStart) / (Landing - FlightStart);
	float g = 0.5F + 0.25F * std::exp(-(t - Landing) / 0.55F);
	if (t > Hush && t < Slash)
		g *= 1.F - 0.4F * std::sin(Pi * (t - Hush) / (Slash - Hush));
	if (t >= Slash)
		g += 0.2F * std::exp(-(t - Slash) / 0.4F);
	return g;
}

float TimeWarp(float t)
{
	if (t >= Landing && t < Landing + 0.012F)
		return 0.075F;
	if (t >= Slash + 0.085F && t < Slash + 0.097F)
		return 0.075F;
	return 1.F;
}

float LineReach(float t)
{
	const float u = Clamp01((t - Slash - 0.12F) / 0.45F);
	return u * (2.F - u);
}

float LineAmount(float t)
{
	return 1.F + 0.6F * std::exp(-std::max(0.F, t - Slash - 0.15F) / 0.5F);
}

float MenuAlpha(float t)
{
	return SmoothStep(MenuAt, MenuAt + 1.8F, t);
}

float IntroLook(float t)
{
	return 1.F - SmoothStep(MenuAt, End, t);
}

Lens LensAt(float t, float jitterX, float jitterY)
{
	Lens lens;
	const float d = t - Landing;
	const float ts = t - Slash;
	const float tt = t - Stamp;
	const float fl = (t >= FlightStart && t < Landing) ? 1.F - (t - FlightStart) / (Landing - FlightStart) : 0.F;
	const float hit = d >= 0 ? std::exp(-d / 0.16F) : 0.F;
	const float cut = ts >= 0 ? std::exp(-ts / 0.3F) : 0.F;
	lens.aberration = 0.001F + 0.0015F * fl + 0.007F * hit + 0.006F * cut;
	const float e1 = d >= 0 ? std::exp(-d / 0.24F) : 0.F;
	const float e2 = ts >= 0 ? std::exp(-ts / 0.2F) : 0.F;
	const float e3 = tt >= 0 ? std::exp(-tt / 0.18F) : 0.F;
	const float rumble = fl > 0 ? (1.F - fl) * 4.F : 0.F;
	lens.shakeX = 22.F * e1 * std::sin(d * 60.F) - 70.F * ClawDirX * e2 * std::cos(ts * 44.F) + jitterX * rumble;
	lens.shakeY = 64.F * e1 * std::cos(d * 52.F) - 70.F * ClawDirY * e2 * std::cos(ts * 44.F) + 30.F * e3 * std::cos(tt * 50.F) + jitterY * rumble;
	lens.punch = (d >= 0 ? 0.11F * std::exp(-d / 0.12F) : 0.F) + (ts >= 0 ? 0.09F * std::exp(-ts / 0.13F) : 0.F) + (tt >= 0 ? 0.03F * std::exp(-tt / 0.1F) : 0.F);
	lens.flash = (ts >= 0 ? 0.7F * std::exp(-ts / 0.05F) : 0.F) + (tt >= 0 ? 0.4F * std::exp(-tt / 0.05F) : 0.F);
	lens.dim = t < Landing ? 1.F : (t < Slash ? 1.F - 0.7F * SmoothStep(Slash - 0.22F, Slash - 0.02F, t) : 1.F);
	lens.rays = t < FlightStart ? 0.3F : (t < Landing ? 1.F : 0.28F + 0.25F * std::exp(-d / 0.7F) + (ts >= 0 ? 0.45F * std::exp(-ts / 0.35F) : 0.F));
	// Strong as it bursts out of the screen, easing off as it falls into place.
	lens.zoom = std::pow(fl, 1.3F) * 1.4F + 0.3F * hit;
	lens.defocus = 0.9F * SmoothStep(0.68F, 1.F, fl);
	return lens;
}

const std::array<SoundCue, 4> &SoundCues()
{
	// Each through the reference's master gain of 0.9. The first (apoc) fades in over 0.8 s, not starting all at once. The D2R swell is the file from its 3.0 s on (intro_sfx.wav).
	static const std::array<SoundCue, 4> Cues { {
		{ "apoc.wav", nullptr, FlightStart - 0.10F, 1.0F * 0.9F, 0.8F },
		{ "intro_sfx.wav", nullptr, Landing, 0.9F, 0.35F },
		{ "diabloa1.wav", "diabloa2.wav", Slash - 0.06F, 1.0F * 0.9F, 0.F },
		{ "flamwave.wav", nullptr, Slash + 0.15F, 0.9F * 0.9F, 0.F },
	} };
	return Cues;
}

float Random::Next()
{
	state_ += 0x6D2B79F5U;
	uint32_t t = state_;
	t = (t ^ (t >> 15)) * (1U | state_);
	t = (t + ((t ^ (t >> 7)) * (61U | t))) ^ t;
	return static_cast<float>(t ^ (t >> 14)) / 4294967296.F;
}

// ---- noise ----

float ValueNoise(float x, float y, float z)
{
	const float ix = std::floor(x);
	const float iy = std::floor(y);
	const float iz = std::floor(z);
	const float fx = Smooth(x - ix);
	const float fy = Smooth(y - iy);
	const float fz = Smooth(z - iz);
	const float a = Mix(Mix(Hash(ix, iy, iz), Hash(ix + 1, iy, iz), fx), Mix(Hash(ix, iy + 1, iz), Hash(ix + 1, iy + 1, iz), fx), fy);
	const float b = Mix(Mix(Hash(ix, iy, iz + 1), Hash(ix + 1, iy, iz + 1), fx), Mix(Hash(ix, iy + 1, iz + 1), Hash(ix + 1, iy + 1, iz + 1), fx), fy);
	return (Mix(a, b, fz) * 2.F - 1.F) * 1.73F;
}

float Fbm2(float x, float y, float z)
{
	return (ValueNoise(x, y, z) + 0.5F * ValueNoise(x * 2.F + 17.F, y * 2.F + 17.F, z + 7.F)) / 1.5F;
}

float Fbm3(float x, float y, float z)
{
	return (ValueNoise(x, y, z) + 0.5F * ValueNoise(x * 2.F + 17.F, y * 2.F + 17.F, z + 7.F) + 0.25F * ValueNoise(x * 4.F + 31.F, y * 4.F + 31.F, z + 13.F)) / 1.75F;
}

// ---- the subtitle ----

namespace {

/** @brief A bilinear sample of an RGBA picture (top row first) at (u, v), v up, as the graphics card's (edges clamped). */
void SampleRgba(const uint8_t *rgba, int width, int height, int pitch, float u, float v, float out[4])
{
	const float tx = std::clamp(u * static_cast<float>(width) - 0.5F, 0.F, static_cast<float>(width - 1));
	const float ty = std::clamp((1.F - v) * static_cast<float>(height) - 0.5F, 0.F, static_cast<float>(height - 1));
	const int x0 = static_cast<int>(tx);
	const int y0 = static_cast<int>(ty);
	const int x1 = std::min(x0 + 1, width - 1);
	const int y1 = std::min(y0 + 1, height - 1);
	const float fx = tx - static_cast<float>(x0);
	const float fy = ty - static_cast<float>(y0);
	const uint8_t *r0 = rgba + static_cast<ptrdiff_t>(y0) * pitch;
	const uint8_t *r1 = rgba + static_cast<ptrdiff_t>(y1) * pitch;
	for (int c = 0; c < 4; c++) {
		const float a = Mix(r0[x0 * 4 + c], r0[x1 * 4 + c], fx);
		const float b = Mix(r1[x0 * 4 + c], r1[x1 * 4 + c], fx);
		out[c] = Mix(a, b, fy) / 255.F;
	}
}

} // namespace

bool Subtitle::Load(const uint8_t *steel, int steelWidth, int steelHeight, int steelPitch, const uint8_t *mask, int maskWidth, int maskHeight, int maskPitch, int width, int height)
{
	worn_.clear();
	if (steel == nullptr || mask == nullptr || steelWidth <= 0 || steelHeight <= 0 || maskWidth <= 0 || maskHeight <= 0 || width <= 0 || height <= 0)
		return false;
	width_ = width;
	height_ = height;
	const size_t pixels = static_cast<size_t>(width) * height;
	alpha_.assign(pixels, 0.F);
	glow_.assign(pixels, 0.F);
	worn_.assign(pixels * 3, 0.F);
	for (int j = 0; j < height; j++) {
		const float tv = 1.F - (static_cast<float>(j) + 0.5F) / static_cast<float>(height);
		const float py = Bottom + tv * Height;
		// Lit from above, and a warm glow low down.
		const float top = Mix(0.78F, 1.12F, SmoothStep(0.36F, 0.66F, tv));
		const float low = 1.F - SmoothStep(0.33F, 0.55F, tv);
		for (int i = 0; i < width; i++) {
			const float tu = (static_cast<float>(i) + 0.5F) / static_cast<float>(width);
			const float px = Left + tu * Width;
			float s[4];
			float m[4];
			SampleRgba(steel, steelWidth, steelHeight, steelPitch, tu, tv, s);
			SampleRgba(mask, maskWidth, maskHeight, maskPitch, tu, tv, m);
			const size_t k = static_cast<size_t>(j) * width + i;
			alpha_[k] = s[3];
			glow_[k] = m[1];
			if (s[3] <= 0.F)
				continue;
			const float lum = Clamp01(s[0] * 0.3F + s[1] * 0.59F + s[2] * 0.11F);
			const float l = std::pow(lum, 1.15F);
			const float grime = (0.70F + 0.30F * Fbm3(px / 4.F, py / 4.F, 7.F)) * (0.88F + 0.12F * ValueNoise(px / 1.4F, py / 1.4F, 3.F));
			const float k2 = grime * top;
			worn_[k * 3 + 0] = Mix(0.17F, 0.95F, l) * k2 + 0.10F * low;
			worn_[k * 3 + 1] = Mix(0.13F, 0.86F, l) * k2 + 0.035F * low;
			worn_[k * 3 + 2] = Mix(0.11F, 0.75F, l) * k2 + 0.01F * low;
		}
	}
	return true;
}

bool Subtitle::Shows(float sinceSlash)
{
	return sinceSlash - 0.05F > 0.F;
}

float Subtitle::Pop(float sinceSlash)
{
	const float age = std::max(sinceSlash - 0.05F, 0.F);
	return 1.F + 0.10F * std::exp(-age / 0.05F);
}

void Subtitle::Paint(uint8_t *rgba, int pitch, float t, float sinceSlash, float red) const
{
	const int w = width_;
	const int h = height_;
	const float age = sinceSlash - 0.05F;
	if (age <= 0.F || worn_.empty()) {
		for (int j = 0; j < h; j++)
			std::fill(rgba + static_cast<ptrdiff_t>(j) * pitch, rgba + static_cast<ptrdiff_t>(j) * pitch + w * 4, 0);
		return;
	}
	const float vis = SmoothStep(0.F, 0.015F, age);
	const float heat = std::exp(-age / 1.35F);
	const float k = SmoothStep(0.03F, 0.5F, heat);
	// White-hot, through molten orange to a dull red as it cools.
	const float a1 = SmoothStep(0.08F, 0.4F, heat);
	const float a2 = SmoothStep(0.45F, 0.95F, heat);
	const float hotRgb[3] = {
		Mix(Mix(0.75F, 1.F, a1), 1.F, a2),
		Mix(Mix(0.10F, 0.42F, a1), 0.93F, a2),
		Mix(Mix(0.02F, 0.06F, a1), 0.72F, a2),
	};
	const float emberRgb[3] = { Mix(1.F, 0.95F, red), Mix(0.40F, 0.16F, red), Mix(0.08F, 0.04F, red) };
	const float haloRgb[3] = { Mix(1.F, 0.9F, red), Mix(0.36F, 0.14F, red), Mix(0.06F, 0.03F, red) };
	// The glint sweeping across the cooled metal.
	const float glintAt = (sinceSlash - 2.7F) / 0.75F * 1.3F - 0.15F;
	const bool glinting = glintAt > -0.3F && glintAt < 1.4F;
	Plane flicker;
	if (k > 0.F)
		flicker.Prepare((Left - 2.F) / 7.F, (Left + Width + 2.F) / 7.F, (Bottom - 2.F) / 7.F, (Bottom + Height + 2.F) / 7.F, t * 9.F);
	Plane breath;
	breath.Prepare((Left - 2.F) / 26.F, (Left + Width + 2.F) / 26.F, (Bottom - 2.F) / 26.F, (Bottom + Height + 2.F) / 26.F, t * 0.7F);
	for (int j = 0; j < h; j++) {
		const float tv = 1.F - (static_cast<float>(j) + 0.5F) / static_cast<float>(h);
		const float py = Bottom + tv * Height;
		uint8_t *out = rgba + static_cast<ptrdiff_t>(j) * pitch;
		for (int i = 0; i < w; i++) {
			const size_t n = static_cast<size_t>(j) * w + i;
			const float sa = alpha_[n];
			const float mg = glow_[n];
			uint8_t *px = out + static_cast<ptrdiff_t>(i) * 4;
			if (sa <= 0.F && mg <= 0.F) {
				px[0] = px[1] = px[2] = px[3] = 0;
				continue;
			}
			const float tu = (static_cast<float>(i) + 0.5F) / static_cast<float>(w);
			const float pxl = Left + tu * Width;
			const float breathe = 0.5F + 0.5F * std::sin(t * 1.1F + pxl * 0.012F) + 0.35F * breath.At(pxl / 26.F, py / 26.F);
			float col[3] = { 0.F, 0.F, 0.F };
			const float a = sa * vis;
			if (sa > 0.F) {
				const float flick = k > 0.F ? 0.9F + 0.1F * flicker.At(pxl / 7.F, py / 7.F) : 1.F;
				const float ember = (1.F - tv) * (0.10F + 0.07F * breathe) * (1.F - k) * 0.6F;
				float glint = 0.F;
				if (glinting) {
					const float g = (tu + tv * 0.06F - glintAt) / 0.035F;
					glint = g * g < 16.F ? std::exp(-g * g) * 1.3F : 0.F;
				}
				for (int c = 0; c < 3; c++) {
					const float body = Mix(worn_[n * 3 + c] + emberRgb[c] * ember, hotRgb[c] * flick, k) + (c == 0 ? 1.F : (c == 1 ? 0.93F : 0.80F)) * glint;
					col[c] = body * a;
				}
			}
			const float halo = mg * (heat * 1.1F + 0.015F + 0.02F * breathe) * vis * (1.F - a);
			for (int c = 0; c < 3; c++)
				px[c] = ToByte((col[c] + haloRgb[c] * halo) * 255.F);
			px[3] = ToByte(a * 255.F);
		}
	}
}

// ---- the claws ----

namespace {

constexpr float ClawLength[4] = { 370.F, 470.F, 450.F, 340.F };
constexpr float ClawStart[4] = { -40.F, 8.F, 30.F, 85.F };
/** @brief Across the claws' way (browser pixels, y up). */
constexpr float ClawAcrossX = 0.191F;
constexpr float ClawAcrossY = -0.982F;

} // namespace

void Claws::Prepare(float perPixel)
{
	width_ = std::max(1, static_cast<int>((Right - Left) * perPixel));
	height_ = std::max(1, static_cast<int>((Top - Bottom) * perPixel));
	const float du = (Right - Left) / static_cast<float>(width_);
	const float dv = (Top - Bottom) / static_cast<float>(height_);
	lit_.assign(static_cast<size_t>(width_) * height_ * 8, 0.F);
	for (int k = 0; k < 4; k++) {
		const float kf = static_cast<float>(k);
		const float length = ClawLength[k];
		const float start = ClawStart[k];
		along_[k].assign(width_, -1.F);
		first_[k].assign(width_, height_);
		last_[k].assign(width_, -1);
		for (int i = 0; i < width_; i++) {
			const float u = Left + (static_cast<float>(i) + 0.5F) * du;
			const float sp = (u - start + length) / (2.F * length);
			if (sp < 0.F || sp > 1.F)
				continue;
			along_[k][i] = sp;
			// Digs in thick, leaves thin.
			const float wd = (2.F + 14.F * std::pow(std::max(std::sin(Pi * std::pow(sp, 0.75F)), 0.F), 1.3F)) * (k == 1 || k == 2 ? 1.F : 0.78F);
			const float vc = (kf - 1.5F) * 40.F + 0.00024F * (u - start) * (u - start) - 22.F + (Fbm2(u / 90.F, kf * 7.F, 1.F) - 0.5F) * 10.F;
			const float jag = (ValueNoise(u / 9.F, kf * 3.F, 1.F) - 0.5F) * wd * 0.5F;
			const float spread = wd * wd * 7.F + 60.F;
			for (int j = 0; j < height_; j++) {
				const float v = Top - (static_cast<float>(j) + 0.5F) * dv;
				const float d = v - vc;
				const float dd = std::abs(d + jag * 0.4F);
				const size_t n = static_cast<size_t>(j) * width_ + i;
				const float r = dd / wd;
				float *lit = &lit_[n * 8 + static_cast<size_t>(k) * 2];
				lit[0] = r < 2.6F ? std::exp(-std::pow(r, 2.6F)) : 0.F;
				const float g = d * d / spread;
				// Out where it adds less than a step of the picture's colour, none at all.
				lit[1] = g < 5.5F ? std::exp(-g) : 0.F;
				if (lit[0] > 0.F || lit[1] > 0.F) {
					first_[k][i] = std::min(first_[k][i], j);
					last_[k][i] = j;
				}
			}
		}
	}
}

bool Claws::Paint(uint8_t *rgba, int pitch, float t, float sinceSlash, float red) const
{
	const float ts = sinceSlash;
	if (width_ <= 0 || ts <= 0.F || ts >= Lasts)
		return false;
	const float du = (Right - Left) / static_cast<float>(width_);
	const float dv = (Top - Bottom) / static_cast<float>(height_);
	const float glowRgb[3] = { Mix(1.F, 0.9F, red), Mix(0.34F, 0.14F, red), Mix(0.06F, 0.03F, red) };
	constexpr float White[3] = { 1.F, 0.97F, 0.86F };
	// Per claw and column, this frame: how hot (white), how molten (and its colour) and the glow round it.
	struct Column {
		float white, glow;
		float molten[3];
		bool on;
	};
	std::vector<Column> columns[4];
	// The cracks in the molten metal: noise in screen space, racing up.
	Fbm2Plane cracks[4];
	const float cx[4] = { CentreX + Left * ClawDirX + Top * ClawAcrossX, CentreX + Right * ClawDirX + Top * ClawAcrossX, CentreX + Left * ClawDirX + Bottom * ClawAcrossX, CentreX + Right * ClawDirX + Bottom * ClawAcrossX };
	const float cy[4] = { CentreY + Left * ClawDirY + Top * ClawAcrossY, CentreY + Right * ClawDirY + Top * ClawAcrossY, CentreY + Left * ClawDirY + Bottom * ClawAcrossY, CentreY + Right * ClawDirY + Bottom * ClawAcrossY };
	const float xMin = *std::min_element(cx, cx + 4) - 2.F;
	const float xMax = *std::max_element(cx, cx + 4) + 2.F;
	const float yMin = *std::min_element(cy, cy + 4) - 2.F;
	const float yMax = *std::max_element(cy, cy + 4) + 2.F;
	bool any = false;
	for (int k = 0; k < 4; k++) {
		const float kf = static_cast<float>(k);
		const float head = (ts - kf * 0.010F) / 0.065F;
		columns[k].resize(width_);
		for (int i = 0; i < width_; i++) {
			Column &c = columns[k][i];
			const float sp = along_[k][i];
			c.on = sp >= 0.F && sp <= head;
			if (!c.on)
				continue;
			const float age = ts - kf * 0.010F - sp * 0.065F;
			const float hot = std::exp(-age / 0.20F);
			const float mol = std::exp(-age / 0.65F) * (1.F - SmoothStep(2.0F, 3.6F, age));
			const float cool = Clamp01(age / 1.3F);
			c.white = hot * 3.2F;
			c.molten[0] = Mix(1.F, 0.78F, cool) * mol * 1.6F;
			c.molten[1] = Mix(0.40F, 0.08F, cool) * mol * 1.6F;
			c.molten[2] = Mix(0.05F, 0.02F, cool) * mol * 1.6F;
			c.glow = hot * 1.1F + mol * 0.16F;
			// Gone dark: nothing more to work out there.
			if (c.white < 0.002F && c.molten[0] < 0.002F && c.glow < 0.002F) {
				c.on = false;
				continue;
			}
			any = true;
		}
		cracks[k].Prepare((xMin + t * 120.F) / 40.F, (xMax + t * 120.F) / 40.F, (yMin + t * 200.F) / 70.F, (yMax + t * 200.F) / 70.F, t * 1.5F + kf);
	}
	// The cracks are broad (40 by 70 browser pixels): worked out every few pixels and blended between.
	constexpr int Step = 4;
	const int cw = width_ / Step + 2;
	const int ch = height_ / Step + 2;
	std::vector<float> crackGrid[4];
	for (int k = 0; k < 4; k++) {
		crackGrid[k].resize(static_cast<size_t>(cw) * ch);
		for (int b = 0; b < ch; b++) {
			const float v = Top - (static_cast<float>(std::min(b * Step, height_ - 1)) + 0.5F) * dv;
			for (int a = 0; a < cw; a++) {
				const float u = Left + (static_cast<float>(std::min(a * Step, width_ - 1)) + 0.5F) * du;
				const float px = CentreX + u * ClawDirX + v * ClawAcrossX;
				const float py = CentreY + u * ClawDirY + v * ClawAcrossY;
				crackGrid[k][static_cast<size_t>(b) * cw + a] = 0.55F + 0.7F * cracks[k].At((px + t * 120.F) / 40.F, (py + t * 200.F) / 70.F);
			}
		}
	}
	for (int j = 0; j < height_; j++) {
		uint8_t *out = rgba + static_cast<ptrdiff_t>(j) * pitch;
		for (int i = 0; i < width_; i++) {
			float col[3] = { 0.F, 0.F, 0.F };
			const size_t n = static_cast<size_t>(j) * width_ + i;
			for (int k = 0; k < 4; k++) {
				const Column &c = columns[k][i];
				if (!c.on || j < first_[k][i] || j > last_[k][i])
					continue;
				const float g = lit_[n * 8 + static_cast<size_t>(k) * 2];
				const float gg = lit_[n * 8 + static_cast<size_t>(k) * 2 + 1];
				if (g > 0.F) {
					const int a = i / Step;
					const int b = j / Step;
					const float fa = static_cast<float>(i - a * Step) / Step;
					const float fb = static_cast<float>(j - b * Step) / Step;
					const float *c0 = &crackGrid[k][static_cast<size_t>(b) * cw + a];
					const float *c1 = c0 + cw;
					const float cr = Mix(Mix(c0[0], c0[1], fa), Mix(c1[0], c1[1], fa), fb);
					for (int e = 0; e < 3; e++)
						col[e] += g * (White[e] * c.white + c.molten[e] * cr);
				}
				if (gg > 0.F) {
					for (int e = 0; e < 3; e++)
						col[e] += glowRgb[e] * gg * c.glow;
				}
			}
			uint8_t *px = out + static_cast<ptrdiff_t>(i) * 4;
			px[0] = ToByte(col[0] * 255.F);
			px[1] = ToByte(col[1] * 255.F);
			px[2] = ToByte(col[2] * 255.F);
			px[3] = 255;
		}
	}
	return any;
}

// ---- the room ----

void PaintRoom(uint8_t *rgba, int pitch, int width, int height, float t, float glow)
{
	if (width <= 0 || height <= 0)
		return;
	// The reference's stage, 1600 x 900 y up, stretched over the screen.
	Plane smoke[3];
	const float sx = 1600.F;
	const float sy = 900.F;
	{
		const float x0 = (0.F + t * 18.F) / 260.F;
		const float x1 = (sx + t * 18.F) / 260.F;
		const float y0 = (0.F - t * 46.F) / 260.F;
		const float y1 = (sy - t * 46.F) / 260.F;
		smoke[0].Prepare(x0, x1, y0, y1, t * 0.15F);
		smoke[1].Prepare(x0 * 2.F + 17.F, x1 * 2.F + 17.F, y0 * 2.F + 17.F, y1 * 2.F + 17.F, t * 0.15F + 7.F);
		smoke[2].Prepare(x0 * 4.F + 31.F, x1 * 4.F + 31.F, y0 * 4.F + 31.F, y1 * 4.F + 31.F, t * 0.15F + 13.F);
	}
	Fbm2Plane swirl;
	swirl.Prepare(0.F, sx / 120.F, (0.F - t * 90.F) / 120.F, (sy - t * 90.F) / 120.F, t * 0.3F);
	for (int j = 0; j < height; j++) {
		const float py = (1.F - (static_cast<float>(j) + 0.5F) / static_cast<float>(height)) * sy;
		uint8_t *out = rgba + static_cast<ptrdiff_t>(j) * pitch;
		for (int i = 0; i < width; i++) {
			const float px = (static_cast<float>(i) + 0.5F) / static_cast<float>(width) * sx;
			const float dx = (px - 800.F) / 900.F;
			const float dy = (py - 560.F) / 520.F;
			const float r2 = dx * dx + dy * dy;
			const float qx = (px + t * 18.F) / 260.F;
			const float qy = (py - t * 46.F) / 260.F;
			const float sm = (smoke[0].At(qx, qy) + 0.5F * smoke[1].At(qx * 2.F + 17.F, qy * 2.F + 17.F) + 0.25F * smoke[2].At(qx * 4.F + 31.F, qy * 4.F + 31.F)) / 1.75F;
			const float s2 = swirl.At(px / 120.F, (py - t * 90.F) / 120.F);
			const float b = std::exp(-r2 * 1.5F) * (0.35F + 1.1F * sm) * glow;
			const float low = std::exp(-py / 240.F) * glow * 0.45F * (0.35F + s2);
			const float fade = std::clamp(1.F - 0.5F * r2, 0.F, 1.F);
			const float r = (0.60F * b + 0.95F * b * b * 0.55F * s2 + 0.50F * low) * fade;
			const float g = (0.085F * b + 0.30F * b * b * 0.55F * s2 + 0.07F * low) * fade;
			const float bl = (0.02F * b + 0.05F * b * b * 0.55F * s2 + 0.02F * low) * fade;
			uint8_t *p = out + static_cast<ptrdiff_t>(i) * 4;
			p[0] = ToByte(r * 255.F);
			p[1] = ToByte(g * 255.F);
			p[2] = ToByte(bl * 255.F);
			p[3] = 255;
		}
	}
}

// ---- the embers ----

void Embers::SetScreen(float left, float bottom, float right, float top)
{
	left_ = left;
	bottom_ = bottom;
	right_ = std::max(right, left + 1.F);
	top_ = std::max(top, bottom + 1.F);
}

void Embers::Step(float dt, float t, Random &random)
{
	if (dt <= 0)
		return;
	const float sw = right_ - left_;
	const float sh = top_ - bottom_;
	// As many to the pixel as on the reference's stage.
	const float area = std::max(0.25F, sw * sh / (1600.F * 900.F));
	if (t >= 0) {
		const float rate = (t < FlightStart ? 9.F : (t < intro::Landing ? 34.F : 50.F + 70.F * std::exp(-(t - intro::Landing) / 1.6F))) * area * IntroLook(t);
		const float want = rate * dt;
		int n = static_cast<int>(want) + (random.Next() < want - std::floor(want) ? 1 : 0);
		while (n-- > 0) {
			Ember e {};
			if (t < intro::Landing) {
				e.x = left_ + random.Next() * sw;
				e.y = bottom_ + random.Next() * sh * 0.85F;
				e.vx = (random.Next() - 0.5F) * 30.F;
				e.vy = 20.F + random.Next() * 60.F;
				e.life = 2.F + random.Next() * 2.5F;
				e.p = random.Next() * 6.28F;
				e.s = 0.9F + random.Next() * 1.F;
				e.dim = 0.5F;
			} else {
				e.x = left_ + random.Next() * sw;
				e.y = random.Next() < 0.3F ? bottom_ + random.Next() * sh * 0.5F : bottom_ - 4.F;
				e.vx = 10.F + random.Next() * 70.F;
				e.vy = 60.F + random.Next() * 190.F;
				e.life = 2.5F + random.Next() * 3.F;
				e.p = random.Next() * 6.28F;
				e.s = 1.F + random.Next() * 1.5F;
				e.dim = 0.5F + random.Next() * 0.5F;
			}
			e.kind = 0;
			e.ox = e.x;
			e.oy = e.y;
			embers_.push_back(e);
		}
	}
	const float d1 = std::pow(0.3F, dt);
	const float d2 = std::pow(0.1F, dt);
	for (Ember &e : embers_) {
		e.age += dt;
		if (e.age < 0)
			continue;
		if (e.kind == 1) {
			e.vy -= 1000.F * dt;
			e.vx *= d1;
		} else if (e.kind == 2) {
			e.vx *= d2;
			e.vy = e.vy * d2 + 60.F * dt;
		} else {
			e.vx += std::sin(e.p + e.age * 3.F) * 40.F * dt;
		}
		e.ox = e.x;
		e.oy = e.y;
		e.moved = true;
		e.x += e.vx * dt;
		e.y += e.vy * dt;
	}
	embers_.erase(std::remove_if(embers_.begin(), embers_.end(), [&](const Ember &e) {
		return !(e.age < e.life && e.y > bottom_ - 12.F && e.y < top_ + 12.F && e.x > left_ - 20.F && e.x < right_ + 20.F);
	}),
	    embers_.end());
	const size_t most = static_cast<size_t>(5200.F * std::min(area, 3.F));
	if (embers_.size() > most)
		embers_.erase(embers_.begin(), embers_.begin() + static_cast<ptrdiff_t>(embers_.size() - most));
}

void Embers::Landing(Random &random)
{
	// Blown out from the logo as it lands.
	for (int k = 0; k < 650; k++) {
		const float a = random.Next() * 6.283F;
		const float r1 = random.Next();
		const float sp = 150.F + r1 * random.Next() * 1300.F;
		Ember e {};
		e.x = StageAnchorX + (random.Next() - 0.5F) * 880.F;
		e.y = StageAnchorY + (random.Next() - 0.5F) * 230.F;
		e.vx = std::cos(a) * sp * 1.4F;
		e.vy = std::sin(a) * sp * 0.8F + 140.F;
		e.life = 0.6F + random.Next() * 1.8F;
		e.p = random.Next() * 6.28F;
		e.s = 1.2F + random.Next() * 1.8F;
		e.kind = 2;
		e.dim = 1.F;
		e.ox = e.x;
		e.oy = e.y;
		embers_.push_back(e);
	}
}

void Embers::Rake(Random &random)
{
	// Thrown off the claws as they rake across, along their way.
	for (int k = 0; k < 4; k++) {
		for (int n = 0; n < 180; n++) {
			const float sp = random.Next();
			const float u = ClawStart[k] - ClawLength[k] + 2.F * ClawLength[k] * sp;
			const float v = (static_cast<float>(k) - 1.5F) * 40.F + 0.00024F * (u - ClawStart[k]) * (u - ClawStart[k]) - 22.F;
			const float r1 = random.Next();
			const float sv = 300.F + r1 * random.Next() * 1900.F;
			const float side = (random.Next() - 0.5F) * 900.F;
			Ember e {};
			e.x = Claws::CentreX + ClawDirX * u - ClawDirY * v;
			e.y = Claws::CentreY + ClawDirY * u + ClawDirX * v;
			e.vx = ClawDirX * sv - ClawDirY * side * 0.5F;
			e.vy = ClawDirY * sv + ClawDirX * side * 0.5F + 260.F;
			e.age = -sp * 0.06F - static_cast<float>(k) * 0.01F;
			e.life = 0.4F + random.Next() * 1.4F;
			e.p = random.Next() * 6.28F;
			e.s = 1.2F + random.Next() * 2.F;
			e.kind = 1;
			e.dim = 1.F;
			e.ox = e.x;
			e.oy = e.y;
			embers_.push_back(e);
		}
	}
}

void Embers::Streaks(std::vector<Streak> &out, float fade) const
{
	out.clear();
	if (fade <= 0)
		return;
	for (const Ember &e : embers_) {
		if (e.age < 0 || !e.moved)
			continue;
		const float u = e.age / e.life;
		const float br = std::min(1.F, u / 0.08F) * std::pow(std::max(0.F, 1.F - u), 1.2F) * (0.75F + 0.25F * std::sin(e.age * 31.F + e.p)) * e.dim * fade;
		if (br <= 0.002F)
			continue;
		float dx = e.x - e.ox;
		float dy = e.y - e.oy;
		const float len = std::hypot(dx, dy);
		if (len > 0) {
			dx /= len;
			dy /= len;
		} else {
			dx = 0;
			dy = 1;
		}
		const float length = std::max(3.F, len * 2.2F);
		const bool hotter = e.kind != 0;
		Streak s;
		s.headX = e.x;
		s.headY = e.y;
		s.tailX = e.x - dx * length;
		s.tailY = e.y - dy * length;
		s.halfWidth = e.s * 0.5F;
		s.r = 1.F;
		s.g = (hotter ? 0.9F : 0.78F) - 0.45F * u;
		s.b = (hotter ? 0.55F : 0.30F) - 0.26F * u;
		s.a = std::min(br, 1.F);
		out.push_back(s);
	}
}

// ---- the burning lines ----

namespace {

/** @brief A line's feed at P (box pixels): fading out toward its ends as it reaches out, thin across. */
float LineAt(float px, float py, float y, float halfWidth, float thickness, float reach)
{
	const float d = std::abs(px - LineMiddleX);
	const float w = halfWidth * reach;
	if (d >= w)
		return 0.F;
	const float u = d / w;
	const float a = (py - y) / thickness;
	return std::pow(1.F - u * u, 0.45F) * std::exp(-a * a);
}

} // namespace

fire::FireSource LineSource()
{
	fire::FireSource source;
	source.width = LineGridWidth;
	source.height = LineGridHeight;
	source.pad = 0;
	const size_t cells = static_cast<size_t>(LineGridWidth) * LineGridHeight;
	source.feed.assign(cells, 0.F);
	source.rise.assign(cells, 0.F);
	source.cool.assign(cells, 0.F);
	// The lower line keeps its flames short, under the words: cooled hard 10 to 36 pixels over it.
	for (int j = 0; j < LineGridHeight; j++) {
		const float py = static_cast<float>(j) + 0.5F + LineGridY;
		const float band = Clamp01((py - BottomLineY) / 10.F) * (1.F - SmoothStep(BottomLineY + 24.F, BottomLineY + 36.F, py));
		std::fill(source.cool.begin() + static_cast<ptrdiff_t>(j) * LineGridWidth, source.cool.begin() + static_cast<ptrdiff_t>(j + 1) * LineGridWidth, 2.4F * band);
	}
	return source;
}

void LineFeed(std::vector<float> &feed, float reach)
{
	feed.assign(static_cast<size_t>(LineGridWidth) * LineGridHeight, 0.F);
	if (reach <= 0.F)
		return;
	for (int j = 0; j < LineGridHeight; j++) {
		const float py = static_cast<float>(j) + 0.5F + LineGridY;
		if (std::abs(py - TopLineY) > 12.F && std::abs(py - BottomLineY) > 12.F)
			continue;
		float *row = feed.data() + static_cast<size_t>(j) * LineGridWidth;
		for (int i = 0; i < LineGridWidth; i++) {
			const float px = static_cast<float>(i) + 0.5F + LineGridX;
			row[i] = LineAt(px, py, TopLineY, TopLineHalfWidth, 2.6F, reach) + 0.85F * LineAt(px, py, BottomLineY, BottomLineHalfWidth, 2.2F, reach);
		}
	}
}

void LineSparks(fire::LiveFire &fire, float dt, float reach, Random &random)
{
	if (reach <= 0.05F)
		return;
	const float want = 14.F * dt;
	int n = static_cast<int>(want) + (random.Next() < want - std::floor(want) ? 1 : 0);
	while (n-- > 0) {
		const bool top = random.Next() < 0.8F;
		const float halfWidth = (top ? TopLineHalfWidth : BottomLineHalfWidth) * reach;
		const float x = LineMiddleX + (random.Next() * 2.F - 1.F) * halfWidth * 0.9F;
		const float y = (top ? TopLineY : BottomLineY) + 4.F;
		fire.AddSparkAt(x - LineGridX, y - LineGridY, 0.F, top ? 0.8F : 0.45F);
	}
}

void PaintSeams(uint8_t *rgba, int pitch, float t, float reach, float red)
{
	if (reach <= 0.F)
		return;
	const float glowRgb[3] = { 1.F, Mix(0.42F, 0.22F, red), Mix(0.08F, 0.04F, red) };
	constexpr float Core[3] = { 1.F, 0.86F, 0.55F };
	const float rw = static_cast<float>(LineGridWidth);
	const float rh = static_cast<float>(LineGridHeight);
	for (int i = 0; i < LineGridWidth; i++) {
		const float gx = static_cast<float>(i) + 0.5F;
		const float px = gx + LineGridX;
		const float dx = std::abs(px - LineMiddleX);
		const float flicker = 0.8F + 0.2F * ValueNoise(px / 14.F, t * 6.F, 5.F) + 0.15F * ValueNoise(px / 4.F, t * 11.F, 9.F);
		const float wobble = 0.6F * ValueNoise(px / 30.F, t, 2.F);
		const float edgeX = Clamp01(gx / 12.F) * Clamp01((rw - gx) / 12.F);
		for (int line = 0; line < 2; line++) {
			const float y = line == 0 ? TopLineY : BottomLineY;
			const float halfWidth = (line == 0 ? TopLineHalfWidth : BottomLineHalfWidth) * reach;
			if (dx >= halfWidth)
				continue;
			const float u = dx / halfWidth;
			const float ends = std::pow(1.F - u * u, 0.6F);
			const float strength = flicker * (line == 0 ? 1.F : 0.85F);
			const int j0 = std::max(0, static_cast<int>(y - LineGridY - 16.F));
			const int j1 = std::min(LineGridHeight - 1, static_cast<int>(y - LineGridY + 16.F));
			for (int j = j0; j <= j1; j++) {
				const float gy = static_cast<float>(j) + 0.5F;
				const float dy = gy + LineGridY - y - wobble;
				const float core = std::exp(-dy * dy / 1.6F) * ends;
				const float halo = std::exp(-dy * dy / 40.F) * ends;
				float ed = Clamp01((rh - gy) / 22.F) * edgeX;
				ed = ed * ed * (3.F - 2.F * ed);
				uint8_t *p = rgba + static_cast<ptrdiff_t>(LineGridHeight - 1 - j) * pitch + static_cast<ptrdiff_t>(i) * 4;
				for (int c = 0; c < 3; c++)
					p[c] = ToByte(static_cast<float>(p[c]) + (Core[c] * core * 1.1F + glowRgb[c] * halo * 0.55F) * strength * ed * 255.F);
			}
		}
	}
}

// ---- the glow spots ----

std::array<GlowSpot, 12> GlowSpots(float t)
{
	std::array<GlowSpot, 12> spots {};
	const double td = t;
	for (int j = 0; j < 12; j++) {
		const double fj = j;
		const double a = std::fmod(std::sin(fj * 12.9898 + 1.0) * 43758.5, 1.0);
		const double b = std::fmod(std::sin(fj * 78.233 + 2.0) * 12345.6, 1.0);
		const double fa = a < 0 ? a + 1.0 : a;
		const double fb = b < 0 ? b + 1.0 : b;
		const auto fr = [](double x) { return x - std::floor(x); };
		const double x = fr(fa + td * 0.010 * (1.0 + fb)) * 1800.0 - 100.0;
		const double y = fr(fb + td * (0.025 + 0.03 * fa)) * 1100.0 - 100.0;
		spots[j].x = static_cast<float>(x / 1600.0);
		spots[j].y = static_cast<float>(1.0 - y / 900.0);
		spots[j].radius = static_cast<float>(20.0 + 34.0 * fa);
		spots[j].bright = static_cast<float>(0.055 * (0.5 + 0.5 * std::sin(td * 1.7 + fj * 2.0)));
	}
	return spots;
}

float GlowSpotProfile(float d)
{
	return (1.F - SmoothStep(0.55F, 1.F, d)) * (0.6F + 0.4F * SmoothStep(0.5F, 0.9F, d));
}

} // namespace intro

} // namespace devilution

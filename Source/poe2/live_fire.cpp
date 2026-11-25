#include "poe2/live_fire.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

// Worker threads where the game has them for sure (as the HD reader, hd.cpp); elsewhere it all runs on the caller's.
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define POE2_FIRE_THREADS 1
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace devilution {

namespace fire {

namespace {

constexpr float Pi = 3.14159265358979F;

// ---- value noise ----
// The reference (a WebGL page) works out 3D value noise at every point. Here each octave's lattice is worked out once a
// frame on the plane of its time (z), which every point of a frame shares, so a point costs four table reads.

uint32_t Hash3(int32_t x, int32_t y, int32_t z)
{
	uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343U ^ static_cast<uint32_t>(y) * 0xD8163841U ^ static_cast<uint32_t>(z) * 0xCB1AB31FU;
	h ^= h >> 16;
	h *= 0x7FEB352DU;
	h ^= h >> 15;
	h *= 0x846CA68BU;
	h ^= h >> 16;
	return h;
}

float HashUnit(int32_t x, int32_t y, int32_t z)
{
	return static_cast<float>(Hash3(x, y, z) >> 8) * (1.F / 16777216.F);
}

float Smooth(float t)
{
	return t * t * (3.F - 2.F * t);
}

/** @brief One octave of value noise on a frame's time plane: lattice x = X * scale + offsetX (y the same). */
struct NoiseLayer {
	float scale = 1;
	float fracX = 0;
	float fracY = 0;
	int x0 = 0;
	int y0 = 0;
	int nx = 0;
	int ny = 0;
	/** @brief The lattice values, already spread to about -1.73..1.73 and times the octave's weight. */
	std::vector<float> v;

	/** @brief Works out the lattice that covers X in [xMin, xMax] and Y in [yMin, yMax]. */
	void Prepare(double scaleIn, double offsetX, double offsetY, double z, float weight, float xMin, float xMax, float yMin, float yMax)
	{
		scale = static_cast<float>(scaleIn);
		// The integer part of the offsets (they grow with time) goes into the lattice index, so they never lose detail.
		const double baseXd = std::floor(offsetX);
		const double baseYd = std::floor(offsetY);
		const double baseZd = std::floor(z);
		fracX = static_cast<float>(offsetX - baseXd);
		fracY = static_cast<float>(offsetY - baseYd);
		const auto baseX = static_cast<int32_t>(static_cast<int64_t>(baseXd));
		const auto baseY = static_cast<int32_t>(static_cast<int64_t>(baseYd));
		const auto baseZ = static_cast<int32_t>(static_cast<int64_t>(baseZd));
		const float fz = Smooth(static_cast<float>(z - baseZd));
		x0 = static_cast<int>(std::floor(xMin * scale + fracX));
		y0 = static_cast<int>(std::floor(yMin * scale + fracY));
		nx = static_cast<int>(std::floor(xMax * scale + fracX)) - x0 + 2;
		ny = static_cast<int>(std::floor(yMax * scale + fracY)) - y0 + 2;
		v.resize(static_cast<size_t>(nx) * ny);
		for (int j = 0; j < ny; j++) {
			for (int i = 0; i < nx; i++) {
				const int32_t lx = baseX + x0 + i;
				const int32_t ly = baseY + y0 + j;
				const float a = HashUnit(lx, ly, baseZ);
				const float b = HashUnit(lx, ly, baseZ + 1);
				v[static_cast<size_t>(j) * nx + i] = ((a + (b - a) * fz) * 2.F - 1.F) * 1.73F * weight;
			}
		}
	}
};

/** @brief Where each of a list of coordinates falls on a layer's lattice: its cell and smoothed weight. */
struct Axis {
	std::vector<int> cell;
	std::vector<float> weight;

	void Make(const NoiseLayer &layer, bool alongX, const float *coords, size_t count)
	{
		cell.resize(count);
		weight.resize(count);
		const float frac = alongX ? layer.fracX : layer.fracY;
		const int origin = alongX ? layer.x0 : layer.y0;
		const int last = (alongX ? layer.nx : layer.ny) - 2;
		for (size_t k = 0; k < count; k++) {
			const float l = coords[k] * layer.scale + frac;
			const float f = std::floor(l);
			cell[k] = std::clamp(static_cast<int>(f) - origin, 0, last);
			weight[k] = Smooth(std::clamp(l - f, 0.F, 1.F));
		}
	}
};

/** @brief Adds one layer's noise along a row: `out[k] += noise(xs[k], y)`, the row's place given by its axis entry. */
void AddRow(const NoiseLayer &layer, const Axis &xs, int rowCell, float rowWeight, float *out, size_t count)
{
	const float *r0 = layer.v.data() + static_cast<size_t>(rowCell) * layer.nx;
	const float *r1 = r0 + layer.nx;
	const int *cells = xs.cell.data();
	const float *weights = xs.weight.data();
	for (size_t k = 0; k < count; k++) {
		const int c = cells[k];
		const float w = weights[k];
		const float a = r0[c] + (r0[c + 1] - r0[c]) * w;
		const float b = r1[c] + (r1[c + 1] - r1[c]) * w;
		out[k] += a + (b - a) * rowWeight;
	}
}

/**
 * @brief The fbm octaves of one noise of the reference: fbm2 (two octaves) or fbm3 (three). Point P (grid pixels) is
 * at lattice ((P - (0, speed * t)) / size) * 2^o + offset_o, time z * 2^0 + zOffset_o; octave o weighs 2^-o / total.
 */
void PrepareFbm(NoiseLayer *layers, int octaves, double t, float size, float speed, double z, float amplitude, float xMin, float xMax, float yMin, float yMax)
{
	static constexpr float Offsets[] = { 0.F, 17.F, 31.F };
	static constexpr float ZOffsets[] = { 0.F, 7.F, 13.F };
	const float total = octaves == 3 ? 1.75F : 1.5F;
	for (int o = 0; o < octaves; o++) {
		const double m = static_cast<double>(1 << o);
		const double scale = m / size;
		const double offsetY = -speed * t / size * m + Offsets[o];
		layers[o].Prepare(scale, Offsets[o], offsetY, z + ZOffsets[o], amplitude * static_cast<float>(1.0 / m) / total, xMin, xMax, yMin, yMax);
	}
}

/** @brief A bilinear sample of a grid as the graphics card's (texel centres at +0.5, clamped at the edges). */
float Bilinear(const float *grid, int width, int height, float tx, float ty)
{
	tx = std::clamp(tx, 0.F, static_cast<float>(width - 1));
	ty = std::clamp(ty, 0.F, static_cast<float>(height - 1));
	const int x0 = std::min(static_cast<int>(tx), width - 1);
	const int y0 = std::min(static_cast<int>(ty), height - 1);
	const int x1 = std::min(x0 + 1, width - 1);
	const int y1 = std::min(y0 + 1, height - 1);
	const float fx = tx - static_cast<float>(x0);
	const float fy = ty - static_cast<float>(y0);
	const float *r0 = grid + static_cast<size_t>(y0) * width;
	const float *r1 = grid + static_cast<size_t>(y1) * width;
	const float a = r0[x0] + (r0[x1] - r0[x0]) * fx;
	const float b = r1[x0] + (r1[x1] - r1[x0]) * fx;
	return a + (b - a) * fy;
}

/** @brief A coordinate's two texels and weight along one axis of a grid `size` long, edges clamped. */
struct Tap {
	int i0, i1;
	float w;
};

Tap TapAt(float t, int size)
{
	t = std::clamp(t, 0.F, static_cast<float>(size - 1));
	const int i0 = std::min(static_cast<int>(t), size - 1);
	return { i0, std::min(i0 + 1, size - 1), t - static_cast<float>(i0) };
}

// The colour ramp, heat: r g b alpha. Yellow and hell-red; Curtis's settings mix them half and half.
struct RampStop {
	float heat;
	float y[4];
	float r[4];
};
constexpr RampStop Ramp[] = {
	{ 0.F, { 20, 4, 2, 0 }, { 14, 2, 2, 0 } },
	{ .07F, { 46, 9, 4, .45F }, { 38, 5, 4, .5F } },
	{ .20F, { 120, 22, 6, .85F }, { 96, 10, 6, .88F } },
	{ .36F, { 205, 62, 8, 1 }, { 160, 22, 8, 1 } },
	{ .55F, { 246, 128, 16, 1 }, { 214, 52, 10, 1 } },
	{ .75F, { 255, 184, 48, 1 }, { 240, 96, 18, 1 } },
	{ .95F, { 255, 222, 110, 1 }, { 252, 150, 40, 1 } },
	{ 1.25F, { 255, 238, 170, 1 }, { 255, 196, 90, 1 } },
	{ 1.7F, { 255, 244, 196, 1 }, { 255, 220, 140, 1 } },
};
constexpr int RampSteps = 1024;
constexpr float MaxHeat = 1.7F;

// The coarse wind grid: 70 x 28 cells over the logo's 1100 x 432.
constexpr float WindCell = 1100.F / 70.F;

/** @brief Rows a task takes at a time. */
constexpr int RowsPerTask = 8;

int TasksFor(int rows)
{
	return (rows + RowsPerTask - 1) / RowsPerTask;
}

uint8_t ToByte(float v)
{
	return static_cast<uint8_t>(std::clamp(v, 0.F, 255.F) + 0.5F);
}

} // namespace

// ---- worker threads ----

class LiveFire::Workers {
public:
	explicit Workers(int count)
	{
#ifdef POE2_FIRE_THREADS
		for (int i = 0; i < count; i++)
			threads_.emplace_back([this, i] { Loop(static_cast<size_t>(i)); });
#else
		(void)count;
#endif
	}

	~Workers()
	{
#ifdef POE2_FIRE_THREADS
		{
			const std::lock_guard<std::mutex> guard(lock_);
			quit_ = true;
		}
		wake_.notify_all();
		for (std::thread &thread : threads_)
			thread.join();
#endif
	}

	Workers(const Workers &) = delete;
	Workers &operator=(const Workers &) = delete;

	/** @brief Runs `job(task)` for each task in [0, tasks), on the workers and the caller's thread, and waits for all. */
	void Run(int tasks, const std::function<void(int)> &job)
	{
#ifdef POE2_FIRE_THREADS
		const size_t helpers = std::min(threads_.size(), limit_);
		if (helpers > 0 && tasks > 1) {
			{
				const std::lock_guard<std::mutex> guard(lock_);
				job_ = &job;
				tasks_ = tasks;
				next_.store(0);
				active_ = helpers;
				pending_ = helpers;
				generation_++;
			}
			wake_.notify_all();
			Work();
			std::unique_lock<std::mutex> guard(lock_);
			done_.wait(guard, [this] { return pending_ == 0; });
			job_ = nullptr;
			return;
		}
#endif
		for (int task = 0; task < tasks; task++)
			job(task);
	}

	/** @brief At most this many of the workers help (the rest sleep on). */
	void Limit(size_t count) { limit_ = count; }

private:
	size_t limit_ = SIZE_MAX;
#ifdef POE2_FIRE_THREADS
	void Work()
	{
		for (int task = next_.fetch_add(1); task < tasks_; task = next_.fetch_add(1))
			(*job_)(task);
	}

	void Loop(size_t index)
	{
		uint64_t seen = 0;
		for (;;) {
			{
				std::unique_lock<std::mutex> guard(lock_);
				wake_.wait(guard, [&] { return quit_ || generation_ != seen; });
				if (quit_)
					return;
				seen = generation_;
				// Not one of this job's helpers.
				if (index >= active_)
					continue;
			}
			Work();
			{
				const std::lock_guard<std::mutex> guard(lock_);
				if (--pending_ == 0)
					done_.notify_one();
			}
		}
	}

	std::vector<std::thread> threads_;
	std::mutex lock_;
	std::condition_variable wake_;
	std::condition_variable done_;
	const std::function<void(int)> *job_ = nullptr;
	int tasks_ = 0;
	std::atomic<int> next_ { 0 };
	size_t pending_ = 0;
	size_t active_ = 0;
	uint64_t generation_ = 0;
	bool quit_ = false;
#endif
};

// ---- maps and presets ----

bool SourceFromMaps(const uint8_t *rgba, int width, int height, int pitch, FireSource &source, int pad)
{
	if (rgba == nullptr || width <= 0 || height <= 0)
		return false;
	pad = std::max(pad, 0);
	const int gridWidth = width + 2 * pad;
	source.width = gridWidth;
	source.height = height;
	source.pad = pad;
	const size_t cells = static_cast<size_t>(gridWidth) * height;
	source.feed.assign(cells, 0.F);
	source.rise.assign(cells, 0.F);
	source.sparkX.clear();
	source.sparkY.clear();
	for (int row = 0; row < height; row++) {
		const uint8_t *line = rgba + static_cast<ptrdiff_t>(row) * pitch;
		const int y = height - 1 - row;
		for (int x = 0; x < width; x++) {
			const uint8_t *p = line + static_cast<ptrdiff_t>(x) * 4;
			const size_t i = static_cast<size_t>(y) * gridWidth + x + pad;
			source.feed[i] = p[0] / 255.F * 1.6F;
			source.rise[i] = p[1] / 255.F;
			if ((row & 1) == 0 && (x & 1) == 0 && p[2] > 64) {
				source.sparkX.push_back(static_cast<float>(x + pad));
				source.sparkY.push_back(static_cast<float>(y));
			}
		}
		// The padding has no feed and no sparks, and the height above the source of the picture's edge (as a graphics
		// card clamps a texture read past it).
		float *rise = &source.rise[static_cast<size_t>(y) * gridWidth];
		std::fill(rise, rise + pad, rise[pad]);
		std::fill(rise + pad + width, rise + gridWidth, rise[pad + width - 1]);
	}
	return true;
}

std::vector<FireZone> LogoZones()
{
	// D I A B L O, from and to in logo pixels (the grid is twice the logo).
	static constexpr int Letters[6][2] = { { 23, 101 }, { 134, 165 }, { 201, 249 }, { 283, 328 }, { 366, 408 }, { 440, 516 } };
	std::vector<FireZone> zones;
	for (int i = 0; i < 6; i++) {
		const float from = static_cast<float>(Letters[i][0]);
		const float to = static_cast<float>(Letters[i][1]);
		FireZone zone;
		zone.centre = from + to;
		zone.halfWidth = (to - from) + 28.F;
		zone.sparkHalfWidth = to - from;
		// Big letters breathe slower.
		zone.beat = 2.6F + 2.2F * (1.F - ((to - from) - 31.F) / 47.F);
		zone.phase = static_cast<float>(i) * 1.7F + 0.4F;
		zones.push_back(zone);
	}
	return zones;
}

FireRecipe LineRecipe()
{
	// FS_AIR2 / FS_SIM2 / FS_FIRE2 of the intro (v18): noise about half the size, slower to rise and quicker to cool.
	FireRecipe r;
	r.airDivisor = 2;
	r.swirlSize[0] = 34.F;
	r.swirlSize[1] = 13.F;
	r.swirlSpeed[0] = 70.F;
	r.swirlSpeed[1] = 140.F;
	r.swirlRate[0] = 1.7F;
	r.swirlRate[1] = 2.8F;
	r.swirlStrength[0] = 20.F;
	r.swirlStrength[1] = 16.F;
	r.swirlStep = 1.F;
	r.erosionSize = 15.F;
	r.erosionSpeed = 200.F;
	r.erosionRate = 2.2F;
	r.tongueSize = 11.F;
	r.tongueRate = 2.6F;
	r.tongueMin = 0.12F;
	r.tongueMax = 1.6F;
	r.rise = 78.F;
	r.buoy = 135.F;
	r.cool = 0.28F;
	r.erode = 1.5F;
	r.emit = 12.F;
	r.swirlX = 1.2F;
	r.swirlY = 1.F;
	r.byHeight = false;
	r.topFade = 30.F;
	r.detailSize = 5.F;
	r.detailSpeed = 300.F;
	r.shown = 0.9F;
	r.glowLevel = 3.6F;
	r.glowAlpha = 0.6F;
	r.edgeTop = 22.F;
	r.edgeSide = 12.F;
	r.sparkRate = 0.F;
	return r;
}

RampColour RampAt(float heat, float red)
{
	heat = std::clamp(heat, 0.F, MaxHeat);
	size_t j = 0;
	constexpr size_t Stops = sizeof(Ramp) / sizeof(Ramp[0]);
	while (j < Stops - 2 && heat > Ramp[j + 1].heat)
		j++;
	const float u = std::clamp((heat - Ramp[j].heat) / (Ramp[j + 1].heat - Ramp[j].heat), 0.F, 1.F);
	float c[4];
	for (int k = 0; k < 4; k++) {
		const float y = Ramp[j].y[k] + (Ramp[j + 1].y[k] - Ramp[j].y[k]) * u;
		const float r = Ramp[j].r[k] + (Ramp[j + 1].r[k] - Ramp[j].r[k]) * u;
		c[k] = y + (r - y) * red;
	}
	return { c[0], c[1], c[2], c[3] };
}

// ---- the fire ----

LiveFire::LiveFire(FireSource source, FireSettings settings, uint32_t seed, int threads, const FireRecipe &recipe)
    : recipe_(recipe)
    , width_(std::max(source.width, 4))
    , height_(std::max(source.height, 4))
    , pad_(std::clamp(source.pad, 0, (std::max(source.width, 4) - 1) / 2))
    , source_(std::move(source))
    , settings_(settings)
    , rng_(seed)
{
	const size_t cells = static_cast<size_t>(width_) * height_;
	source_.width = width_;
	source_.height = height_;
	source_.pad = pad_;
	source_.feed.resize(cells, 0.F);
	source_.rise.resize(cells, 0.F);
	recipe_.airDivisor = std::max(recipe_.airDivisor, 1);
	recipe_.glowLevel = std::clamp(recipe_.glowLevel, 1.F, 4.99F);
	airWidth_ = std::max(1, width_ / recipe_.airDivisor);
	airHeight_ = std::max(1, height_ / recipe_.airDivisor);
	if (!source_.cool.empty())
		source_.cool.resize(cells, 0.F);
	lightWidth_ = std::max(1, PictureWidth() / LightDivisor);
	lightHeight_ = std::max(1, height_ / LightDivisor);
	heat_.assign(cells, 0.F);
	next_.assign(cells, 0.F);
	air_.assign(static_cast<size_t>(airWidth_) * airHeight_ * 4, 0.F);
	const int windWidth = std::max(1, static_cast<int>(std::lround(width_ / WindCell)));
	const int windHeight = std::max(1, static_cast<int>(std::lround(height_ / WindCell)));
	wind_.assign(static_cast<size_t>(windWidth) * windHeight * 2, 0.F);
	columnGain_.assign(width_, 1.F);
	flareAt_.assign(source_.zones.size(), -10.0);
	// Mip levels down to the one the glow reads (5).
	int w = width_;
	int h = height_;
	for (int level = 1; level <= 5; level++) {
		w = std::max(1, w / 2);
		h = std::max(1, h / 2);
		mipWidth_.push_back(w);
		mipHeight_.push_back(h);
		mips_.emplace_back(static_cast<size_t>(w) * h, 0.F);
	}
	BuildRamp();
#ifdef POE2_FIRE_THREADS
	if (threads < 0) {
		const unsigned cores = std::thread::hardware_concurrency();
		threads = std::clamp(static_cast<int>(cores) - 2, 0, 7);
	}
#else
	threads = 0;
#endif
	workers_ = std::make_unique<Workers>(threads);
}

LiveFire::~LiveFire() = default;

void LiveFire::SetSettings(const FireSettings &settings)
{
	const bool recolour = settings.red != settings_.red;
	settings_ = settings;
	if (recolour)
		BuildRamp();
}

void LiveFire::BuildRamp()
{
	ramp_.resize(static_cast<size_t>(RampSteps) * 4);
	for (int i = 0; i < RampSteps; i++) {
		const RampColour c = RampAt(static_cast<float>(i) / (RampSteps - 1) * MaxHeat, settings_.red);
		ramp_[i * 4 + 0] = c.r * c.a;
		ramp_[i * 4 + 1] = c.g * c.a;
		ramp_[i * 4 + 2] = c.b * c.a;
		ramp_[i * 4 + 3] = c.a;
	}
	const float red = settings_.red;
	glowR_ = (1.F + (0.9F - 1.F) * red) * 255.F;
	glowG_ = (0.33F + (0.12F - 0.33F) * red) * 255.F;
	glowB_ = (0.06F + (0.03F - 0.06F) * red) * 255.F;
}

float LiveFire::Random()
{
	// The reference page's generator (mulberry32), so a seed gives the same sparks and flare-ups.
	rng_ += 0x6D2B79F5U;
	uint32_t t = rng_;
	t = (t ^ (t >> 15)) * (1U | rng_);
	t = (t + ((t ^ (t >> 7)) * (61U | t))) ^ t;
	return static_cast<float>(t ^ (t >> 14)) / 4294967296.F;
}

float LiveFire::FlareGain(size_t zone, double t) const
{
	const double d = t - flareAt_[zone];
	if (d < 0 || d > 1.3)
		return 1.F;
	// It pulls back first, then bursts and settles.
	if (d < 0.25)
		return static_cast<float>(1.0 - 0.35 * std::sin(Pi * d / 0.25));
	const double e = d - 0.25;
	return static_cast<float>(1.0 + 1.1 * std::exp(-e / 0.28) * (1.0 - std::exp(-e / 0.04)));
}

void LiveFire::Splat(float x, float y, float vx, float vy)
{
	const int windWidth = std::max(1, static_cast<int>(std::lround(width_ / WindCell)));
	const int windHeight = static_cast<int>(wind_.size() / 2) / windWidth;
	const float cell = static_cast<float>(width_) / static_cast<float>(windWidth);
	const float r = 52.F / cell;
	const float gx = x / cell - 0.5F;
	const float gy = y / cell - 0.5F;
	const int j0 = std::max(0, static_cast<int>(std::floor(gy - 2 * r)));
	const int j1 = std::min(windHeight - 1, static_cast<int>(std::ceil(gy + 2 * r)));
	const int i0 = std::max(0, static_cast<int>(std::floor(gx - 2 * r)));
	const int i1 = std::min(windWidth - 1, static_cast<int>(std::ceil(gx + 2 * r)));
	for (int j = j0; j <= j1; j++) {
		for (int i = i0; i <= i1; i++) {
			const float dx = static_cast<float>(i) - gx;
			const float dy = static_cast<float>(j) - gy;
			const float f = std::exp(-(dx * dx + dy * dy) / (r * r)) * 0.7F;
			float *q = &wind_[(static_cast<size_t>(j) * windWidth + i) * 2];
			q[0] += (vx - q[0]) * f;
			q[1] += (vy - q[1]) * f;
		}
	}
	windAny_ = true;
}

void LiveFire::WindAt(float x, float y, float &wx, float &wy) const
{
	const int windWidth = std::max(1, static_cast<int>(std::lround(width_ / WindCell)));
	const int windHeight = static_cast<int>(wind_.size() / 2) / windWidth;
	const float cell = static_cast<float>(width_) / static_cast<float>(windWidth);
	const int i = std::clamp(static_cast<int>(x / cell), 0, windWidth - 1);
	const int j = std::clamp(static_cast<int>(y / cell), 0, windHeight - 1);
	const float *q = &wind_[(static_cast<size_t>(j) * windWidth + i) * 2];
	wx = q[0];
	wy = q[1];
}

void LiveFire::Push(float x0, float y0, float x1, float y1, float seconds)
{
	const float dt = std::max(0.004F, seconds);
	const float vx = (x1 - x0) / dt;
	const float vy = (y1 - y0) / dt;
	const float speed = std::hypot(vx, vy);
	if (speed <= 8.F || dt >= 0.2F || settings_.wind <= 0.F)
		return;
	constexpr float Limit = 1040.F;
	const float f = (speed > Limit ? Limit / speed : 1.F) * 0.55F * settings_.wind;
	const int steps = std::clamp(static_cast<int>(std::lround(std::hypot(x1 - x0, y1 - y0) / 20.F)), 1, 8);
	for (int s = 1; s <= steps; s++) {
		const float k = static_cast<float>(s) / static_cast<float>(steps);
		Splat(x0 + (x1 - x0) * k, y0 + (y1 - y0) * k, vx * f, vy * f * 0.6F);
	}
}

void LiveFire::AddSpark(float x, float y, float delay, float push)
{
	if (sparks_.size() >= 4000)
		return;
	Spark s {};
	s.x = x + Random() * 6.F - 3.F;
	s.y = y + Random() * 6.F - 3.F;
	s.vx = (Random() - 0.5F) * 44.F * push;
	s.vy = (80.F + Random() * 110.F) * push;
	s.age = -delay;
	s.life = 0.7F + Random() * 1.2F;
	s.p = Random() * 6.28F;
	s.s = 1.F + Random() * 1.1F;
	s.ox = s.x;
	s.oy = s.y;
	s.moved = false;
	sparks_.push_back(s);
}

void LiveFire::Flare(size_t zone)
{
	FlareWith(zone, time_, 0.25F);
}

void LiveFire::FlareWith(size_t zone, double at, float sparkDelay)
{
	if (zone >= source_.zones.size())
		return;
	flareAt_[zone] = at;
	if (warming_)
		return;
	const FireZone &z = source_.zones[zone];
	const float reach = z.sparkHalfWidth > 0 ? z.sparkHalfWidth : z.halfWidth;
	std::vector<size_t> mine;
	for (size_t i = 0; i < source_.sparkX.size(); i++) {
		if (std::abs(source_.sparkX[i] - z.centre) < reach)
			mine.push_back(i);
	}
	const int n = static_cast<int>(std::lround(50.F * settings_.sparks));
	for (int k = 0; k < n && !mine.empty(); k++) {
		const size_t p = mine[std::min(mine.size() - 1, static_cast<size_t>(Random() * static_cast<float>(mine.size())))];
		AddSpark(source_.sparkX[p], source_.sparkY[p], sparkDelay + Random() * 0.25F, 1.6F);
	}
}

void LiveFire::BuildAir()
{
	const double t = time_;
	const float cx = static_cast<float>(width_) / static_cast<float>(airWidth_);
	const float cy = static_cast<float>(height_) / static_cast<float>(airHeight_);
	const float E = recipe_.swirlStep;
	std::vector<float> xc(airWidth_), yc(airHeight_), xs(static_cast<size_t>(airWidth_) * 2), ys(static_cast<size_t>(airHeight_) * 2);
	for (int i = 0; i < airWidth_; i++) {
		xc[i] = cx * (static_cast<float>(i) + 0.5F);
		xs[i * 2] = xc[i] - E;
		xs[i * 2 + 1] = xc[i] + E;
	}
	for (int j = 0; j < airHeight_; j++) {
		yc[j] = cy * (static_cast<float>(j) + 0.5F);
		ys[j * 2] = yc[j] - E;
		ys[j * 2 + 1] = yc[j] + E;
	}
	const float xMin = -E - 1.F;
	const float xMax = static_cast<float>(width_) + E + 1.F;
	const float yMin = -E - 1.F;
	const float yMax = static_cast<float>(height_) + E + 1.F;
	// The stream function: two fbm2, the big slow one and the small quick one.
	NoiseLayer psi[4];
	const FireRecipe &r = recipe_;
	for (int l = 0; l < 2; l++)
		PrepareFbm(psi + l * 2, 2, t, r.swirlSize[l], r.swirlSpeed[l], t * r.swirlRate[l], r.swirlStrength[l] * r.swirlSize[l], xMin, xMax, yMin, yMax);
	NoiseLayer erosion[3];
	PrepareFbm(erosion, 3, t, r.erosionSize, r.erosionSpeed, t * r.erosionRate, 1.F, xMin, xMax, yMin, yMax);
	NoiseLayer tongue[2];
	PrepareFbm(tongue, 2, t, r.tongueSize, 0.F, t * r.tongueRate, 1.F, xMin, xMax, yMin, yMax);
	// Each layer's axes: psi at x +-E (for d/dx) and at y +-E (for d/dy); erosion and tongue at the cell centres.
	struct Axes {
		Axis xs, xc, ys, yc;
	};
	Axes psiAxes[4], erosionAxes[3], tongueAxes[2];
	for (int l = 0; l < 4; l++) {
		psiAxes[l].xs.Make(psi[l], true, xs.data(), xs.size());
		psiAxes[l].xc.Make(psi[l], true, xc.data(), xc.size());
		psiAxes[l].ys.Make(psi[l], false, ys.data(), ys.size());
		psiAxes[l].yc.Make(psi[l], false, yc.data(), yc.size());
	}
	for (int l = 0; l < 3; l++) {
		erosionAxes[l].xc.Make(erosion[l], true, xc.data(), xc.size());
		erosionAxes[l].yc.Make(erosion[l], false, yc.data(), yc.size());
	}
	for (int l = 0; l < 2; l++) {
		tongueAxes[l].xc.Make(tongue[l], true, xc.data(), xc.size());
		tongueAxes[l].yc.Make(tongue[l], false, yc.data(), yc.size());
	}
	const int aw = airWidth_;
	workers_->Run(TasksFor(airHeight_), [&](int task) {
		std::vector<float> alongX(static_cast<size_t>(aw) * 2), below(aw), above(aw), er(aw), tg(aw);
		const int jEnd = std::min(airHeight_, (task + 1) * RowsPerTask);
		for (int j = task * RowsPerTask; j < jEnd; j++) {
			std::fill(alongX.begin(), alongX.end(), 0.F);
			std::fill(below.begin(), below.end(), 0.F);
			std::fill(above.begin(), above.end(), 0.F);
			std::fill(er.begin(), er.end(), 0.F);
			std::fill(tg.begin(), tg.end(), 0.F);
			for (int l = 0; l < 4; l++) {
				const Axes &a = psiAxes[l];
				AddRow(psi[l], a.xs, a.yc.cell[j], a.yc.weight[j], alongX.data(), alongX.size());
				AddRow(psi[l], a.xc, a.ys.cell[j * 2], a.ys.weight[j * 2], below.data(), below.size());
				AddRow(psi[l], a.xc, a.ys.cell[j * 2 + 1], a.ys.weight[j * 2 + 1], above.data(), above.size());
			}
			for (int l = 0; l < 3; l++)
				AddRow(erosion[l], erosionAxes[l].xc, erosionAxes[l].yc.cell[j], erosionAxes[l].yc.weight[j], er.data(), er.size());
			for (int l = 0; l < 2; l++)
				AddRow(tongue[l], tongueAxes[l].xc, tongueAxes[l].yc.cell[j], tongueAxes[l].yc.weight[j], tg.data(), tg.size());
			float *out = &air_[static_cast<size_t>(j) * aw * 4];
			for (int i = 0; i < aw; i++) {
				// turb = (dPsi/dy, -dPsi/dx), by central difference.
				out[i * 4 + 0] = (above[i] - below[i]) / (2.F * E);
				out[i * 4 + 1] = -(alongX[i * 2 + 1] - alongX[i * 2]) / (2.F * E);
				out[i * 4 + 2] = er[i];
				out[i * 4 + 3] = tg[i];
			}
		}
	});
}

void LiveFire::Simulate(float dt)
{
	const FireSettings &k = settings_;
	const float height = std::max(k.height, 0.05F);
	const FireRecipe &recipe = recipe_;
	const float rise = recipe.rise * height;
	const float buoy = recipe.buoy * height;
	const float cool = recipe.cool / height;
	const float erode = recipe.erode / height;
	const float emit = recipe.emit * k.amount;
	const float wild = k.wild;
	const bool byHeight = recipe.byHeight;
	const float swirlX = recipe.swirlX;
	const float swirlY = recipe.swirlY;
	const float tongueMin = recipe.tongueMin;
	const float tongueMax = recipe.tongueMax;
	const float topFade = recipe.topFade;
	const float *extraCool = source_.cool.empty() ? nullptr : source_.cool.data();
	const int w = width_;
	const int h = height_;
	const float fw = static_cast<float>(w);
	const float fh = static_cast<float>(h);
	const int aw = airWidth_;
	const int ah = airHeight_;
	const float airX = static_cast<float>(aw) / fw;
	const float airY = static_cast<float>(ah) / fh;
	const int windWidth = std::max(1, static_cast<int>(std::lround(w / WindCell)));
	const int windHeight = static_cast<int>(wind_.size() / 2) / windWidth;
	const bool windy = windAny_;
	// Along a row, each column's taps into the air and the wind are the same for every row.
	std::vector<Tap> airTaps(w), windTaps(windy ? w : 0);
	for (int i = 0; i < w; i++) {
		const float x = static_cast<float>(i) + 0.5F;
		airTaps[i] = TapAt(x * airX - 0.5F, aw);
		if (windy)
			windTaps[i] = TapAt(x / fw * static_cast<float>(windWidth) - 0.5F, windWidth);
	}
	const float *heat = heat_.data();
	float *next = next_.data();
	const float *air = air_.data();
	const float *wind = wind_.data();
	const float *feed = source_.feed.data();
	const float *riseMap = source_.rise.data();
	const float *gain = columnGain_.data();
	const float maxX = fw - 1.F;
	const float maxY = fh - 1.F;
	workers_->Run(TasksFor(h), [&](int task) {
		// This row of the air and of the wind, already blended between the rows of their grids.
		std::vector<float> airRow(static_cast<size_t>(aw) * 4), windRow(windy ? static_cast<size_t>(windWidth) * 2 : 0);
		const int jEnd = std::min(h, (task + 1) * RowsPerTask);
		for (int j = task * RowsPerTask; j < jEnd; j++) {
			const float y = static_cast<float>(j) + 0.5F;
			const Tap ay = TapAt(y * airY - 0.5F, ah);
			const float *a0 = air + static_cast<size_t>(ay.i0) * aw * 4;
			const float *a1 = air + static_cast<size_t>(ay.i1) * aw * 4;
			for (size_t k = 0; k < airRow.size(); k++)
				airRow[k] = a0[k] + (a1[k] - a0[k]) * ay.w;
			if (windy) {
				const Tap wy = TapAt(y / fh * static_cast<float>(windHeight) - 0.5F, windHeight);
				const float *w0 = wind + static_cast<size_t>(wy.i0) * windWidth * 2;
				const float *w1 = wind + static_cast<size_t>(wy.i1) * windWidth * 2;
				for (size_t k = 0; k < windRow.size(); k++)
					windRow[k] = w0[k] + (w1[k] - w0[k]) * wy.w;
			}
			// Near the top of the picture the heat fades out, so nothing reaches its edge.
			const float f = std::clamp(1.F - (fh - y) / topFade, 0.F, 1.F);
			const float fade = dt * 6.F * f * f;
			const size_t row = static_cast<size_t>(j) * w;
			const float *ar = airRow.data();
			for (int i = 0; i < w; i++) {
				const Tap &ax = airTaps[i];
				const float *l = ar + ax.i0 * 4;
				const float *r = ar + ax.i1 * 4;
				const float c0 = l[0] + (r[0] - l[0]) * ax.w;
				const float c1 = l[1] + (r[1] - l[1]) * ax.w;
				const float c2 = l[2] + (r[2] - l[2]) * ax.w;
				const float c3 = l[3] + (r[3] - l[3]) * ax.w;
				const float hgt = byHeight ? riseMap[row + i] : 1.F;
				const float t = heat[row + i];
				const float turb = (byHeight ? 0.35F + 0.65F * hgt : 1.F) * wild;
				float vx = c0 * turb * swirlX;
				float vy = c1 * turb * swirlY + rise + buoy * std::min(t, 1.2F);
				if (windy) {
					const Tap &wx = windTaps[i];
					const float *wl = windRow.data() + wx.i0 * 2;
					const float *wr = windRow.data() + wx.i1 * 2;
					vx += wl[0] + (wr[0] - wl[0]) * wx.w;
					vy += wl[1] + (wr[1] - wl[1]) * wx.w;
				}
				// Carried along: the heat that is here now was upwind of here a step ago.
				const float qx = static_cast<float>(i) - vx * dt;
				const float qy = static_cast<float>(j) - vy * dt;
				float n = 0.F;
				if (qx >= -0.5F && qx <= fw - 0.5F && qy >= -0.5F && qy <= fh - 0.5F) {
					// Bilinear, the edges clamped, as the graphics card's.
					const float tx = std::clamp(qx, 0.F, maxX);
					const float ty = std::clamp(qy, 0.F, maxY);
					const int x0 = static_cast<int>(tx);
					const int y0 = static_cast<int>(ty);
					const int x1 = x0 + (x0 < w - 1 ? 1 : 0);
					const size_t r0 = static_cast<size_t>(y0) * w;
					const size_t r1 = y0 < h - 1 ? r0 + w : r0;
					const float fx = tx - static_cast<float>(x0);
					const float fy = ty - static_cast<float>(y0);
					const float top = heat[r0 + x0] + (heat[r0 + x1] - heat[r0 + x0]) * fx;
					const float bottom = heat[r1 + x0] + (heat[r1 + x1] - heat[r1 + x0]) * fx;
					n = top + (bottom - top) * fy;
				}
				const float e = std::clamp(0.5F + c2 * 1.3F, 0.F, 1.6F);
				n -= dt * (cool + erode * e * (0.25F + 0.75F * hgt));
				n += dt * emit * feed[row + i] * gain[i] * std::clamp(0.55F + c3 * 1.5F, tongueMin, tongueMax);
				if (extraCool != nullptr)
					n -= dt * extraCool[row + i];
				n -= fade;
				next[row + i] = std::clamp(n, 0.F, MaxHeat);
			}
		}
	});
	std::swap(heat_, next_);
}

void LiveFire::UpdateSparks(float dt)
{
	const float fw = static_cast<float>(width_);
	const float fh = static_cast<float>(height_);
	for (Spark &s : sparks_) {
		s.age += dt;
		if (s.age < 0)
			continue;
		float wx;
		float wy;
		WindAt(s.x, s.y, wx, wy);
		if (std::abs(wx) > 8.F)
			s.vx += (wx * 1.2F - s.vx) * std::min(1.F, dt * 3.F);
		s.vx += std::sin(s.p + s.age * 7.F) * 60.F * dt;
		s.vy += wy * dt * 2.F;
		s.ox = s.x;
		s.oy = s.y;
		s.moved = true;
		s.x += s.vx * dt;
		s.y += s.vy * dt * (1.F - 0.25F * s.age / s.life);
	}
	sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [&](const Spark &s) {
		return !(s.age < s.life && s.y < fh - 6.F && s.x > 6.F && s.x < fw - 6.F);
	}),
	    sparks_.end());
}

void LiveFire::Step(float dt)
{
	if (dt <= 0)
		return;
	time_ += dt;
	// Each zone breathes at its own pace, times its flare-up.
	if (source_.zones.empty()) {
		std::fill(columnGain_.begin(), columnGain_.end(), 1.F);
	} else {
		std::vector<float> g(source_.zones.size());
		for (size_t z = 0; z < g.size(); z++) {
			const FireZone &zone = source_.zones[z];
			g[z] = static_cast<float>((1.0 + 0.35 * std::sin(6.283 * zone.beat * time_ / 4.0 + zone.phase)) * FlareGain(z, time_));
		}
		for (int i = 0; i < width_; i++) {
			const float x = static_cast<float>(i) + 0.5F;
			float best = 0.F;
			for (size_t z = 0; z < g.size(); z++) {
				const FireZone &zone = source_.zones[z];
				const float d = std::abs(x - zone.centre);
				if (d < zone.halfWidth)
					best = std::max(best, std::sqrt(1.F - d / zone.halfWidth) * g[z]);
			}
			columnGain_[i] = best;
		}
	}
	// The wind dies down.
	if (windAny_) {
		const float decay = std::pow(0.03F, dt);
		float most = 0.F;
		for (float &v : wind_) {
			v *= decay;
			most = std::max(most, std::abs(v));
		}
		if (most < 0.01F) {
			std::fill(wind_.begin(), wind_.end(), 0.F);
			windAny_ = false;
		}
	}
	BuildAir();
	Simulate(dt);
	// Sparks ride the air.
	if (!warming_ && !source_.sparkX.empty()) {
		const float want = recipe_.sparkRate * settings_.sparks * dt;
		int n = static_cast<int>(want) + (Random() < want - std::floor(want) ? 1 : 0);
		while (n-- > 0) {
			const size_t p = std::min(source_.sparkX.size() - 1, static_cast<size_t>(Random() * static_cast<float>(source_.sparkX.size())));
			AddSpark(source_.sparkX[p], source_.sparkY[p], 0.F, 1.F);
		}
	}
	UpdateSparks(dt);
	if (settings_.flares > 0 && !source_.zones.empty()) {
		nextFlare_ -= dt * settings_.flares;
		if (nextFlare_ <= 0) {
			Flare(std::min(source_.zones.size() - 1, static_cast<size_t>(Random() * static_cast<float>(source_.zones.size()))));
			nextFlare_ = 2.2 + Random() * 3.2;
		}
	}
}

void LiveFire::LimitWorkers(int count)
{
	workers_->Limit(count < 0 ? SIZE_MAX : static_cast<size_t>(count));
}

void LiveFire::Advance(float seconds)
{
	float dt = std::min(seconds, 0.05F);
	if (!(dt > 0))
		return;
	const int n = dt > 0.024F ? 2 : 1;
	for (int s = 0; s < n; s++)
		Step(dt / static_cast<float>(n));
}

void LiveFire::WarmUp(float seconds)
{
	warming_ = true;
	constexpr float Dt = 1.F / 22.5F;
	for (float t = 0; t < seconds; t += Dt)
		Step(Dt);
	warming_ = false;
	sparks_.clear();
}

void LiveFire::BuildMips()
{
	const float *src = heat_.data();
	int sw = width_;
	int sh = height_;
	for (size_t level = 0; level < mips_.size(); level++) {
		const int w = mipWidth_[level];
		const int h = mipHeight_[level];
		float *dst = mips_[level].data();
		const auto job = [&, src, sw, sh, w, dst](int task) {
			const int jEnd = std::min(h, (task + 1) * RowsPerTask);
			for (int j = task * RowsPerTask; j < jEnd; j++) {
				const float *r0 = src + static_cast<size_t>(std::min(j * 2, sh - 1)) * sw;
				const float *r1 = src + static_cast<size_t>(std::min(j * 2 + 1, sh - 1)) * sw;
				for (int i = 0; i < w; i++) {
					const int x0 = std::min(i * 2, sw - 1);
					const int x1 = std::min(i * 2 + 1, sw - 1);
					dst[static_cast<size_t>(j) * w + i] = (r0[x0] + r0[x1] + r1[x0] + r1[x1]) * 0.25F;
				}
			}
		};
		if (level == 0)
			workers_->Run(TasksFor(h), job);
		else
			for (int task = 0; task < TasksFor(h); task++)
				job(task);
		src = dst;
		sw = w;
		sh = h;
	}
}

float LiveFire::SampleMip(int level, float u, float v) const
{
	if (level <= 0)
		return Bilinear(heat_.data(), width_, height_, u * static_cast<float>(width_) - 0.5F, v * static_cast<float>(height_) - 0.5F);
	const size_t l = std::min(static_cast<size_t>(level - 1), mips_.size() - 1);
	const int w = mipWidth_[l];
	const int h = mipHeight_[l];
	return Bilinear(mips_[l].data(), w, h, u * static_cast<float>(w) - 0.5F, v * static_cast<float>(h) - 0.5F);
}

void LiveFire::SetPaintExtras(float trail, float back)
{
	trail_ = std::max(trail, 0.F);
	back_ = std::max(back, 0.F);
}

void LiveFire::Clear()
{
	std::fill(heat_.begin(), heat_.end(), 0.F);
	std::fill(next_.begin(), next_.end(), 0.F);
	std::fill(wind_.begin(), wind_.end(), 0.F);
	windAny_ = false;
	sparks_.clear();
	std::fill(flareAt_.begin(), flareAt_.end(), -10.0);
	for (std::vector<float> &mip : mips_)
		std::fill(mip.begin(), mip.end(), 0.F);
}

void LiveFire::FlareAgo(size_t zone, float ago, float sparkDelay)
{
	FlareWith(zone, time_ - static_cast<double>(ago), sparkDelay);
}

void LiveFire::Blow(float x, float y, float vx, float vy)
{
	Splat(x, y, vx, vy);
}

void LiveFire::BlowOutward(float x, float y, float strength, float dt)
{
	const int windWidth = std::max(1, static_cast<int>(std::lround(width_ / WindCell)));
	const int windHeight = static_cast<int>(wind_.size() / 2) / windWidth;
	const float cell = static_cast<float>(width_) / static_cast<float>(windWidth);
	const float k = 1.F - std::pow(0.02F, dt);
	for (int j = 0; j < windHeight; j++) {
		for (int i = 0; i < windWidth; i++) {
			const float dx = (static_cast<float>(i) + 0.5F) * cell - x;
			const float dy = (static_cast<float>(j) + 0.5F) * cell - y;
			float *q = &wind_[(static_cast<size_t>(j) * windWidth + i) * 2];
			q[0] += (dx * 1.2F * strength - q[0]) * k;
			q[1] += (dy * 1.3F * strength + std::abs(dx) * 0.25F * strength - q[1]) * k;
		}
	}
	windAny_ = true;
}

void LiveFire::BuildTrail()
{
	// The heat's first mip level drawn out toward the viewer from the logo's middle (FS_FIRE's trail): the most of 14
	// samples along the line to it, each less the further out.
	const int w = mipWidth_[0];
	const int h = mipHeight_[0];
	trailMap_.resize(static_cast<size_t>(w) * h);
	const float cx = (static_cast<float>(source_.width - 2 * pad_) * 0.5F + static_cast<float>(pad_)) / static_cast<float>(width_);
	const float cy = 120.F / 432.F;
	const float trail = trail_;
	const float *mip = mips_[0].data();
	workers_->Run(TasksFor(h), [&](int task) {
		const int jEnd = std::min(h, (task + 1) * RowsPerTask);
		for (int j = task * RowsPerTask; j < jEnd; j++) {
			const float v = (static_cast<float>(j) + 0.5F) / static_cast<float>(h);
			for (int i = 0; i < w; i++) {
				const float u = (static_cast<float>(i) + 0.5F) / static_cast<float>(w);
				float acc = 0.F;
				for (int k = 0; k < 14; k++) {
					const float f = (static_cast<float>(k) + 0.5F) / 14.F;
					const float m = 1.F - f * trail * 0.45F;
					const float qu = cx + (u - cx) * m;
					const float qv = cy + (v - cy) * m;
					acc = std::max(acc, Bilinear(mip, w, h, qu * static_cast<float>(w) - 0.5F, qv * static_cast<float>(h) - 0.5F) * (1.F - f) * 1.05F);
				}
				trailMap_[static_cast<size_t>(j) * w + i] = acc;
			}
		}
	});
}

void LiveFire::BuildBack()
{
	// The letters' feed (maps R) blurred as the graphics card's mip levels 4, 5.5 and 6.5 would, the softer ones
	// shifted up a little (FS_FIRE's burn-through): worked out once, at a quarter of the picture each way.
	const int pw = std::max(1, PictureWidth());
	const int ph = height_;
	std::vector<std::vector<float>> levels;
	std::vector<int> lw, lh;
	{
		std::vector<float> base(static_cast<size_t>(pw) * ph);
		for (int y = 0; y < ph; y++)
			for (int x = 0; x < pw; x++)
				base[static_cast<size_t>(y) * pw + x] = source_.feed[static_cast<size_t>(y) * width_ + x + pad_] / 1.6F;
		levels.push_back(std::move(base));
		lw.push_back(pw);
		lh.push_back(ph);
	}
	for (int level = 1; level <= 7; level++) {
		const int sw = lw.back();
		const int sh = lh.back();
		const int w = std::max(1, sw / 2);
		const int h = std::max(1, sh / 2);
		std::vector<float> next(static_cast<size_t>(w) * h);
		const std::vector<float> &src = levels.back();
		for (int j = 0; j < h; j++) {
			for (int i = 0; i < w; i++) {
				const int x0 = std::min(i * 2, sw - 1);
				const int x1 = std::min(i * 2 + 1, sw - 1);
				const int y0 = std::min(j * 2, sh - 1);
				const int y1 = std::min(j * 2 + 1, sh - 1);
				next[static_cast<size_t>(j) * w + i] = (src[static_cast<size_t>(y0) * sw + x0] + src[static_cast<size_t>(y0) * sw + x1] + src[static_cast<size_t>(y1) * sw + x0] + src[static_cast<size_t>(y1) * sw + x1]) * 0.25F;
			}
		}
		levels.push_back(std::move(next));
		lw.push_back(w);
		lh.push_back(h);
	}
	const auto sample = [&](float level, float u, float v) {
		const int l0 = std::clamp(static_cast<int>(level), 0, 7);
		const int l1 = std::min(l0 + 1, 7);
		const float f = std::clamp(level - static_cast<float>(l0), 0.F, 1.F);
		const auto at = [&](int l) {
			const float cu = std::clamp(u, 0.F, 1.F);
			const float cv = std::clamp(v, 0.F, 1.F);
			return Bilinear(levels[l].data(), lw[l], lh[l], cu * static_cast<float>(lw[l]) - 0.5F, cv * static_cast<float>(lh[l]) - 0.5F);
		};
		return at(l0) * (1.F - f) + at(l1) * f;
	};
	backWidth_ = std::max(1, pw / 4);
	backHeight_ = std::max(1, ph / 4);
	backMap_.resize(static_cast<size_t>(backWidth_) * backHeight_);
	for (int j = 0; j < backHeight_; j++) {
		const float v = (static_cast<float>(j) + 0.5F) / static_cast<float>(backHeight_);
		for (int i = 0; i < backWidth_; i++) {
			const float u = (static_cast<float>(i) + 0.5F) / static_cast<float>(backWidth_);
			backMap_[static_cast<size_t>(j) * backWidth_ + i] = sample(4.F, u, v) * 0.9F + sample(5.5F, u, v - 0.03F) * 0.8F + sample(6.5F, u, v - 0.07F) * 0.6F;
		}
	}
}

void LiveFire::Paint(uint8_t *rgba, int pitch, const uint8_t *cover)
{
	BuildMips();
	const int w = width_;
	const int h = height_;
	const float fw = static_cast<float>(w);
	const float fh = static_cast<float>(h);
	const double t = time_;
	const FireRecipe &recipe = recipe_;
	// The flicker on the colour: fine noise that races up.
	NoiseLayer detail[2];
	PrepareFbm(detail, 2, t, recipe.detailSize, recipe.detailSpeed, t * recipe.detailRate, 1.F, -1.F, fw + 1.F, -1.F, fh + 1.F);
	std::vector<float> xs(w), ys(h);
	for (int i = 0; i < w; i++)
		xs[i] = static_cast<float>(i) + 0.5F;
	for (int j = 0; j < h; j++)
		ys[j] = static_cast<float>(j) + 0.5F;
	Axis detailX[2], detailY[2];
	for (int l = 0; l < 2; l++) {
		detailX[l].Make(detail[l], true, xs.data(), xs.size());
		detailY[l].Make(detail[l], false, ys.data(), ys.size());
	}
	// The glow behind: the heat's mip level `glowLevel`, as the reference reads it (e.g. 4.6: 0.4 of level 4, 0.6 of 5).
	const int glowLow = std::clamp(static_cast<int>(recipe.glowLevel), 1, 4);
	const float glowHigh = std::clamp(recipe.glowLevel - static_cast<float>(glowLow), 0.F, 1.F);
	const std::vector<float> &mipA = mips_[glowLow - 1];
	const std::vector<float> &mipB = mips_[glowLow];
	const int w4 = mipWidth_[glowLow - 1];
	const int h4 = mipHeight_[glowLow - 1];
	const int w5 = mipWidth_[glowLow];
	const int h5 = mipHeight_[glowLow];
	std::vector<Tap> tap4(w), tap5(w);
	std::vector<float> edgeX(w);
	for (int i = 0; i < w; i++) {
		const float u = xs[i] / fw;
		tap4[i] = TapAt(u * static_cast<float>(w4) - 0.5F, w4);
		tap5[i] = TapAt(u * static_cast<float>(w5) - 0.5F, w5);
		edgeX[i] = std::clamp(xs[i] / recipe.edgeSide, 0.F, 1.F) * std::clamp((fw - xs[i]) / recipe.edgeSide, 0.F, 1.F);
	}
	// While the intro's logo flies: its heat trailing toward the viewer, and the light through the letters.
	const bool trailing = trail_ > 0.001F;
	if (trailing)
		BuildTrail();
	const bool backLit = back_ > 0.001F;
	std::vector<float> backColumn;
	std::vector<Tap> backTapX;
	float backRgb[3] = {};
	if (backLit) {
		if (backMap_.empty())
			BuildBack();
		const float hot = std::clamp(back_ - 0.9F, 0.F, 1.F);
		backRgb[0] = 255.F;
		backRgb[1] = (0.42F + (0.86F - 0.42F) * hot) * 255.F;
		backRgb[2] = (0.10F + (0.62F - 0.10F) * hot) * 255.F;
		backColumn.resize(w);
		backTapX.resize(w);
		// It flickers along the letters: noise across x that changes fast in time (vn(P.x / 90, 7t, 3)).
		NoiseLayer flicker;
		flicker.Prepare(1.0 / 90.0, 0.0, t * 7.0, 3.0, 1.F, -1.F, fw + 1.F, 0.F, 0.F);
		Axis flickerX;
		flickerX.Make(flicker, true, xs.data(), xs.size());
		const int y0 = static_cast<int>(std::floor(flicker.fracY)) - flicker.y0;
		const float fy = Smooth(std::clamp(flicker.fracY - std::floor(flicker.fracY), 0.F, 1.F));
		std::vector<float> f0(w, 0.F);
		AddRow(flicker, flickerX, std::clamp(y0, 0, flicker.ny - 2), fy, f0.data(), f0.size());
		const float pw = static_cast<float>(std::max(1, PictureWidth()));
		for (int i = 0; i < w; i++) {
			const float x = xs[i];
			const float fl = 0.85F + 0.15F * f0[i];
			const float sx = std::clamp(x / 90.F, 0.F, 1.F);
			const float ex = std::clamp((fw - x) / 90.F, 0.F, 1.F);
			backColumn[i] = fl * Smooth(sx) * Smooth(ex) * back_;
			const float mu = (x - static_cast<float>(pad_)) / pw;
			backTapX[i] = TapAt(std::clamp(mu, 0.F, 1.F) * static_cast<float>(backWidth_) - 0.5F, backWidth_);
		}
	}
	const float *heat = heat_.data();
	const float *ramp = ramp_.data();
	const float *m4 = mipA.data();
	const float *m5 = mipB.data();
	const float gr = glowR_;
	const float gg = glowG_;
	const float gb = glowB_;
	const float shown = recipe.shown;
	const float glowAlpha = recipe.glowAlpha;
	const float edgeTop = recipe.edgeTop;
	const int tw = mipWidth_[0];
	const int th = mipHeight_[0];
	constexpr float RampScale = static_cast<float>(RampSteps - 1) / MaxHeat;
	workers_->Run(TasksFor(h), [&](int task) {
		std::vector<float> noise(w), g4(w4), g5(w5), trailRow(trailing ? tw : 0), backRow(backLit ? backWidth_ : 0);
		std::vector<Tap> trailTapX;
		if (trailing) {
			trailTapX.resize(w);
			for (int i = 0; i < w; i++)
				trailTapX[i] = TapAt(xs[i] / fw * static_cast<float>(tw) - 0.5F, tw);
		}
		const int jEnd = std::min(h, (task + 1) * RowsPerTask);
		for (int j = task * RowsPerTask; j < jEnd; j++) {
			const float y = ys[j];
			const float v = y / fh;
			uint8_t *out = rgba + static_cast<ptrdiff_t>(h - 1 - j) * pitch;
			const uint8_t *coverRow = cover != nullptr ? cover + static_cast<size_t>(h - 1 - j) * w : nullptr;
			const size_t row = static_cast<size_t>(j) * w;
			// This row of both glow levels.
			const Tap t4 = TapAt(v * static_cast<float>(h4) - 0.5F, h4);
			const Tap t5 = TapAt(v * static_cast<float>(h5) - 0.5F, h5);
			for (int i = 0; i < w4; i++) {
				const float a = m4[static_cast<size_t>(t4.i0) * w4 + i];
				g4[i] = a + (m4[static_cast<size_t>(t4.i1) * w4 + i] - a) * t4.w;
			}
			for (int i = 0; i < w5; i++) {
				const float a = m5[static_cast<size_t>(t5.i0) * w5 + i];
				g5[i] = a + (m5[static_cast<size_t>(t5.i1) * w5 + i] - a) * t5.w;
			}
			if (trailing) {
				const Tap ty = TapAt(v * static_cast<float>(th) - 0.5F, th);
				for (int i = 0; i < tw; i++) {
					const float a = trailMap_[static_cast<size_t>(ty.i0) * tw + i];
					trailRow[i] = a + (trailMap_[static_cast<size_t>(ty.i1) * tw + i] - a) * ty.w;
				}
			}
			float backY = 0.F;
			if (backLit) {
				const Tap by = TapAt(v * static_cast<float>(backHeight_) - 0.5F, backHeight_);
				for (int i = 0; i < backWidth_; i++) {
					const float a = backMap_[static_cast<size_t>(by.i0) * backWidth_ + i];
					backRow[i] = a + (backMap_[static_cast<size_t>(by.i1) * backWidth_ + i] - a) * by.w;
				}
				backY = Smooth(std::clamp(y / 70.F, 0.F, 1.F));
			}
			const bool anyHeat = trailing || std::any_of(heat + row, heat + row + w, [](float x) { return x > 0.F; });
			if (anyHeat) {
				std::fill(noise.begin(), noise.end(), 0.F);
				for (int l = 0; l < 2; l++)
					AddRow(detail[l], detailX[l], detailY[l].cell[j], detailY[l].weight[j], noise.data(), noise.size());
			}
			const float edgeY = std::clamp((fh - y) / edgeTop, 0.F, 1.F);
			for (int i = 0; i < w; i++) {
				float hot = heat[row + i];
				if (trailing) {
					const Tap &tx = trailTapX[i];
					hot = std::max(hot, trailRow[tx.i0] + (trailRow[tx.i1] - trailRow[tx.i0]) * tx.w);
				}
				const Tap &a4 = tap4[i];
				const Tap &a5 = tap5[i];
				const float glow4 = g4[a4.i0] + (g4[a4.i1] - g4[a4.i0]) * a4.w;
				const float glow5 = g5[a5.i0] + (g5[a5.i1] - g5[a5.i0]) * a5.w;
				const float gw = std::clamp(glow4 * (1.F - glowHigh) + glow5 * glowHigh, 0.F, 1.2F);
				const float ga = std::clamp(gw * glowAlpha, 0.F, 0.5F);
				uint8_t *px = out + static_cast<ptrdiff_t>(i) * 4;
				px[3] = 255;
				float ed = edgeY * edgeX[i];
				ed = ed * ed * (3.F - 2.F * ed);
				if (coverRow != nullptr)
					ed *= static_cast<float>(coverRow[i]) * (1.F / 255.F);
				if ((hot <= 0.F && ga <= 0.F && !backLit) || ed <= 0.F) {
					px[0] = px[1] = px[2] = 0;
					continue;
				}
				float r = 0.F;
				float g = 0.F;
				float b = 0.F;
				float alpha = 0.F;
				if (hot > 0.F) {
					const float d = noise[i];
					const float heatShown = std::clamp(hot * (1.F + 0.22F * d * std::min(hot * 2.F, 1.F)) * shown, 0.F, MaxHeat);
					const float *c = ramp + static_cast<size_t>(heatShown * RampScale + 0.5F) * 4;
					r = c[0];
					g = c[1];
					b = c[2];
					alpha = c[3];
				}
				const float under = ga * (1.F - alpha);
				r += gr * under;
				g += gg * under;
				b += gb * under;
				if (backLit) {
					const Tap &bx = backTapX[i];
					const float bl = (backRow[bx.i0] + (backRow[bx.i1] - backRow[bx.i0]) * bx.w) * backColumn[i] * backY * (1.F - alpha * 0.7F);
					r += backRgb[0] * bl;
					g += backRgb[1] * bl;
					b += backRgb[2] * bl;
				}
				px[0] = ToByte(r * ed);
				px[1] = ToByte(g * ed);
				px[2] = ToByte(b * ed);
			}
		}
	});
	DrawSparks(rgba, pitch, cover);
}

void LiveFire::DrawSparks(uint8_t *rgba, int pitch, const uint8_t *cover) const
{
	// Short bright streaks along their motion, added over the fire.
	int drawn = 0;
	for (const Spark &s : sparks_) {
		if (s.age < 0 || !s.moved || drawn >= 800)
			continue;
		drawn++;
		const float u = s.age / s.life;
		const float br = std::min(1.F, u / 0.08F) * std::pow(std::max(0.F, 1.F - u), 1.2F) * (0.75F + 0.25F * std::sin(s.age * 31.F + s.p));
		if (br <= 0)
			continue;
		float dx = s.x - s.ox;
		float dy = s.y - s.oy;
		const float len = std::hypot(dx, dy);
		const float norm = len > 0 ? len : 1.F;
		dx /= norm;
		dy /= norm;
		if (len <= 0) {
			dx = 0;
			dy = 1;
		}
		const float length = std::max(3.F, len * 2.2F);
		const float half = s.s * 0.5F;
		const float tailX = s.x - dx * length;
		const float tailY = s.y - dy * length;
		const float cr = 255.F * br;
		const float cg = (0.78F - 0.45F * u) * 255.F * br;
		const float cb = (0.30F - 0.26F * u) * 255.F * br;
		const float reach = half + 1.F;
		const int x0 = std::max(0, static_cast<int>(std::floor(std::min(tailX, s.x) - reach)));
		const int x1 = std::min(width_ - 1, static_cast<int>(std::ceil(std::max(tailX, s.x) + reach)));
		const int y0 = std::max(0, static_cast<int>(std::floor(std::min(tailY, s.y) - reach)));
		const int y1 = std::min(height_ - 1, static_cast<int>(std::ceil(std::max(tailY, s.y) + reach)));
		for (int y = y0; y <= y1; y++) {
			uint8_t *line = rgba + static_cast<ptrdiff_t>(height_ - 1 - y) * pitch;
			const uint8_t *coverRow = cover != nullptr ? cover + static_cast<size_t>(height_ - 1 - y) * width_ : nullptr;
			for (int x = x0; x <= x1; x++) {
				const float px = static_cast<float>(x) + 0.5F - tailX;
				const float py = static_cast<float>(y) + 0.5F - tailY;
				const float along = (px * dx + py * dy) / length;
				if (along < 0.F || along > 1.F)
					continue;
				const float across = std::abs(px * dy - py * dx);
				float k = std::clamp(half + 0.5F - across, 0.F, 1.F) * along;
				if (coverRow != nullptr)
					k *= static_cast<float>(coverRow[x]) * (1.F / 255.F);
				if (k <= 0.F)
					continue;
				uint8_t *p = line + static_cast<ptrdiff_t>(x) * 4;
				p[0] = ToByte(p[0] + cr * k);
				p[1] = ToByte(p[1] + cg * k);
				p[2] = ToByte(p[2] + cb * k);
			}
		}
	}
}

float LiveFire::LightAt(float x, float y) const
{
	const float u = x / static_cast<float>(width_);
	const float v = y / static_cast<float>(height_);
	// Mip level 3.2, as the reference reads it.
	const float blurred = SampleMip(3, u, v) * 0.8F + SampleMip(4, u, v) * 0.2F;
	return std::clamp(blurred * 1.6F, 0.F, 1.F);
}

void LiveFire::PaintLight(uint8_t *multiply, int multiplyPitch, uint8_t *add, int addPitch, bool doubled)
{
	// The mips are those of the last Paint (the same heat).
	const float red = settings_.red;
	const float tint[3] = { 1.F, 0.62F + (0.36F - 0.62F) * red, 0.25F + (0.16F - 0.25F) * red };
	constexpr float Dark[3] = { 0.10F, 0.028F, 0.006F };
	const float scale = doubled ? 0.5F : 1.F;
	for (int j = 0; j < lightHeight_; j++) {
		const float y = (static_cast<float>(j) + 0.5F) / static_cast<float>(lightHeight_) * static_cast<float>(height_);
		uint8_t *m = multiply + static_cast<ptrdiff_t>(lightHeight_ - 1 - j) * multiplyPitch;
		uint8_t *a = add + static_cast<ptrdiff_t>(lightHeight_ - 1 - j) * addPitch;
		for (int i = 0; i < lightWidth_; i++) {
			// Over the maps' picture only: what the fire lights (the letters) is there, not in the padding.
			const float x = static_cast<float>(pad_) + (static_cast<float>(i) + 0.5F) / static_cast<float>(lightWidth_) * static_cast<float>(PictureWidth());
			const float li = LightAt(x, y);
			for (int c = 0; c < 3; c++) {
				// rgb = L * (1 - 0.75 li) + L * tint * 1.12 * 0.75 li + (1 - lum) * dark * li
				const float times = 1.F - 0.75F * li + tint[c] * 1.12F * 0.75F * li;
				m[i * 4 + c] = ToByte(std::min(times * scale, 1.F) * 255.F);
				a[i * 4 + c] = ToByte(Dark[c] * li * 255.F);
			}
			m[i * 4 + 3] = 255;
			a[i * 4 + 3] = 255;
		}
	}
}

} // namespace fire

} // namespace devilution

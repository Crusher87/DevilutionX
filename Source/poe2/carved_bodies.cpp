#include "poe2/carved_bodies.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace devilution {

namespace carved {

namespace {

uint32_t U32(std::span<const uint8_t> data, size_t at)
{
	return static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) | (static_cast<uint32_t>(data[at + 2]) << 16)
	    | (static_cast<uint32_t>(data[at + 3]) << 24);
}

float F32(std::span<const uint8_t> data, size_t at)
{
	const uint32_t bits = U32(data, at);
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

int16_t I16(std::span<const uint8_t> data, size_t at)
{
	return static_cast<int16_t>(static_cast<uint16_t>(data[at]) | (static_cast<uint16_t>(data[at + 1]) << 8));
}

constexpr size_t HeadSize = 4 + 4 + 4 * 4 + 16 * 4;
constexpr size_t NameSize = 12;
constexpr size_t PoseSize = NameSize + 8;
constexpr size_t BallSize = 8;
constexpr float Pi = 3.14159265F;

} // namespace

const Pose *Sprite::Find(std::string_view name) const
{
	for (const Pose &pose : poses) {
		if (pose.name == name)
			return &pose;
	}
	return nullptr;
}

std::span<const Ball> Sprite::BallsOf(const Pose &pose) const
{
	if (pose.first > balls.size() || pose.count > balls.size() - pose.first)
		return {};
	return std::span<const Ball>(balls).subspan(pose.first, pose.count);
}

void Sprite::FeetAt(int facing, float &x, float &y) const
{
	const size_t d = static_cast<size_t>(std::clamp(facing, 0, 7));
	x = ax + shifts[d * 2];
	y = ay + shifts[d * 2 + 1] - zmin * std::cos(el * Pi / 180);
}

bool Parse(std::span<const uint8_t> data, Sprite &out)
{
	if (data.size() < HeadSize || std::memcmp(data.data(), "CRV1", 4) != 0)
		return false;
	const uint32_t count = U32(data, 4);
	if (count > 100000 || data.size() < HeadSize + static_cast<size_t>(count) * PoseSize)
		return false;
	Sprite sprite;
	sprite.el = F32(data, 8);
	sprite.ax = F32(data, 12);
	sprite.ay = F32(data, 16);
	sprite.zmin = F32(data, 20);
	for (size_t i = 0; i < 16; i++)
		sprite.shifts[i] = F32(data, 24 + i * 4);
	if (!std::isfinite(sprite.el) || !std::isfinite(sprite.ax) || !std::isfinite(sprite.ay) || !std::isfinite(sprite.zmin))
		return false;
	uint64_t total = 0;
	sprite.poses.resize(count);
	for (size_t i = 0; i < count; i++) {
		const size_t at = HeadSize + i * PoseSize;
		const char *name = reinterpret_cast<const char *>(data.data() + at);
		sprite.poses[i].name.assign(name, strnlen(name, NameSize));
		sprite.poses[i].first = U32(data, at + NameSize);
		sprite.poses[i].count = U32(data, at + NameSize + 4);
		total = std::max<uint64_t>(total, static_cast<uint64_t>(sprite.poses[i].first) + sprite.poses[i].count);
	}
	const size_t ballsAt = HeadSize + static_cast<size_t>(count) * PoseSize;
	if (data.size() < ballsAt + total * BallSize)
		return false;
	sprite.balls.resize(static_cast<size_t>(total));
	for (size_t i = 0; i < sprite.balls.size(); i++) {
		const size_t at = ballsAt + i * BallSize;
		sprite.balls[i] = Ball { I16(data, at) / 8.F, I16(data, at + 2) / 8.F, I16(data, at + 4) / 8.F, I16(data, at + 6) / 64.F };
	}
	out = std::move(sprite);
	return true;
}

void MakeCard(std::span<const Ball> balls, int facing, float lean, float dx, float dy, float tileAcross, int below, Card &out)
{
	out = Card {};
	if (balls.empty())
		return;
	// The body's x and y as the viewer sees them for this facing (u right, v away), then on the floor in tiles
	// (u = 32 (wx - wy), v = -32 (wx + wy) at 1x), then across the light.
	const float turn = -45.F * static_cast<float>(facing) * Pi / 180;
	const float c = std::cos(turn);
	const float s = std::sin(turn);
	std::vector<float> across(balls.size());
	float reach = 0;
	float top = 0;
	for (size_t i = 0; i < balls.size(); i++) {
		const Ball &b = balls[i];
		const float u = b.x * c - b.y * s;
		const float v = b.x * s + b.y * c;
		const float wx = (u - v) / 64;
		const float wy = (-u - v) / 64;
		across[i] = (-dy * wx + dx * wy) * tileAcross;
		reach = std::max(reach, std::abs(across[i]) + b.r);
		top = std::max(top, (b.z + b.r) * lean);
	}
	const int half = static_cast<int>(std::ceil(reach)) + 2;
	out.width = half * 2;
	out.floorRow = static_cast<int>(std::ceil(top)) + 2;
	out.height = out.floorRow + 1 + std::max(below, 0);
	out.cover.assign(static_cast<size_t>(out.width) * out.height, 0.F);
	int highest = out.height;
	for (size_t i = 0; i < balls.size(); i++) {
		const Ball &b = balls[i];
		const float cx = static_cast<float>(half) + across[i];
		const float cy = static_cast<float>(out.floorRow) + 0.5F - b.z * lean;
		const float rx = std::max(b.r, 0.3F);
		const float ry = std::max(b.r * lean, 0.3F);
		const int x0 = std::max(static_cast<int>(std::floor(cx - rx - 1)), 0);
		const int x1 = std::min(static_cast<int>(std::ceil(cx + rx + 1)), out.width - 1);
		const int y0 = std::max(static_cast<int>(std::floor(cy - ry - 1)), 0);
		const int y1 = std::min(static_cast<int>(std::ceil(cy + ry + 1)), out.height - 1);
		const float edge = std::min(rx, ry);
		for (int y = y0; y <= y1; y++) {
			const float ny = (static_cast<float>(y) + 0.5F - cy) / ry;
			for (int x = x0; x <= x1; x++) {
				const float nx = (static_cast<float>(x) + 0.5F - cx) / rx;
				// Smooth edge: a pixel half inside the ellipse is half covered.
				const float inside = std::clamp((1 - std::sqrt(nx * nx + ny * ny)) * edge + 0.5F, 0.F, 1.F);
				if (inside <= 0)
					continue;
				float &cell = out.cover[static_cast<size_t>(y) * out.width + x];
				cell = std::max(cell, inside);
				highest = std::min(highest, y);
			}
		}
	}
	out.top = highest;
}

void ViewFacings(const Sprite &sprite, std::span<const Ball> balls, int facing, float k, int width, int height, float smooth, std::vector<float> &out)
{
	const size_t count = static_cast<size_t>(std::max(width, 0)) * static_cast<size_t>(std::max(height, 0));
	out.assign(count * 4, 0.F);
	if (count == 0)
		return;
	const float turn = -45.F * static_cast<float>(std::clamp(facing, 0, 7)) * Pi / 180;
	const float c = std::cos(turn);
	const float s = std::sin(turn);
	const float e = sprite.el * Pi / 180;
	const float ce = std::cos(e);
	const float se = std::sin(e);
	const size_t d = static_cast<size_t>(std::clamp(facing, 0, 7));
	const float ox = sprite.ax + sprite.shifts[d * 2];
	const float oy = sprite.ay + sprite.shifts[d * 2 + 1];
	std::vector<float> depth(count, -1e9F);
	for (const Ball &b : balls) {
		const float u = b.x * c - b.y * s;
		const float v = b.x * s + b.y * c;
		const float z = b.z + sprite.zmin;
		const float cx = k * (ox + u);
		const float cy = k * (oy - (z * ce + v * se));
		const float radius = k * b.r;
		if (radius <= 0.05F)
			continue;
		// How near the eye the ball's middle is: the eye looks away (+v) and down.
		const float near = -v * ce + z * se;
		const int x0 = std::max(static_cast<int>(std::floor(cx - radius)), 0);
		const int x1 = std::min(static_cast<int>(std::ceil(cx + radius)), width - 1);
		const int y0 = std::max(static_cast<int>(std::floor(cy - radius)), 0);
		const int y1 = std::min(static_cast<int>(std::ceil(cy + radius)), height - 1);
		for (int y = y0; y <= y1; y++) {
			const float ny = (static_cast<float>(y) + 0.5F - cy) / radius;
			for (int x = x0; x <= x1; x++) {
				const float nx = (static_cast<float>(x) + 0.5F - cx) / radius;
				const float r2 = nx * nx + ny * ny;
				if (r2 > 1)
					continue;
				const float toEye = std::sqrt(1 - r2);
				const float at = near + b.r * toEye;
				const size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
				if (at <= depth[i])
					continue;
				depth[i] = at;
				out[i * 4] = nx;
				out[i * 4 + 1] = -ny;
				out[i * 4 + 2] = toEye;
				out[i * 4 + 3] = 1;
			}
		}
	}
	if (smooth <= 0)
		return;
	// Smoothed: each facing weighted by how sure it is, along the rows and then down the columns.
	const int radius = std::max(static_cast<int>(smooth * 2.5F + 0.5F), 1);
	std::vector<float> kernel(static_cast<size_t>(radius) * 2 + 1);
	for (int i = -radius; i <= radius; i++)
		kernel[static_cast<size_t>(i + radius)] = std::exp(-static_cast<float>(i * i) / (2 * smooth * smooth));
	std::vector<float> pass(out.size(), 0.F);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			std::array<float, 4> sum {};
			float weights = 0;
			for (int i = std::max(-radius, -x); i <= std::min(radius, width - 1 - x); i++) {
				const size_t from = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x + i)) * 4;
				const float w = kernel[static_cast<size_t>(i + radius)];
				for (size_t q = 0; q < 3; q++)
					sum[q] += out[from + q] * out[from + 3] * w;
				sum[3] += out[from + 3] * w;
				weights += w;
			}
			const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
			for (size_t q = 0; q < 3; q++)
				pass[to + q] = sum[q];
			pass[to + 3] = sum[3] / weights;
		}
	}
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			std::array<float, 4> sum {};
			float weights = 0;
			for (int i = std::max(-radius, -y); i <= std::min(radius, height - 1 - y); i++) {
				const size_t from = (static_cast<size_t>(y + i) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
				const float w = kernel[static_cast<size_t>(i + radius)];
				for (size_t q = 0; q < 4; q++)
					sum[q] += pass[from + q] * w;
				weights += w;
			}
			const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
			const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
			if (length < 1e-6F) {
				out[to] = 0;
				out[to + 1] = 0;
				out[to + 2] = 1;
				out[to + 3] = 0;
				continue;
			}
			out[to] = sum[0] / length;
			out[to + 1] = sum[1] / length;
			out[to + 2] = std::max(sum[2] / length, 0.F);
			out[to + 3] = std::min(sum[3] / weights, 1.F);
		}
	}
}

int FacingOf(std::string_view name)
{
	const size_t slash = name.rfind('/');
	if (slash == std::string_view::npos || slash + 2 >= name.size() || name[slash + 1] != 'd')
		return -1;
	const char digit = name[slash + 2];
	return digit >= '0' && digit <= '7' ? digit - '0' : -1;
}

std::string PoseOf(std::string_view name, int inBetween)
{
	const size_t slash = name.rfind('/');
	const std::string_view leaf = slash == std::string_view::npos ? name : name.substr(slash + 1);
	const size_t under = leaf.find('_');
	if (under == std::string_view::npos)
		return {};
	std::string pose(leaf.substr(under + 1));
	if (inBetween >= 1 && inBetween <= 2)
		pose += "_m" + std::to_string(inBetween);
	else if (inBetween >= 3 && inBetween <= 4)
		pose += "_b" + std::to_string(inBetween - 2);
	return pose;
}

} // namespace carved

} // namespace devilution

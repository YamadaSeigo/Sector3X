#pragma once
#include <vector>
#include <cstdint>
#include <random>

namespace SFW::Math {
	//-------------------------------
	// 乱数テーブル付き Perlin 2D

	//-------------------------------
	class Perlin2D {
	public:
		explicit Perlin2D(uint32_t seed) {
			// パーマネーションテーブル 256*2
			std::vector<int> base(256);
			for (int i = 0; i < 256; i++) base[i] = i;
			std::mt19937 rng(seed);
			std::shuffle(base.begin(), base.end(), rng);
			for (int i = 0; i < 256; i++) { perm[i] = base[i]; perm[256 + i] = base[i]; }
		}

		// 単オクターブの Perlin ノイズ（-1..1）
		float noise(float x, float y) const {
			int xi = fastfloor(x) & 255;
			int yi = fastfloor(y) & 255;

			float xf = x - std::floor(x);
			float yf = y - std::floor(y);

			float u = fade(xf);
			float v = fade(yf);

			int aa = perm[perm[xi] + yi];
			int ab = perm[perm[xi] + yi + 1];
			int ba = perm[perm[xi + 1] + yi];
			int bb = perm[perm[xi + 1] + yi + 1];

			float x1 = lerp(grad(aa, xf, yf),
				grad(ba, xf - 1.0f, yf), u);
			float x2 = lerp(grad(ab, xf, yf - 1.0f),
				grad(bb, xf - 1.0f, yf - 1.0f), u);
			return lerp(x1, x2, v); // -1..1
		}

		// fBm（オクターブ合成）
		float fbm(float x, float y, int oct, float lac, float gain) const {
			float amp = 1.0f;
			float freq = 1.0f;
			float sum = 0.0f, norm = 0.0f;
			for (int i = 0; i < oct; i++) {
				sum += amp * noise(x * freq, y * freq);
				norm += amp;
				freq *= lac;
				amp *= gain;
			}
			return (norm > 0.0f) ? (sum / norm) : 0.0f; // -1..1 を平均化
		}

	private:
		int perm[512];

		static inline int   fastfloor(float x) { return (x >= 0) ? int(x) : int(x) - 1; }
		static inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
		static inline float lerp(float a, float b, float t) { return a + t * (b - a); }
		static inline float grad(int h, float x, float y) {
			// 8 方向グラディエント
			int g = h & 7; // 0..7
			float u = (g < 4) ? x : y;
			float v = (g < 4) ? y : x;
			return ((g & 1) ? -u : u) + ((g & 2) ? -2.0f * v : 2.0f * v);
		}
	};
}

// HeightmapBake.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

namespace SFW::Graphics::TerrainBake {
	using Vec3 = Math::Vec3f;

	struct Tri {
		Vec3 a, b, c;
	};

	// グリッド（頂点格子）
	struct Grid {
		Vec3   origin;   // XZの基準位置（yは無視）
		float  cellSize; // セル一辺の実寸（X/Z共通）
		uint32_t vertsX; // X方向 頂点数（= セル数+1）
		uint32_t vertsZ; // Z方向 頂点数（= セル数+1）
	};

	// 出力/動作オプション
	enum class BakeMode : uint32_t {
		MaxHeight,   // 上面優先: 最大Y
		Average,     // 平均Y
	};

	struct BakeOptions {
		BakeMode mode = BakeMode::MaxHeight;

		// abs(n.y) < slopeNyEps の三角形（ほぼ垂直）は高さ決定に使わない
		float slopeNyEps = 1e-4f;

		// 焼いた後の穴（サンプルなし）を近傍補間で埋めるか
		bool  fillHoles = true;
		int   fillIterations = 2; // 近傍補間を何回繰り返すか

		// 正規化（H01化）の範囲。NaNの場合は自動（min/max をデータから推定）
		float yMin = std::numeric_limits<float>::quiet_NaN();
		float yMax = std::numeric_limits<float>::quiet_NaN();
	};

	// デバッグ統計
	struct BakeStats {
		uint64_t testedVertices = 0;     // 三角形内判定した頂点数
		uint64_t writtenVertices = 0;    // 実際に値を書いた頂点数
		uint32_t uncoveredVertices = 0;  // 最終的に穴だった頂点数（補間前）
		float    minY = +std::numeric_limits<float>::infinity();
		float    maxY = -std::numeric_limits<float>::infinity();
	};

	// ----------------------------- 内部ユーティリティ -----------------------------
	inline float clamp01(float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); }

	// 2D（三角形の XZ 投影）ポイントインテスト：バリセンtric（符号付き面積）
	static inline bool pointInTriXZ(float px, float pz, const Vec3& a, const Vec3& b, const Vec3& c) {
		// 2Dベクトル
		auto cross = [](float ax, float az, float bx, float bz) {
			return ax * bz - az * bx;
			};
		const float v0x = b.x - a.x, v0z = b.z - a.z;
		const float v1x = c.x - a.x, v1z = c.z - a.z;
		const float v2x = px - a.x, v2z = pz - a.z;

		const float denom = cross(v0x, v0z, v1x, v1z);
		if (std::fabs(denom) < 1e-20f) return false; // 退化

		const float u = cross(v2x, v2z, v1x, v1z) / denom;
		const float v = cross(v0x, v0z, v2x, v2z) / denom;
		// u>=0, v>=0, u+v<=1
		return (u >= -1e-6f) && (v >= -1e-6f) && (u + v <= 1.0f + 1e-6f);
	}

	// 三角形平面から y を求める（n.y が小さすぎる＝ほぼ垂直→false）
	static inline bool solveYOnTrianglePlane(const Tri& t, float x, float z, float slopeNyEps, float& outY) {
		const float ux = t.b.x - t.a.x;
		const float uy = t.b.y - t.a.y;
		const float uz = t.b.z - t.a.z;
		const float vx = t.c.x - t.a.x;
		const float vy = t.c.y - t.a.y;
		const float vz = t.c.z - t.a.z;

		// n = u x v
		const float nx = uy * vz - uz * vy;
		const float ny = uz * vx - ux * vz;
		const float nz = ux * vy - uy * vx;

		if (std::fabs(ny) < slopeNyEps) return false; // ほぼ垂直 → y が安定しない

		// 平面方程式: n·(P - a) = 0 → y = a.y - (n.x*(x-a.x) + n.z*(z-a.z)) / n.y
		const float dx = x - t.a.x;
		const float dz = z - t.a.z;
		outY = t.a.y - (nx * dx + nz * dz) / ny;
		return true;
	}

	// AABB vs グリッド範囲（XZ）→ グリッドのインデックス範囲に丸め込む
	static inline void triBBoxToGridRangeXZ(const Tri& tri, const Grid& g,
		uint32_t& ix0, uint32_t& ix1,
		uint32_t& iz0, uint32_t& iz1)
	{
		const float minX = (std::min)({ tri.a.x, tri.b.x, tri.c.x });
		const float maxX = (std::max)({ tri.a.x, tri.b.x, tri.c.x });
		const float minZ = (std::min)({ tri.a.z, tri.b.z, tri.c.z });
		const float maxZ = (std::max)({ tri.a.z, tri.b.z, tri.c.z });

		const float fx0 = (minX - g.origin.x) / g.cellSize;
		const float fx1 = (maxX - g.origin.x) / g.cellSize;
		const float fz0 = (minZ - g.origin.z) / g.cellSize;
		const float fz1 = (maxZ - g.origin.z) / g.cellSize;

		// 頂点格子の添字に丸める（頂点格子なので [0, verts-1] の範囲でクリップ）
		// 端に触れる三角形を拾えるように1セル程度の安全マージンを付けてもOK
		auto clampIndex = [](int v, int lo, int hi) {
			return (v < lo) ? lo : (v > hi ? hi : v);
			};

		int i0 = (int)std::floor(fx0) - 1;
		int i1 = (int)std::ceil(fx1) + 1;
		int k0 = (int)std::floor(fz0) - 1;
		int k1 = (int)std::ceil(fz1) + 1;

		i0 = clampIndex(i0, 0, (int)g.vertsX - 1);
		i1 = clampIndex(i1, 0, (int)g.vertsX - 1);
		k0 = clampIndex(k0, 0, (int)g.vertsZ - 1);
		k1 = clampIndex(k1, 0, (int)g.vertsZ - 1);

		ix0 = (uint32_t)(std::min)(i0, i1);
		ix1 = (uint32_t)(std::max)(i0, i1);
		iz0 = (uint32_t)(std::min)(k0, k1);
		iz1 = (uint32_t)(std::max)(k0, k1);
	}

	// 近傍補間（穴埋め）：未サンプル頂点を 4/8 近傍の平均で埋める（複数回）
	static inline void fillHolesAverage(std::vector<float>& Y,
		const std::vector<uint32_t>& C,
		const Grid& g, int iterations)
	{
		const uint32_t W = g.vertsX, H = g.vertsZ;
		std::vector<float> tmp(Y.size());

		auto at = [&](uint32_t x, uint32_t z) -> size_t { return (size_t)z * W + x; };

		for (int it = 0; it < iterations; ++it) {
			tmp = Y;
			for (uint32_t z = 0; z < H; ++z) {
				for (uint32_t x = 0; x < W; ++x) {
					const size_t idx = at(x, z);
					if (C[idx] > 0) continue; // 既に値あり
					// 8近傍平均
					float acc = 0.0f; int cnt = 0;
					for (int dz = -1; dz <= 1; ++dz) {
						for (int dx = -1; dx <= 1; ++dx) {
							if (dx == 0 && dz == 0) continue;
							const int nx = (int)x + dx;
							const int nz = (int)z + dz;
							if (nx < 0 || nz < 0 || nx >= (int)W || nz >= (int)H) continue;
							const size_t nIdx = at((uint32_t)nx, (uint32_t)nz);
							if (C[nIdx] > 0 || !std::isnan(Y[nIdx])) {
								// C>0 が本来のサンプル。NaN でない値も使う（前回補間を許容）
								acc += Y[nIdx];
								cnt += 1;
							}
						}
					}
					if (cnt > 0) tmp[idx] = acc / (float)cnt;
				}
			}
			Y.swap(tmp);
		}
	}

	// ----------------------------- メインAPI -----------------------------
	inline void BakeHeightFieldFromMesh(
		const std::vector<Tri>& tris,
		const Grid& grid,
		const BakeOptions& opt,
		std::vector<float>& outH01,    // size = vertsX * vertsZ
		BakeStats* outStats = nullptr  // 任意
	) {
		const uint32_t W = grid.vertsX;
		const uint32_t H = grid.vertsZ;
		const size_t   N = (size_t)W * H;

		outH01.assign(N, std::numeric_limits<float>::quiet_NaN()); // 後で正規化
		std::vector<float> accY;     // Average用の合計
		std::vector<uint32_t> cntY;  // Average用のカウント or 値ありフラグ

		if (opt.mode == BakeMode::Average) {
			accY.assign(N, 0.0f);
			cntY.assign(N, 0);
		}
		else {
			// MaxHeight の場合、NaN 初期化の outH01 に対して max を取っていく
			cntY.assign(N, 0);
		}

		auto at = [&](uint32_t x, uint32_t z) -> size_t { return (size_t)z * W + x; };

		BakeStats stats{};
		// 各三角形ごとに、そのXZ包囲の頂点格子だけを見る
		for (const Tri& t : tris) {
			// ほぼ垂直面（n.y ~ 0）はスキップ（高さ場には向かない）
			float dummyY;
			// 代表点チェック用（重心）
			const Vec3 cen{ (t.a.x + t.b.x + t.c.x) / 3.0f,
							(t.a.y + t.b.y + t.c.y) / 3.0f,
							(t.a.z + t.b.z + t.c.z) / 3.0f };
			// 平面の ny チェック（厳密には solveY 内でやるが、ここで早期判定）
			if (!solveYOnTrianglePlane(t, cen.x, cen.z, opt.slopeNyEps, dummyY)) {
				continue;
			}

			uint32_t ix0, ix1, iz0, iz1;
			triBBoxToGridRangeXZ(t, grid, ix0, ix1, iz0, iz1);

			for (uint32_t iz = iz0; iz <= iz1; ++iz) {
				const float pz = grid.origin.z + (float)iz * grid.cellSize;
				for (uint32_t ix = ix0; ix <= ix1; ++ix) {
					const float px = grid.origin.x + (float)ix * grid.cellSize;
					stats.testedVertices++;

					if (!pointInTriXZ(px, pz, t.a, t.b, t.c)) continue;

					float y;
					if (!solveYOnTrianglePlane(t, px, pz, opt.slopeNyEps, y)) continue;

					const size_t idx = at(ix, iz);
					if (opt.mode == BakeMode::Average) {
						accY[idx] += y;
						cntY[idx] += 1;
					}
					else { // MaxHeight
						if (cntY[idx] == 0 || y > outH01[idx]) {
							outH01[idx] = y;
						}
						cntY[idx] = 1; // 値あり
					}

					stats.minY = (std::min)(stats.minY, y);
					stats.maxY = (std::max)(stats.maxY, y);
					stats.writtenVertices++;
				}
			}
		}

		// Average の場合、平均を outH01 へ
		if (opt.mode == BakeMode::Average) {
			for (size_t i = 0; i < N; ++i) {
				if (cntY[i] > 0) outH01[i] = accY[i] / (float)cntY[i];
			}
		}

		// 未サンプルの頂点数（補間前）
		for (size_t i = 0; i < N; ++i) {
			if (cntY[i] == 0) stats.uncoveredVertices++;
		}

		// 必要なら穴埋め
		if (opt.fillHoles && stats.uncoveredVertices > 0) {
			// NaN で残っている所を近傍平均で埋める
			// cntY は「元から値があるか」を示すフラグとして流用
			fillHolesAverage(outH01, cntY, grid, (std::max)(1, opt.fillIterations));

			// 補間で min/max が広がる可能性は低いが、一応再スキャン
			stats.minY = +std::numeric_limits<float>::infinity();
			stats.maxY = -std::numeric_limits<float>::infinity();
			for (float y : outH01) {
				if (!std::isnan(y)) {
					stats.minY = (std::min)(stats.minY, y);
					stats.maxY = (std::max)(stats.maxY, y);
				}
			}
		}

		// 正規化レンジの決定
		float yMin = opt.yMin, yMax = opt.yMax;
		const bool autoMin = std::isnan(yMin);
		const bool autoMax = std::isnan(yMax);
		if (autoMin || autoMax) {
			if (stats.minY == +std::numeric_limits<float>::infinity() ||
				stats.maxY == -std::numeric_limits<float>::infinity()) {
				// 何も焼けていない → 0で埋めて終了
				std::fill(outH01.begin(), outH01.end(), 0.0f);
				if (outStats) *outStats = stats;
				return;
			}
			if (autoMin) yMin = stats.minY;
			if (autoMax) yMax = stats.maxY;
		}

		// y → H01 へ（クランプあり）
		const float denom = (yMax - yMin);
		const float inv = (std::fabs(denom) < 1e-20f) ? 0.0f : (1.0f / denom);
		for (float& y : outH01) {
			if (std::isnan(y)) { y = 0.0f; continue; } // 念のため残った穴を0に
			y = clamp01((y - yMin) * inv);
		}

		if (outStats) *outStats = stats;
	}
} // namespace TerrainBake

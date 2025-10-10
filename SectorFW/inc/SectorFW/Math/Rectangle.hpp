// Rectangle.hpp (例)
// 依存: Vector.hpp (Vec3f/Vec4f), Matrix.hpp (Matrix<4,4,float>), AABB.hpp, Frustum.hpp(任意)

#pragma once
#include <algorithm>
#include <array>
#include <limits>
#include "Vector.hpp"
#include "Matrix.hpp"
#include "AABB.hpp"
#include "Frustum.hpp" // 任意: 早期リジェクトに使える

#ifndef SFW_MATRIX_ROWMAJOR_RMUL
#define SFW_MATRIX_ROWMAJOR_RMUL 1
#endif

namespace SectorFW::Math {

    struct Rectangle {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool  visible = false;
        float width()  const { return (std::max)(0.f, x1 - x0); }
        float height() const { return (std::max)(0.f, y1 - y0); }

        std::array<Vec2f, 8> MakeLineVertex() const noexcept {
			return {
				Vec2f{x0,y0}, Vec2f{x1,y0},
				Vec2f{x1,y0}, Vec2f{x1,y1},
				Vec2f{x1,y1}, Vec2f{x0,y1},
				Vec2f{x0,y1}, Vec2f{x0,y0}
			};
        }
    };

    // ------------- 内部ユーティリティ -------------
    inline Vec4f ToClip(const Matrix<4, 4, float>& m, const Vec3f& p) {
        // 同次座標 (x,y,z,1) として変換
        const Vec4f v = { p.x, p.y, p.z, 1.0f };
        return v * m;
    }

    inline bool TrivialRejectClip(const std::array<Vec4f, 8>& clip) {
        // クリップ空間での6面に対する同側外側判定 (D3D: 近面 z>=0, 遠面 z<=w)
        // x < -w / x > w / y < -w / y > w / z < 0 / z > w
        auto all_out = [&](auto pred)->bool {
            for (const auto& c : clip) if (!pred(c)) return false;
            return true;
            };
        if (all_out([](const Vec4f& c) { return c.x < -c.w; })) return true; // left
        if (all_out([](const Vec4f& c) { return c.x > c.w; })) return true; // right
        if (all_out([](const Vec4f& c) { return c.y < -c.w; })) return true; // bottom
        if (all_out([](const Vec4f& c) { return c.y > c.w; })) return true; // top
        if (all_out([](const Vec4f& c) { return c.z < 0.0f; })) return true; // near
        if (all_out([](const Vec4f& c) { return c.z > c.w; })) return true; // far
        return false;
    }

    // 12本のエッジ（AABBの辺）インデックス
    static constexpr int kEdges[12][2] = {
        {0,1},{1,3},{3,2},{2,0}, // bottom face (y=0 側と仮定したいが順序は角の定義次第、ここでは頂点配列順に依存しない)
        {4,5},{5,7},{7,6},{6,4}, // top face
        {0,4},{1,5},{2,6},{3,7}  // vertical
    };

    // 近面 z=0 に対するエッジクリップ (clip空間上での線分交点)
    inline bool IntersectEdgeWithNearZ0(const Vec4f& a, const Vec4f& b, Vec4f& out) {
        const float za = a.z, zb = b.z;
        const bool ina = (za >= 0.0f), inb = (zb >= 0.0f);
        if (ina == inb) return false;                // 片方のみが内側のときだけ交点がある
        const float t = za / (za - zb);              // z=0 での比例
        // 同次座標は線形補間でOK（クリップ空間）
        out.x = a.x + (b.x - a.x) * t;
        out.y = a.y + (b.y - a.y) * t;
        out.z = 0.0f;
        out.w = a.w + (b.w - a.w) * t;
        return out.w > 0.0f;                         // 後段の除算保護
    }

    // 返却用：NDC矩形 + wmin
    struct NdcRectWithW {
        float xmin = -1.0f, ymin = -1.0f, xmax = 1.0f, ymax = 1.0f;
        float wmin = 1.0f;      // TestRect に渡す clip-space W
        bool  valid = false;     // 何かしら可視範囲が得られたか
    };

    // 互換API（既存呼び出し箇所はそのまま）
    // 1) Rectangle を返す版
    inline Rectangle ProjectAABBToScreenRect(
        const AABB<float, Vec3f>& box,
        const Matrix<4, 4, float>& worldViewProj,
        float viewportWidth, float viewportHeight,
        float viewportX = 0.0f, float viewportY = 0.0f,
        float clampMargin = 0.0f)
    {
        Rectangle out{};
        // ---- 8コーナー ----
        const Vec3f& L = box.lb;
        const Vec3f& U = box.ub;
        std::array<Vec3f, 8> corners = {
            Vec3f{L.x,L.y,L.z}, Vec3f{U.x,L.y,L.z}, Vec3f{L.x,U.y,L.z}, Vec3f{U.x,U.y,L.z},
            Vec3f{L.x,L.y,U.z}, Vec3f{U.x,L.y,U.z}, Vec3f{L.x,U.y,U.z}, Vec3f{U.x,U.y,U.z}
        };

        // ---- クリップ空間へ ----
        std::array<Vec4f, 8> clip{};
        for (int i = 0; i < 8; ++i) clip[i] = ToClip(worldViewProj, corners[i]);

        // ---- 6面の同側外側なら不可視（早期リジェクト）----
        if (TrivialRejectClip(clip)) {
            // 両方とも不可視のまま返す（screen.visible=false, ndc.valid=false）
            return out;
        }

        // ---- 候補点（z>=0,w>0 の頂点 + 近面(z=0)交点）を収集 ----
        // これを元に NDC/Screen の min/max と wmin を同時計算
        std::array<Vec4f, 20> cand{}; int n = 0;

        for (int i = 0; i < 8; ++i) {
            if (clip[i].z >= 0.0f && clip[i].w > 0.0f) cand[n++] = clip[i];
        }
        for (const auto& e : kEdges) {
            Vec4f inter;
            if (IntersectEdgeWithNearZ0(clip[e[0]], clip[e[1]], inter)) {
                if (n < 20) cand[n++] = inter;
            }
        }
        if (n == 0) {
            // 近面の背後のみ → 画面/ndcともに無効
            return out;
        }

        // ---- Screen の min/max と wmin を 1ループで構築 ----

        float scrMinX = +std::numeric_limits<float>::infinity();
        float scrMinY = +std::numeric_limits<float>::infinity();
        float scrMaxX = -std::numeric_limits<float>::infinity();
        float scrMaxY = -std::numeric_limits<float>::infinity();

        float wmin = +std::numeric_limits<float>::infinity();

        for (int i = 0; i < n; ++i) {
            const Vec4f& c = cand[i];
            if (c.w <= 0.0f) continue; // 念のため

            // wmin（クリップ空間のW。最前の“保守的W” = 最小w）
            wmin = (std::min)(wmin, c.w);

            // NDC
            const float invw = 1.0f / c.w;
            const float x = c.x * invw;           // [-1,1]
            const float y = c.y * invw;           // [-1,1]

            // Screen（D3D規約：そのまま 0..W/H へ）
            const float sx = viewportX + (x * 0.5f + 0.5f) * viewportWidth;
            const float sy = viewportY + (y * 0.5f + 0.5f) * viewportHeight;

            scrMinX = (std::min)(scrMinX, sx);
            scrMinY = (std::min)(scrMinY, sy);
            scrMaxX = (std::max)(scrMaxX, sx);
            scrMaxY = (std::max)(scrMaxY, sy);
        }

        // ---- Rectangle（スクリーン）用。ビューポートにクランプ＋margin ----
        float rminx = (std::max)(viewportX - clampMargin, scrMinX);
        float rminy = (std::max)(viewportY - clampMargin, scrMinY);
        float rmaxx = (std::min)(viewportX + viewportWidth + clampMargin, scrMaxX);
        float rmaxy = (std::min)(viewportY + viewportHeight + clampMargin, scrMaxY);

        if (rminx < rmaxx && rminy < rmaxy) {
            out.x0 = rminx; out.y0 = rminy;
            out.x1 = rmaxx; out.y1 = rmaxy;
            out.visible = true;
        }
        else {
            out = Rectangle{}; // visible=false
        }

		return out;
    }

    // --- AVX2 用の水平縮約ユーティリティ ---
    static inline float hmin8(__m256 v) {
        __m128 l = _mm256_castps256_ps128(v);
        __m128 h = _mm256_extractf128_ps(v, 1);
        __m128 m = _mm_min_ps(l, h);
        __m128 s = _mm_movehdup_ps(m);
        __m128 t = _mm_min_ps(m, s);
        s = _mm_movehl_ps(s, t);
        t = _mm_min_ss(t, s);
        return _mm_cvtss_f32(t);
    }
    static inline float hmax8(__m256 v) {
        __m128 l = _mm256_castps256_ps128(v);
        __m128 h = _mm256_extractf128_ps(v, 1);
        __m128 m = _mm_max_ps(l, h);
        __m128 s = _mm_movehdup_ps(m);
        __m128 t = _mm_max_ps(m, s);
        s = _mm_movehl_ps(s, t);
        t = _mm_max_ss(t, s);
        return _mm_cvtss_f32(t);
    }

    // AABB 8頂点を一括で WVP 変換（row-major & 右掛け版）
  // 変更: Z を追加で出力
    static inline void TransformAABBCorners8_AVX2(
        const SectorFW::Math::AABB<float, SectorFW::Math::Vec3f>& box,
        const SectorFW::Math::Matrix<4, 4, float>& M,
        __m256& X, __m256& Y, __m256& Z, __m256& W
    ) {
        const float lx = box.lb.x, ly = box.lb.y, lz = box.lb.z;
        const float ux = box.ub.x, uy = box.ub.y, uz = box.ub.z;

        const float xs[8] = { lx, ux, lx, ux, lx, ux, lx, ux };
        const float ys[8] = { ly, ly, uy, uy, ly, ly, uy, uy };
        const float zs[8] = { lz, lz, lz, lz, uz, uz, uz, uz };

        __m256 vx = _mm256_loadu_ps(xs);
        __m256 vy = _mm256_loadu_ps(ys);
        __m256 vz = _mm256_loadu_ps(zs);
        __m256 vw = _mm256_set1_ps(1.0f);

        const float* m = M.data();
        const float m00 = m[0], m01 = m[1], m02 = m[2], m03 = m[3];
        const float m10 = m[4], m11 = m[5], m12 = m[6], m13 = m[7];
        const float m20 = m[8], m21 = m[9], m22 = m[10], m23 = m[11];
        const float m30 = m[12], m31 = m[13], m32 = m[14], m33 = m[15];

#if SFW_MATRIX_ROWMAJOR_RMUL
        __m256 rx = _mm256_fmadd_ps(vx, _mm256_set1_ps(m00),
            _mm256_fmadd_ps(vy, _mm256_set1_ps(m10),
                _mm256_fmadd_ps(vz, _mm256_set1_ps(m20),
                    _mm256_mul_ps(vw, _mm256_set1_ps(m30)))));
        __m256 ry = _mm256_fmadd_ps(vx, _mm256_set1_ps(m01),
            _mm256_fmadd_ps(vy, _mm256_set1_ps(m11),
                _mm256_fmadd_ps(vz, _mm256_set1_ps(m21),
                    _mm256_mul_ps(vw, _mm256_set1_ps(m31)))));
        __m256 rz = _mm256_fmadd_ps(vx, _mm256_set1_ps(m02),
            _mm256_fmadd_ps(vy, _mm256_set1_ps(m12),
                _mm256_fmadd_ps(vz, _mm256_set1_ps(m22),
                    _mm256_mul_ps(vw, _mm256_set1_ps(m32)))));
        __m256 rw = _mm256_fmadd_ps(vx, _mm256_set1_ps(m03),
            _mm256_fmadd_ps(vy, _mm256_set1_ps(m13),
                _mm256_fmadd_ps(vz, _mm256_set1_ps(m23),
                    _mm256_mul_ps(vw, _mm256_set1_ps(m33)))));
#else
        // 列ベクトル左掛け/column-major の場合はここを書き換え
#endif

        X = rx; Y = ry; Z = rz; W = rw;
    }

    struct Clip4 { float x, y, z, w; };

    // f(c)=a*x + b*y + c*z + d*w の符号で内外判定（内側: f>=0）
    // Left : x + w >= 0  -> {+1,0,0,+1}
    // Right: x - w <= 0  -> {-1,0,0,+1} でも良いが上と対照で {+1,0,0,-1}
    // Bottom:y + w >= 0  -> {0,+1,0,+1}
    // Top  :y - w <= 0  -> {0,+1,0,-1}
    // Near :z >= 0      -> {0,0,+1,0}
    // （Far: z - w <= 0 -> {0,0,+1,-1} 必要なら）
    struct ClipPlane { float a, b, c, d; };

    inline float EvalPlane(const Clip4& v, const ClipPlane& P) {
        return P.a * v.x + P.b * v.y + P.c * v.z + P.d * v.w;
    }

    inline bool IntersectEdgeWithPlane(const Clip4& A, const Clip4& B,
        const ClipPlane& P, Clip4& out)
    {
        const float fa = EvalPlane(A, P);
        const float fb = EvalPlane(B, P);
        const bool ina = (fa >= 0.0f);
        const bool inb = (fb >= 0.0f);
        if (ina == inb) return false;           // 一方のみ内側のときだけ交点

        const float t = fa / (fa - fb);         // 同次線形補間
        out.x = A.x + (B.x - A.x) * t;
        out.y = A.y + (B.y - A.y) * t;
        out.z = A.z + (B.z - A.z) * t;
        out.w = A.w + (B.w - A.w) * t;
        return out.w != 0.0f;                   // 0除算ガード（必要なら閾値を）
    }

    // SIMD 版：AABB→NDC矩形 + wmin を取得
    inline NdcRectWithW ProjectAABBToNdcRectWithW_SIMD(
        const SectorFW::Math::AABB<float, SectorFW::Math::Vec3f>& box,
        const SectorFW::Math::Matrix<4, 4, float>& worldViewProj
    ) {
        NdcRectWithW out{}; out.valid = false;

        if (box.lb.x > box.ub.x || box.lb.y > box.ub.y || box.lb.z > box.ub.z) {
            out = { 0,0,0,0, 0, false };
            return out;
        }

#if defined(__AVX2__)
        __m256 Xv, Yv, Zv, Wv;
        TransformAABBCorners8_AVX2(box, worldViewProj, Xv, Yv, Zv, Wv); // ← Z を出す版

        alignas(32) float X[8], Y[8], Z[8], W[8];
        _mm256_store_ps(X, Xv); _mm256_store_ps(Y, Yv);
        _mm256_store_ps(Z, Zv); _mm256_store_ps(W, Wv);

        // --- Fast path: 8頂点だけで NDC 矩形 + wmin を取得 ---
        float ndc_min_x = FLT_MAX, ndc_max_x = -FLT_MAX;
        float ndc_min_y = FLT_MAX, ndc_max_y = -FLT_MAX;
        float w_min = FLT_MAX;
        uint8_t maskWpos = 0;

        for (int i = 0; i < 8; ++i) {
            const float w = W[i];
            w_min = (std::min)(w_min, w);
            if (w > 0.0f) maskWpos |= (1u << i);

            const float invw = 1.0f / (std::max)(w, 1e-20f);
            const float nx = X[i] * invw;
            const float ny = Y[i] * invw;
            ndc_min_x = (std::min)(ndc_min_x, nx);
            ndc_max_x = (std::max)(ndc_max_x, nx);
            ndc_min_y = (std::min)(ndc_min_y, ny);
            ndc_max_y = (std::max)(ndc_max_y, ny);
        }

        // 画面内判定の“ゆとり”（端にかなり近い時だけ交点に進む）
        constexpr float EDGE_EPS = 0.02f;

        bool clearlyInside =
            (ndc_min_x > -1.0f + EDGE_EPS) &&
            (ndc_max_x < 1.0f - EDGE_EPS) &&
            (ndc_min_y > -1.0f + EDGE_EPS) &&
            (ndc_max_y < 1.0f - EDGE_EPS);

        if (maskWpos != 0 && clearlyInside) {
            out.xmin = ndc_min_x; out.xmax = ndc_max_x;
            out.ymin = ndc_min_y; out.ymax = ndc_max_y;
            out.wmin = (std::max)(w_min, 1e-6f);
            out.valid = true;
            return out; // ← ここで終了（交点計算しない）
        }

        // --- Slow path: 端面を“またいでいる”面だけ交点追加 ---
        // 面の符号 f = a*x + b*y + c*z + d*w
        auto fL = [&](int i) { return  X[i] + W[i]; };   // Left:  x + w >= 0
        auto fR = [&](int i) { return  X[i] - W[i]; };   // Right: x - w <= 0
        auto fB = [&](int i) { return  Y[i] + W[i]; };   // Bottom:y + w >= 0
        auto fT = [&](int i) { return  Y[i] - W[i]; };   // Top:   y - w <= 0
        auto fN = [&](int i) { return  Z[i]; };          // Near:  z >= 0

        auto buildMask = [&](auto f)->uint8_t {
            uint8_t m = 0; for (int i = 0; i < 8; ++i) if (f(i) >= 0.0f) m |= (1u << i); return m;
            };
        uint8_t mL = buildMask(fL), mR = buildMask(fR), mB = buildMask(fB), mT = buildMask(fT), mN = buildMask(fN);

        auto needPlane = [&](uint8_t m) { return m != 0 && m != 0xFF; }; // 内外が混在＝“またいでいる”

        // 初期候補: z>=0 & w>0 の頂点だけ
        struct Clip4 { float x, y, z, w; };
        Clip4 cand[32]; int n = 0;
        for (int i = 0; i < 8; ++i) if (Z[i] >= 0.0f && W[i] > 0.0f) cand[n++] = { X[i],Y[i],Z[i],W[i] };

        // 交点ユーティリティ（同次線形補間）
        auto intersectPlane = [](const Clip4& A, const Clip4& B, float fa, float fb)->Clip4 {
            const float t = fa / (fa - fb);
            return { A.x + (B.x - A.x) * t, A.y + (B.y - A.y) * t, A.z + (B.z - A.z) * t, A.w + (B.w - A.w) * t };
            };

        // 対象面だけ処理
        auto addIntersectionsForPlane = [&](auto fPlane) {
            for (int e = 0; e < 12; ++e) {
                int i0 = kEdges[e][0], i1 = kEdges[e][1];
                float fa = fPlane(i0), fb = fPlane(i1);
                bool ina = (fa >= 0.0f), inb = (fb >= 0.0f);
                if (ina == inb) continue;
                Clip4 A{ X[i0],Y[i0],Z[i0],W[i0] };
                Clip4 B{ X[i1],Y[i1],Z[i1],W[i1] };
                Clip4 I = intersectPlane(A, B, fa, fb);
                if (I.w > 0.0f) { if (n < (int)std::size(cand)) cand[n++] = I; }
            }
            };

        if (needPlane(mL)) addIntersectionsForPlane(fL);
        if (needPlane(mR)) addIntersectionsForPlane(fR);
        if (needPlane(mB)) addIntersectionsForPlane(fB);
        if (needPlane(mT)) addIntersectionsForPlane(fT);
        if (needPlane(mN)) addIntersectionsForPlane(fN);
        // （Far が必要なら同様に）

        if (n == 0) return out;

        // 交点を含めて最終 NDC 矩形 + wmin
        float minx = FLT_MAX, maxx = -FLT_MAX, miny = FLT_MAX, maxy = -FLT_MAX, wmin = FLT_MAX;
        bool anyFront = false;
        for (int i = 0; i < n; ++i) {
            const Clip4& c = cand[i];
            if (c.w <= 0.0f) continue;
            anyFront = true;
            wmin = (std::min)(wmin, c.w);
            const float invw = 1.0f / (std::max)(c.w, 1e-20f);
            const float nx = c.x * invw, ny = c.y * invw;
            minx = (std::min)(minx, nx); maxx = (std::max)(maxx, nx);
            miny = (std::min)(miny, ny); maxy = (std::max)(maxy, ny);
        }
        if (!anyFront) return out;

        out.xmin = (std::max)(minx, -1.0f);
        out.ymin = (std::max)(miny, -1.0f);
        out.xmax = (std::min)(maxx, 1.0f);
        out.ymax = (std::min)(maxy, 1.0f);
        out.wmin = (std::max)(wmin, 1e-6f);
        out.valid = (out.xmin < out.xmax) && (out.ymin < out.ymax);
        return out;
#else
        // SSE/スカラ fallback
        const float lx = box.lb.x, ly = box.lb.y, lz = box.lb.z;
        const float ux = box.ub.x, uy = box.ub.y, uz = box.ub.z;
        const SectorFW::Math::Vec3f c[8] = {
            {lx,ly,lz},{ux,ly,lz},{lx,uy,lz},{ux,uy,lz},
            {lx,ly,uz},{ux,ly,uz},{lx,uy,uz},{ux,uy,uz},
        };

        float ndc_min_x = FLT_MAX, ndc_max_x = -FLT_MAX;
        float ndc_min_y = FLT_MAX, ndc_max_y = -FLT_MAX;
        float w_min = FLT_MAX;
        bool  anyFront = false;

        for (int i = 0; i < 8; ++i) {
            SectorFW::Math::Vec4f p = { c[i].x, c[i].y, c[i].z, 1.0f };
            SectorFW::Math::Vec4f q = p * worldViewProj; // 行列の向きに合わせて

            w_min = (std::min)(w_min, q.w);
            if (q.w > 0.0f) anyFront = true;

            const float invW = 1.0f / (std::max)(q.w, 1e-20f);
            const float nx = q.x * invW;
            const float ny = q.y * invW;

            ndc_min_x = (std::min)(ndc_min_x, nx);
            ndc_max_x = (std::max)(ndc_max_x, nx);
            ndc_min_y = (std::min)(ndc_min_y, ny);
            ndc_max_y = (std::max)(ndc_max_y, ny);
        }
        out.xmin = ndc_min_x; out.xmax = ndc_max_x;
        out.ymin = ndc_min_y; out.ymax = ndc_max_y;
        out.wmin = w_min;
        out.valid = anyFront && (out.xmin <= out.xmax) && (out.ymin <= out.ymax);
#endif
        return out;
    }

} // namespace SectorFW::Math

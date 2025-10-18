/*****************************************************************//**
 * @file   Rectangle.hpp  (row-major storage × column-vector convention)
 * @brief  スクリーン矩形/NDC矩形ユーティリティ（列ベクトル規約対応版）
 * @author you
 * @date   2025
 *********************************************************************/

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <vector>

#if defined(_MSC_VER) || defined(__SSE2__)
#include <immintrin.h>
#endif

#include "Matrix.hpp"

namespace SectorFW::Math {

    struct NdcRectWithW {
        float xmin, ymin, xmax, ymax;
        float wmin;   // 8頂点の clip.w の最小値（MOCなどに渡す時に使用）
        bool  valid;
    };

    struct Rectangle {
        // 画面座標（ピクセル）またはNDCに使い回し可。x0<=x1, y0<=y1 を保証。
        float x0{}, y0{}, x1{}, y1{};
        bool  valid{ false };

        void Normalize() noexcept {
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
        }
        float Width()  const noexcept { return valid ? (x1 - x0) : 0.0f; }
        float Height() const noexcept { return valid ? (y1 - y0) : 0.0f; }
        float Area()   const noexcept { return valid ? Width() * Height() : 0.0f; }
        void ExpandToInclude(float x, float y) noexcept {
            if (!valid) { x0 = x1 = x; y0 = y1 = y; valid = true; return; }
            x0 = (std::min)(x0, x); x1 = (std::max)(x1, x);
            y0 = (std::min)(y0, y); y1 = (std::max)(y1, y);
        }
        static Rectangle Union(const Rectangle& a, const Rectangle& b) noexcept {
            if (!a.valid) return b;
            if (!b.valid) return a;
            Rectangle r{ (std::min)(a.x0,b.x0), (std::min)(a.y0,b.y0),
                         (std::max)(a.x1,b.x1), (std::max)(a.y1,b.y1), true };
            return r;
        }

        std::vector<Vec2f> MakeLineVertex() const noexcept
        {
            std::vector<Vec2f> out(8);
            out.push_back({ x0, y0 });
            out.push_back({ x1, y0 });
            out.push_back({ x1, y0 });
            out.push_back({ x1, y1 });
            out.push_back({ x1, y1 });
            out.push_back({ x0, y1 });
            out.push_back({ x0, y1 });
            out.push_back({ x0, y0 });
            return out;
        }
    };

    // ビューポート
    struct ViewportRect {
        float width{};
        float height{};
        // ピクセル座標への変換で y の符号を反転するか（D3D: 上→下が + なので反転しないのが一般的）
        bool  originTopLeft{ true };
    };

    //======================================================================
    // AABB 8頂点を NDC に投影（列ベクトル規約 / row-major）
    // ・VP = P * V を渡すこと（列ベクトルの標準）
    // ・clip.w <= 0（視点背面）は NDC 化できないので除外
    // ・GL の −1..1 にも対応（分岐）
    //======================================================================
    inline NdcRectWithW ProjectAABBToNdc(const Matrix4x4f& VP,
        const float lbx, const float lby, const float lbz,
        const float ubx, const float uby, const float ubz,
        ClipZRange zrange = ClipZRange::ZeroToOne) noexcept
    {
        const float xs[8] = { lbx, ubx, lbx, ubx, lbx, ubx, lbx, ubx };
        const float ys[8] = { lby, lby, uby, uby, lby, lby, uby, uby };
        const float zs[8] = { lbz, lbz, lbz, lbz, ubz, ubz, ubz, ubz };

        float xmin = FLT_MAX, ymin = FLT_MAX, xmax = -FLT_MAX, ymax = -FLT_MAX, wmin = FLT_MAX;
        bool any = false;

        for (int i = 0; i < 8; ++i) {
            float cx, cy, cz, cw;
            MulPoint_RowMajor_ColVec(VP, xs[i], ys[i], zs[i], cx, cy, cz, cw);

            // 背面（cw<=0）は NDC にできない。必要ならここで近似クリップ。
            if (cw <= 0.0f) continue;

            float ndcX = cx / cw;
            float ndcY = cy / cw;
            float ndcZ = cz / cw;

            // Z 範囲の正規化（必要なら −1..1 → 0..1 へ寄せる or 逆）
            if (zrange == ClipZRange::NegOneToOne) {
                // GL: z∈[-1,1] をそのまま持つ。ここでは矩形算出なので z は未使用。
            }
            else {
                // DX: z∈[0,1] 前提。ここも矩形算出では未使用。
            }

            xmin = (std::min)(xmin, ndcX); xmax = (std::max)(xmax, ndcX);
            ymin = (std::min)(ymin, ndcY); ymax = (std::max)(ymax, ndcY);
            wmin = (std::min)(wmin, cw);
            any = true;
        }

        // 1点も NDC 化できなければ invalid
        if (!any) return { 0,0,0,0, wmin, false };

        // NDC は通常 [-1,1]。ここではそのまま返す（後でピクセルへ変換）
        return { xmin, ymin, xmax, ymax, wmin, true };
    }

    //======================================================================
    // NDC 矩形 → ピクセル矩形 変換
    //  ・NDC x,y ∈ [-1,1] を viewport へ
    //  ・originTopLeft=true: y ピクセル上向き→下向き（D3D標準）
    //======================================================================
    inline Rectangle NdcToPixels(const NdcRectWithW& ndc, const ViewportRect& vp) noexcept
    {
        if (!ndc.valid || vp.width <= 0 || vp.height <= 0) return {};

        // NDC [-1,1] → [0,1]
        const float nx0 = (ndc.xmin * 0.5f) + 0.5f;
        const float ny0 = (ndc.ymin * 0.5f) + 0.5f;
        const float nx1 = (ndc.xmax * 0.5f) + 0.5f;
        const float ny1 = (ndc.ymax * 0.5f) + 0.5f;

        Rectangle r{};
        r.valid = true;

        // x: 0..1 → 0..W
        r.x0 = nx0 * vp.width;
        r.x1 = nx1 * vp.width;

        // y: 上端の扱い
        if (vp.originTopLeft) {
            // D3D/UI: (0,0) が上左。NDC +1 が上、-1 が下 なので反転してから高さを掛ける
            const float py0 = (1.0f - ny0) * vp.height;
            const float py1 = (1.0f - ny1) * vp.height;
            r.y0 = (std::min)(py0, py1);
            r.y1 = (std::max)(py0, py1);
        }
        else {
            // GL 的下原点
            r.y0 = ny0 * vp.height;
            r.y1 = ny1 * vp.height;
        }

        r.Normalize();
        return r;
    }

    //======================================================================
    // ワールド空間 AABB → ピクセル矩形（1発ヘルパ）
    //  ・VP は列ベクトル規約で VP=P*V
    //======================================================================
    inline Rectangle ProjectAABBToScreenRect_Pixels(const Matrix4x4f& VP,
        const float lbx, const float lby, const float lbz,
        const float ubx, const float uby, const float ubz,
        const ViewportRect& vp,
        ClipZRange zrange = ClipZRange::ZeroToOne) noexcept
    {
        const NdcRectWithW ndc = ProjectAABBToNdc(VP, lbx, lby, lbz, ubx, uby, ubz, zrange);
        return NdcToPixels(ndc, vp);
    }

    inline Rectangle ProjectAABBToScreenRect_Pixels(const Matrix4x4f& VP,
        const AABB3f& box,
        const ViewportRect& vp,
        ClipZRange zrange = ClipZRange::ZeroToOne) noexcept
    {
        const NdcRectWithW ndc = ProjectAABBToNdc(VP, box.lb.x, box.lb.y, box.lb.z, box.ub.x, box.ub.y, box.ub.z, zrange);
        return NdcToPixels(ndc, vp);
    }

    // ------------- 内部ユーティリティ -------------
    inline Vec4f ToClip(const Matrix<4, 4, float>& m, const Vec3f& p) {
        // 同次座標 (x,y,z,1) として変換
        const Vec4f v = { p.x, p.y, p.z, 1.0f };
        return m * v;
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
            out.valid = true;
        }
        else {
            out = Rectangle{}; // visible=false
        }

        return out;
    }

    //======================================================================
    // 画面占有率（LOD 判定等に）
    //  - 画面全体に対する矩形の面積比（0..1）
    //======================================================================
    inline float ScreenCoverage01(const Rectangle& px, const ViewportRect& vp) noexcept
    {
        if (!px.valid || vp.width <= 0 || vp.height <= 0) return 0.0f;
        const float area = px.Area();
        return (area <= 0.0f) ? 0.0f : (area / (vp.width * vp.height));
    }

    // 8頂点のローカル座標を SoA で用意（AABB corner パターン）
    static inline void MakeAabbCorners8(const AABB3f& b,
        float xs[8], float ys[8], float zs[8]) noexcept {
        const float lx = b.lb.x, ly = b.lb.y, lz = b.lb.z;
        const float ux = b.ub.x, uy = b.ub.y, uz = b.ub.z;
        xs[0] = lx; ys[0] = ly; zs[0] = lz;
        xs[1] = ux; ys[1] = ly; zs[1] = lz;
        xs[2] = lx; ys[2] = uy; zs[2] = lz;
        xs[3] = ux; ys[3] = uy; zs[3] = lz;
        xs[4] = lx; ys[4] = ly; zs[4] = uz;
        xs[5] = ux; ys[5] = ly; zs[5] = uz;
        xs[6] = lx; ys[6] = uy; zs[6] = uz;
        xs[7] = ux; ys[7] = uy; zs[7] = uz;
    }

    // 8点を同次クリップ空間へ: c = VPW * [x y z 1]^T（列ベクトル前提）
    static inline void Transform8_Clip_AVX2(const Matrix4x4f& M,
        const float xs[8], const float ys[8], const float zs[8],
        __m256& cx, __m256& cy, __m256& cz, __m256& cw) noexcept
    {
        const __m256 X = _mm256_loadu_ps(xs);
        const __m256 Y = _mm256_loadu_ps(ys);
        const __m256 Z = _mm256_loadu_ps(zs);
        const __m256 ONE = _mm256_set1_ps(1.0f);

        // row-major 行列 × 列ベクトル = 「各行と (x,y,z,1) のドット積」
        // cx = m00*X + m01*Y + m02*Z + m03
        cx = _mm256_set1_ps(M[0][3]);
        cx = _mm256_fmadd_ps(_mm256_set1_ps(M[0][0]), X, cx);
        cx = _mm256_fmadd_ps(_mm256_set1_ps(M[0][1]), Y, cx);
        cx = _mm256_fmadd_ps(_mm256_set1_ps(M[0][2]), Z, cx);

        cy = _mm256_set1_ps(M[1][3]);
        cy = _mm256_fmadd_ps(_mm256_set1_ps(M[1][0]), X, cy);
        cy = _mm256_fmadd_ps(_mm256_set1_ps(M[1][1]), Y, cy);
        cy = _mm256_fmadd_ps(_mm256_set1_ps(M[1][2]), Z, cy);

        cz = _mm256_set1_ps(M[2][3]);
        cz = _mm256_fmadd_ps(_mm256_set1_ps(M[2][0]), X, cz);
        cz = _mm256_fmadd_ps(_mm256_set1_ps(M[2][1]), Y, cz);
        cz = _mm256_fmadd_ps(_mm256_set1_ps(M[2][2]), Z, cz);

        cw = _mm256_set1_ps(M[3][3]);
        cw = _mm256_fmadd_ps(_mm256_set1_ps(M[3][0]), X, cw);
        cw = _mm256_fmadd_ps(_mm256_set1_ps(M[3][1]), Y, cw);
        cw = _mm256_fmadd_ps(_mm256_set1_ps(M[3][2]), Z, cw);
        // + 1.0f * m[3][3] は上の初期値ですでに加算済み
        (void)ONE;
    }

    // 8点の outcode を一気に作る（LH×ZeroToOne の6平面＋ w<=0）
    // 各平面の 8bit マスクを返す（bit i は頂点 i の outside）
    static inline uint8_t OutsideMaskForPlane(__m256 x, __m256 y, __m256 z, __m256 w, int plane) noexcept {
        __m256 cmp;
        switch (plane) {
        case 0: cmp = _mm256_cmp_ps(x, _mm256_sub_ps(_mm256_setzero_ps(), w), _CMP_LT_OS); break; // x < -w
        case 1: cmp = _mm256_cmp_ps(x, w, _CMP_GT_OS); break;                                    // x >  w
        case 2: cmp = _mm256_cmp_ps(y, _mm256_sub_ps(_mm256_setzero_ps(), w), _CMP_LT_OS); break; // y < -w
        case 3: cmp = _mm256_cmp_ps(y, w, _CMP_GT_OS); break;                                    // y >  w
        case 4: cmp = _mm256_cmp_ps(z, _mm256_setzero_ps(), _CMP_LT_OS); break;                  // z <  0
        case 5: cmp = _mm256_cmp_ps(z, w, _CMP_GT_OS); break;                                    // z >  w
        default: cmp = _mm256_cmp_ps(w, _mm256_setzero_ps(), _CMP_LE_OQ); break;                 // w <= 0
        }
        return static_cast<uint8_t>(_mm256_movemask_ps(cmp)); // 8bit
    }

    // reduce: 8 lanes の min/max
    static inline float hmin8(__m256 v) noexcept {
        __m128 lo = _mm256_castps256_ps128(v);
        __m128 hi = _mm256_extractf128_ps(v, 1);
        __m128 m1 = _mm_min_ps(lo, hi);
        __m128 shuf = _mm_movehdup_ps(m1);
        __m128 m2 = _mm_min_ps(m1, shuf);
        shuf = _mm_movehl_ps(shuf, m2);
        __m128 m3 = _mm_min_ss(m2, shuf);
        return _mm_cvtss_f32(m3);
    }
    static inline float hmax8(__m256 v) noexcept {
        __m128 lo = _mm256_castps256_ps128(v);
        __m128 hi = _mm256_extractf128_ps(v, 1);
        __m128 m1 = _mm_max_ps(lo, hi);
        __m128 shuf = _mm_movehdup_ps(m1);
        __m128 m2 = _mm_max_ps(m1, shuf);
        shuf = _mm_movehl_ps(shuf, m2);
        __m128 m3 = _mm_max_ss(m2, shuf);
        return _mm_cvtss_f32(m3);
    }

    // 列ベクトル規約 / row-major 格納。VPW = (proj * view * world)
    inline NdcRectWithW ProjectAABBToNdc_Robust(const Matrix4x4f& VPW, const AABB3f& box) {
        // 1) AABB 8頂点を clip 空間へ
        const Vec3f& L = box.lb;
        const Vec3f& U = box.ub;
        std::array<Vec3f, 8> corners = {
           Vec3f{L.x,L.y,L.z}, Vec3f{U.x,L.y,L.z}, Vec3f{L.x,U.y,L.z}, Vec3f{U.x,U.y,L.z},
           Vec3f{L.x,L.y,U.z}, Vec3f{U.x,L.y,U.z}, Vec3f{L.x,U.y,U.z}, Vec3f{U.x,U.y,U.z}
        };

        Vec4f clip[8];
        for (int i = 0; i < 8; ++i) {
            Vec3f p = corners[i];                // (lb/ub の組合せ)
            Vec4f v = { p.x, p.y, p.z, 1.0f };
            clip[i] = VPW * v;                      // 列ベクトルで右掛け
        }

        // 2) 12エッジを列挙
        static const int E[12][2] = {
            {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7}
        };

        // 3) クリップ多角形の頂点バッファ（最大: 8 + 12*6 の交点だが通常は少量）
        std::vector<Vec4f> poly; poly.reserve(32);

        // 3a) 最初は「各頂点がクリップ内なら採用」
        auto inside = [](const Vec4f& c) -> bool {
            const float x = c.x, y = c.y, z = c.z, w = c.w;
            if (w <= 0.0f) return false;
            if (x < -w || x > w) return false;
            if (y < -w || y > w) return false;
            if (z <  0.0f || z > w) return false;   // LH ZeroToOne
            return true;
            };
        for (int i = 0; i < 8; ++i) if (inside(clip[i])) poly.push_back(clip[i]);

        // 3b) 各エッジについて、各クリップ平面で「外→内」の交点を追加
        auto addIntersect = [&](const Vec4f& a, const Vec4f& b, int plane) {
            // plane: 0:x=-w,1:x=+w,2:y=-w,3:y=+w,4:z=0,5:z=w
            // f(v) >= 0 が内側になる符号関数を作る
            auto f = [&](const Vec4f& v)->float {
                switch (plane) {
                case 0: return v.x + v.w;       // x >= -w
                case 1: return -v.x + v.w;      // x <=  w
                case 2: return v.y + v.w;       // y >= -w
                case 3: return -v.y + v.w;      // y <=  w
                case 4: return v.z;             // z >= 0
                default:return v.w - v.z;       // z <= w
                }
                };
            float fa = f(a), fb = f(b);
            if ((fa < 0 && fb>0) || (fa > 0 && fb < 0)) {
                float t = fa / (fa - fb);          // 線形補間で交点
                Vec4f p = a + (b - a) * t;           // 同次で補間
                // w<=0 の数値不安定を避けるガード
                if (p.w > 1e-6f) poly.push_back(p);
            }
            };

        for (auto& e : E) {
            const Vec4f& a = clip[e[0]];
            const Vec4f& b = clip[e[1]];
            // 6平面すべてに対して交差チェック
            for (int plane = 0; plane < 6; ++plane) addIntersect(a, b, plane);
        }

        // 万一すべて外：無効
        if (poly.empty()) return { 0,0,0,0, +INFINITY, false };

        // 4) NDCへ割り算 → min/max
        float xmin = +1e9f, ymin = +1e9f, xmax = -1e9f, ymax = -1e9f;
        float wmin = +1e9f;
        for (const Vec4f& c : poly) {
            float invw = 1.0f / c.w;
            float x = c.x * invw;
            float y = c.y * invw;
            xmin = (std::min)(xmin, x);
            ymin = (std::min)(ymin, y);
            xmax = (std::max)(xmax, x);
            ymax = (std::max)(ymax, y);
            wmin = (std::min)(wmin, c.w); // MOC 用
        }

        // 5) 画面端での安定化（[-1,1]にクランプし、反転に対処）
        xmin = (std::max)(-1.0f, (std::min)(1.0f, xmin));
        xmax = (std::max)(-1.0f, (std::min)(1.0f, xmax));
        ymin = (std::max)(-1.0f, (std::min)(1.0f, ymin));
        ymax = (std::max)(-1.0f, (std::min)(1.0f, ymax));

        if (xmax < xmin) std::swap(xmin, xmax);
        if (ymax < ymin) std::swap(ymin, ymax);

        return { xmin, ymin, xmax, ymax, wmin, true };
    }

    // 早期破棄つき・高速 NDC 投影。部分交差は robustClip() に委譲。
    inline NdcRectWithW ProjectAABBToNdc_Fast(const Matrix4x4f& VPW, const AABB3f& box,
        float min_coverage_for_precise = 0.0f) // 例: 0.001f
    {
        // (1) 8頂点→clip
        float xs[8], ys[8], zs[8];
        MakeAabbCorners8(box, xs, ys, zs);
        __m256 cx, cy, cz, cw;
        Transform8_Clip_AVX2(VPW, xs, ys, zs, cx, cy, cz, cw);

        // (2) Outcode 集計
        uint8_t all_out_any_plane = 0;
        uint8_t any_out_any_plane = 0;
        for (int p = 0; p < 6; ++p) {
            uint8_t m = OutsideMaskForPlane(cx, cy, cz, cw, p);
            all_out_any_plane |= (uint8_t)(m == 0xFF); // 全頂点が同じ平面の外
            any_out_any_plane |= (uint8_t)(m != 0x00); // 1点でも外
        }
        // w<=0
        uint8_t wneg = OutsideMaskForPlane(cx, cy, cz, cw, 6);
        if (wneg == 0xFF) return { 0,0,0,0, +INFINITY, false }; // 全点 w<=0

        if (all_out_any_plane) {
            // 完全に外（早期破棄）
            return { 0,0,0,0, +INFINITY, false };
        }

        if (!any_out_any_plane && wneg == 0x00) {
            // (3) 完全内包: SIMD で透視除算 & min/max
            // invw ≈ rcp + 1回ニュートン (精度安定)
            __m256 invw = _mm256_rcp_ps(cw);
            invw = _mm256_mul_ps(_mm256_fnmadd_ps(cw, invw, _mm256_set1_ps(2.0f)), invw); // invw *= (2 - w*invw)

            __m256 ndcX = _mm256_mul_ps(cx, invw);
            __m256 ndcY = _mm256_mul_ps(cy, invw);

            float xmin = hmin8(ndcX), xmax = hmax8(ndcX);
            float ymin = hmin8(ndcY), ymax = hmax8(ndcY);
            float wmin = hmin8(cw);

            // クランプ＆整形
            xmin = (std::max)(-1.0f, (std::min)(1.0f, xmin));
            xmax = (std::max)(-1.0f, (std::min)(1.0f, xmax));
            ymin = (std::max)(-1.0f, (std::min)(1.0f, ymin));
            ymax = (std::max)(-1.0f, (std::min)(1.0f, ymax));
            if (xmax < xmin) std::swap(xmin, xmax);
            if (ymax < ymin) std::swap(ymin, ymax);

            // 早期破棄（小物カットをしたい場合）
            if (min_coverage_for_precise > 0.0f) {
                float covX = (std::max)(0.f, (std::min)(1.f, (xmax - xmin) * 0.5f));
                float covY = (std::max)(0.f, (std::min)(1.f, (ymax - ymin) * 0.5f));
                if (covX * covY < min_coverage_for_precise) {
                    return { xmin, ymin, xmax, ymax, wmin, false }; // 可視性は低いのでスキップ目安
                }
            }
            return { xmin, ymin, xmax, ymax, wmin, true };
        }

        // (4) 部分交差: 重いクリップへ（near/x=±w/y=±w/z=w）
        //  小物しきい値でスキップしたい場合は事前に球近似で cheap 判定を入れてもOK
        return ProjectAABBToNdc_Robust(VPW, box);
    }

    static inline __m256 rcp_nr1(__m256 x) noexcept {
        __m256 r = _mm256_rcp_ps(x);
        // r = r * (2 - x*r)
        return _mm256_mul_ps(_mm256_fnmadd_ps(x, r, _mm256_set1_ps(2.0f)), r);
    }

    // 安全除算（配列化用）
    static inline void safe_div8(const float* num, const float* den, float* out) {
        constexpr float eps = 1e-6f;
        for (int i = 0; i < 8; ++i) {
            float d = std::fabs(den[i]) < eps ? (den[i] < 0 ? -eps : eps) : den[i];
            out[i] = num[i] / d;
        }
    }

    // WVP (= proj*view*world) で 8球を一括投影して可視判定/矩形を返す
    // - BoundingSphere は { Vec3 center; float radius; } を想定
    // - 列ベクトル規約：clip = WVP * [x,y,z,1]^T
    template<class Mat4, class BoundingSphereT>
    inline void ProjectSpheresToNdc_Fast_AVX2(
        const Mat4& WVP,
        const BoundingSphereT* spheres,     // 長さ>=count
        size_t count,
        NdcRectWithW* outRects              // 長さ>=count
    ) {
        const size_t step = 8;
        for (size_t base = 0; base < count; base += step) {
            const size_t n = (std::min)(step, count - base);

            // SoA ロード
            float cx8[8]{}, cy8[8]{}, cz8[8]{}, cw8[8]{};
            float px8[8]{}, py8[8]{}, pz8[8]{};
            float pxw8[8]{}, pyw8[8]{}, pzw8[8]{};
            float ndc_cx8[8]{}, ndc_cy8[8]{}, ndc_cz8[8]{};
            float ndc_pxx8[8]{}, ndc_pyy8[8]{}, ndc_pzz8[8]{};

            float X[8]{}, Y[8]{}, Z[8]{}, R[8]{};
            for (size_t i = 0; i < n; ++i) {
                const auto& s = spheres[base + i];
                X[i] = s.center.x;
                Y[i] = s.center.y;
                Z[i] = s.center.z;
                R[i] = s.radius;
            }

            // AVX2 ブロック
            __m256 VX = _mm256_loadu_ps(X);
            __m256 VY = _mm256_loadu_ps(Y);
            __m256 VZ = _mm256_loadu_ps(Z);
            __m256 VR = _mm256_loadu_ps(R);
            __m256 ONE = _mm256_set1_ps(1.0f);

            // row-major × 列ベクトル：各行と (x,y,z,1) のドット積
            auto dot_row = [&](int row, __m256 x, __m256 y, __m256 z, __m256 one) {
                __m256 sum = _mm256_set1_ps(WVP[row][3]);                  // m[row][3] * 1
                sum = _mm256_fmadd_ps(_mm256_set1_ps(WVP[row][0]), x, sum);
                sum = _mm256_fmadd_ps(_mm256_set1_ps(WVP[row][1]), y, sum);
                sum = _mm256_fmadd_ps(_mm256_set1_ps(WVP[row][2]), z, sum);
                (void)one;
                return sum;
                };

            // 中心の clip 座標
            __m256 Cx = dot_row(0, VX, VY, VZ, ONE);
            __m256 Cy = dot_row(1, VX, VY, VZ, ONE);
            __m256 Cz = dot_row(2, VX, VY, VZ, ONE);
            __m256 Cw = dot_row(3, VX, VY, VZ, ONE);

            // +X（局所）: (x+R,y,z,1) = 中心に m[*,0]*R を足すだけ
            __m256 Px = _mm256_fmadd_ps(_mm256_set1_ps(WVP[0][0]), VR, Cx);
            __m256 Py = _mm256_fmadd_ps(_mm256_set1_ps(WVP[1][0]), VR, Cy);
            __m256 Pz = _mm256_fmadd_ps(_mm256_set1_ps(WVP[2][0]), VR, Cz);
            __m256 Pw = _mm256_fmadd_ps(_mm256_set1_ps(WVP[3][0]), VR, Cw);

            // +Y（局所）
            __m256 Qx = _mm256_fmadd_ps(_mm256_set1_ps(WVP[0][1]), VR, Cx);
            __m256 Qy = _mm256_fmadd_ps(_mm256_set1_ps(WVP[1][1]), VR, Cy);
            __m256 Qz = _mm256_fmadd_ps(_mm256_set1_ps(WVP[2][1]), VR, Cz);
            __m256 Qw = _mm256_fmadd_ps(_mm256_set1_ps(WVP[3][1]), VR, Cw);

            // +Z（局所）… z 可視範囲推定用
            __m256 Rx = _mm256_fmadd_ps(_mm256_set1_ps(WVP[0][2]), VR, Cx);
            __m256 Ry = _mm256_fmadd_ps(_mm256_set1_ps(WVP[1][2]), VR, Cy);
            __m256 Rz = _mm256_fmadd_ps(_mm256_set1_ps(WVP[2][2]), VR, Cz);
            __m256 Rw = _mm256_fmadd_ps(_mm256_set1_ps(WVP[3][2]), VR, Cw);

            // 透視除算（1回ニュートンで安定化）
            __m256 invCw = rcp_nr1(Cw);
            __m256 invPw = rcp_nr1(Pw);
            __m256 invQw = rcp_nr1(Qw);
            __m256 invRw = rcp_nr1(Rw);

            __m256 NdC_x = _mm256_mul_ps(Cx, invCw);
            __m256 NdC_y = _mm256_mul_ps(Cy, invCw);
            __m256 NdC_z = _mm256_mul_ps(Cz, invCw);

            __m256 NdP_x = _mm256_mul_ps(Px, invPw); // +X の x
            __m256 NdQ_y = _mm256_mul_ps(Qy, invQw); // +Y の y
            __m256 NdR_z = _mm256_mul_ps(Rz, invRw); // +Z の z

            // 画面半径 r_ndc = max(|px.x - c.x|, |py.y - c.y|)
            __m256 dx = _mm256_andnot_ps(_mm256_set1_ps(-0.f), _mm256_sub_ps(NdP_x, NdC_x)); // fabs
            __m256 dy = _mm256_andnot_ps(_mm256_set1_ps(-0.f), _mm256_sub_ps(NdQ_y, NdC_y));
            __m256 r = _mm256_max_ps(dx, dy);

            // NDC 矩形
            __m256 xmin = _mm256_sub_ps(NdC_x, r);
            __m256 xmax = _mm256_add_ps(NdC_x, r);
            __m256 ymin = _mm256_sub_ps(NdC_y, r);
            __m256 ymax = _mm256_add_ps(NdC_y, r);

            // [-1,1] にクランプ
            __m256 N1 = _mm256_set1_ps(-1.0f), P1 = _mm256_set1_ps(1.0f);
            xmin = _mm256_min_ps(P1, _mm256_max_ps(N1, xmin));
            xmax = _mm256_min_ps(P1, _mm256_max_ps(N1, xmax));
            ymin = _mm256_min_ps(P1, _mm256_max_ps(N1, ymin));
            ymax = _mm256_min_ps(P1, _mm256_max_ps(N1, ymax));

            // 配列へストア
            _mm256_storeu_ps(cx8, Cx); _mm256_storeu_ps(cy8, Cy);
            _mm256_storeu_ps(cz8, Cz); _mm256_storeu_ps(cw8, Cw);
            _mm256_storeu_ps(px8, xmin); _mm256_storeu_ps(py8, ymin);
            _mm256_storeu_ps(pz8, xmax);
            _mm256_storeu_ps(pxw8, ymax);

            _mm256_storeu_ps(ndc_cx8, NdC_x);
            _mm256_storeu_ps(ndc_cy8, NdC_y);
            _mm256_storeu_ps(ndc_cz8, NdC_z);
            _mm256_storeu_ps(ndc_pxx8, NdP_x);
            _mm256_storeu_ps(ndc_pyy8, NdQ_y);
            _mm256_storeu_ps(ndc_pzz8, NdR_z);

            _mm256_storeu_ps(pyw8, Pw);
            _mm256_storeu_ps(pzw8, Qw);
            _mm256_storeu_ps(pxw8, Pw); // 既に Pw を格納済だが wmin 計算で再利用
            _mm256_storeu_ps(pz8, Rw);

            // 各レーンをまとめて可視判定・出力
            for (size_t i = 0; i < n; ++i) {
                const float ndc_cx = ndc_cx8[i];
                const float ndc_cy = ndc_cy8[i];
                const float ndc_cz = ndc_cz8[i];

                const float ndc_pz = ndc_pzz8[i];

                // 直前で xmin/xmax/ymin/ymax は [-1,1] にクランプ済み
                float xmin_s = px8[i];
                float ymin_s = py8[i];
                float xmax_s = pz8[i];
                float ymax_s = pxw8[i];

                if (xmax_s < xmin_s) std::swap(xmin_s, xmax_s);
                if (ymax_s < ymin_s) std::swap(ymin_s, ymax_s);

                // z の簡易交差（中心と +Z で保守的）
                float zmin_est = (std::min)(ndc_cz, ndc_pz);
                float zmax_est = (std::max)(ndc_cz, ndc_pz);

                const bool x_overlap = !(xmax_s < -1.0f || xmin_s > 1.0f);
                const bool y_overlap = !(ymax_s < -1.0f || ymin_s > 1.0f);
                const bool z_overlap = !(zmax_est < 0.0f || zmin_est > 1.0f);

                // wmin（MOC 等の使用を想定）
                float wmin = (std::min)({ cw8[i], /* Pw */ pxw8[i], /* Qw */ pzw8[i], /* Rw */ pz8[i] });

                outRects[base + i] = {
                    xmin_s, ymin_s, xmax_s, ymax_s,
                    wmin,
                    (x_overlap && y_overlap && z_overlap)
                };
            }
        }
    }
} // namespace SectorFW::Math
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

    // ------------- 公開関数 -------------
    // AABBをビューポート矩形に射影（DirectX規約: 近面は z=0）
    inline Rectangle ProjectAABBToScreenRect(
        const AABB<float, Vec3f>& box,
        const Matrix<4, 4, float>& worldViewProj,
        float viewportWidth, float viewportHeight,
        float viewportX = 0.0f, float viewportY = 0.0f,
        float clampMargin = 0.0f                     // わずかに拡張したい場合（ピクセル）
    ) {
        Rectangle out{};

        // AABB 8頂点を生成（lb: lower bound, ub: upper bound）
        const Vec3f& L = box.lb;
        const Vec3f& U = box.ub;
        std::array<Vec3f, 8> corners = {
            Vec3f{L.x,L.y,L.z}, Vec3f{U.x,L.y,L.z}, Vec3f{L.x,U.y,L.z}, Vec3f{U.x,U.y,L.z},
            Vec3f{L.x,L.y,U.z}, Vec3f{U.x,L.y,U.z}, Vec3f{L.x,U.y,U.z}, Vec3f{U.x,U.y,U.z}
        };

        // クリップ空間へ
        std::array<Vec4f, 8> clip{};
        for (int i = 0; i < 8; ++i) clip[i] = ToClip(worldViewProj, corners[i]);

        // 6面の同側外側なら不可視
        if (TrivialRejectClip(clip)) {
            return out;
        }

        // 近面 z=0 をまたぐエッジの交点も候補点に追加（保守的）
        // 候補点は z>=0 かつ w>0 のみを使用
        // 8頂点 + 最大12交点 → 最大20点
        std::array<Vec4f, 32> candidates{};
        int n = 0;
        for (int i = 0; i < 8; ++i) {
            if (clip[i].z >= 0.0f && clip[i].w > 0.0f) {
                candidates[n++] = clip[i];
            }
        }
        for (const auto& e : kEdges) {
            Vec4f inter;
            if (IntersectEdgeWithNearZ0(clip[e[0]], clip[e[1]], inter)) {
                candidates[n++] = inter;
            }
        }

        if (n == 0) {
            // すべて近面の背後（z<0）→ 画面には映らない
            return out;
        }

        // NDC→スクリーンへ変換しながら min/max を構築
        float minx = std::numeric_limits<float>::infinity();
        float miny = std::numeric_limits<float>::infinity();
        float maxx = -std::numeric_limits<float>::infinity();
        float maxy = -std::numeric_limits<float>::infinity();

        for (int i = 0; i < n; ++i) {
            const Vec4f& c = candidates[i];
            if (c.w <= 0.0f) continue; // 念のため
            const float invw = 1.0f / c.w;
            const float ndcX = c.x * invw;                  // [-1,1]
            const float ndcY = c.y * invw;                  // [-1,1]
            // ビューポート変換 (D3D)
            const float sx = viewportX + (ndcX * 0.5f + 0.5f) * viewportWidth;
            const float sy = viewportY + (ndcY * 0.5f + 0.5f) * viewportHeight;
            minx = (std::min)(minx, sx);
            miny = (std::min)(miny, sy);
            maxx = (std::max)(maxx, sx);
            maxy = (std::max)(maxy, sy);
        }

        // ビューポート内にクランプ（保守的に少し広げたい場合は margin）
        minx = (std::max)(viewportX - clampMargin, minx);
        miny = (std::max)(viewportY - clampMargin, miny);
        maxx = (std::min)(viewportX + viewportWidth + clampMargin, maxx);
        maxy = (std::min)(viewportY + viewportHeight + clampMargin, maxy);

        // 面積ゼロでないか
        if (!(minx < maxx && miny < maxy)) {
            return out; // visible=false
        }

        out.x0 = minx; out.y0 = miny; out.x1 = maxx; out.y1 = maxy;
        out.visible = true;
        return out;
    }

} // namespace SectorFW::Math

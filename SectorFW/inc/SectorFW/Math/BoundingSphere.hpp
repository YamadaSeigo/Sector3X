/*****************************************************************//**
 * @file   BoundingSphere.hpp
 * @brief  点集合/メッシュからの境界球（Bounding Sphere）計算ユーティリティ
 * @author you
 * @date   2025-09
 *********************************************************************/

#pragma once
#include <vector>
#include <array>
#include <random>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdint>
#include <type_traits>

// 行列ユーティリティを使う
#include "Matrix.hpp"

namespace SFW {
    namespace Math {

        // ------------- ベクトルユーティリティ（x,y,zを持つ型ならOK） ----------------
        template<class Vec3>
        inline auto v3_add(const Vec3& a, const Vec3& b) noexcept {
            return Vec3{ a.x + b.x, a.y + b.y, a.z + b.z };
        }
        template<class Vec3, class T>
        inline auto v3_adds(const Vec3& a, T s) noexcept {
            return Vec3{ a.x + s, a.y + s, a.z + s };
        }
        template<class Vec3, class T>
        inline auto v3_muls(const Vec3& a, T s) noexcept {
            return Vec3{ a.x * s, a.y * s, a.z * s };
        }
        template<class Vec3>
        inline auto v3_sub(const Vec3& a, const Vec3& b) noexcept {
            return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
        }
        template<class Vec3>
        inline auto v3_dot(const Vec3& a, const Vec3& b) noexcept {
            using T = decltype(a.x + b.x);
            return T(a.x * b.x + a.y * b.y + a.z * b.z);
        }
        template<class Vec3>
        inline auto v3_len2(const Vec3& a) noexcept {
            return v3_dot(a, a);
        }
        template<class Vec3>
        inline auto v3_len(const Vec3& a) noexcept {
            using std::sqrt;
            return sqrt(v3_len2(a));
        }
        template<class Vec3>
        inline auto v3_mid(const Vec3& a, const Vec3& b) noexcept {
            return v3_muls(v3_add(a, b), typename std::remove_reference_t<decltype(a.x)>(0.5));
        }

        template<typename T, class Vec3>
        struct BoundingSphere {
            using value_type = T;
            Vec3 center{};
            T    radius{ T(0) };

            // ------------- 基本ユーティリティ -------------
            bool contains(const Vec3& p, T eps = T(0)) const noexcept {
                return v3_len2(v3_sub(p, center)) <= (radius + eps) * (radius + eps);
            }
            T distance2(const Vec3& p) const noexcept {
                return v3_len2(v3_sub(p, center));
            }

            // ------------- AABB から生成（厳密ではないが速い） -------------
            static BoundingSphere FromAABB(const Vec3& minP, const Vec3& maxP) noexcept {
                BoundingSphere s;
                s.center = v3_mid(minP, maxP);
                // 半径は対角線の半分
                s.radius = T(0.5) * v3_len(v3_sub(maxP, minP));
                return s;
            }

            // ------------- 2球の最小包含球（正確） -------------
            static BoundingSphere Merge(const BoundingSphere& a, const BoundingSphere& b) noexcept {
                // 片方がもう片方を包含する？
                Vec3 d = v3_sub(b.center, a.center);
                T dist = v3_len(d);
                if (a.radius >= b.radius + dist) return a;
                if (b.radius >= a.radius + dist) return b;
                if (dist <= std::numeric_limits<T>::epsilon()) {
                    // 同心円に近い
                    BoundingSphere s;
                    s.center = a.center;
                    s.radius = (std::max)(a.radius, b.radius);
                    return s;
                }
                T newR = (dist + a.radius + b.radius) * T(0.5);
                Vec3 dir = v3_muls(d, T(1) / dist);
                Vec3 newC = v3_add(a.center, v3_muls(dir, newR - a.radius));
                return { newC, newR };
            }

            // ------------- 逐次拡張（オンライン更新向け、最小ではない） -------------
            void expandToFit(const Vec3& p) noexcept {
                Vec3 diff = v3_sub(p, center);
                T d2 = v3_len2(diff);
                if (d2 <= radius * radius) return; // 既に包含
                T d = std::sqrt(d2);
                T newR = (radius + d) * T(0.5);
                // センターを外側へスライド
                if (d > T(0)) {
                    center = v3_add(center, v3_muls(diff, (newR - radius) / d));
                }
                radius = newR;
            }

            void expandToFit(const BoundingSphere& s) noexcept {
                *this = Merge(*this, s);
            }

            // ------------- Ritter 法（高速・近似、ほぼ十分に小さい） -------------
            // points: AoS 配列。Nが小さくてもOK。O(N)。
            static BoundingSphere FromPointsRitter(const Vec3* points, size_t count) {
                BoundingSphere s{};
                if (count == 0) return s;
                if (count == 1) { s.center = points[0]; s.radius = T(0); return s; }

                // 1) まず適当な点 p0 を選び、そこから最も遠い点 p1、さらに p1 から最も遠い点 p2 を探す
                size_t i0 = 0;
                auto farthest_from = [&](size_t idx)->size_t {
                    size_t best = 0;
                    auto c = points[idx];
                    auto bestD2 = v3_len2(v3_sub(points[0], c));
                    for (size_t i = 1; i < count; ++i) {
                        auto d2 = v3_len2(v3_sub(points[i], c));
                        if (d2 > bestD2) { bestD2 = d2; best = i; }
                    }
                    return best;
                    };
                size_t i1 = farthest_from(i0);
                size_t i2 = farthest_from(i1);

                // 2) p1 と p2 を直径とする球から開始
                Vec3 p1 = points[i1], p2 = points[i2];
                s.center = v3_mid(p1, p2);
                s.radius = T(0.5) * v3_len(v3_sub(p2, p1));

                // 3) すべての点を走査し、外側の点があれば最小拡張
                for (size_t i = 0; i < count; ++i) {
                    s.expandToFit(points[i]);
                }
                return s;
            }

            // ------------- Welzl 法（厳密最小球） -------------
            // 期待 O(N)（ランダムシャッフル前提）。N が数千～数十万でも現実的。
            static BoundingSphere FromPointsWelzl(std::vector<Vec3> pts, uint64_t seed = 0xC0FFEE) {
                BoundingSphere s{};
                const size_t n = pts.size();
                if (n == 0) return s;
                if (n == 1) { s.center = pts[0]; s.radius = T(0); return s; }

                // ランダム順序に
                std::mt19937_64 rng(seed);
                std::shuffle(pts.begin(), pts.end(), rng);

                // サポート集合 R は高々4点
                std::array<Vec3, 4> R{};
                size_t rsz = 0;

                auto ball_from_1 = [&](const Vec3& a) {
                    s.center = a; s.radius = T(0);
                    };
                auto ball_from_2 = [&](const Vec3& a, const Vec3& b) {
                    s.center = v3_mid(a, b);
                    s.radius = T(0.5) * v3_len(v3_sub(a, b));
                    };
                auto ball_from_3 = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
                    // 三点を通る円（3Dでは3点共面の外接円）→ その円の中心と半径（平面内）
                    // 3点がほぼ一直線なら2点からの球へフォールバック
                    Vec3 ab = v3_sub(b, a), ac = v3_sub(c, a);
                    auto abXac_x = ab.y * ac.z - ab.z * ac.y;
                    auto abXac_y = ab.z * ac.x - ab.x * ac.z;
                    auto abXac_z = ab.x * ac.y - ab.y * ac.x;
                    T denom = T(2) * (abXac_x * abXac_x + abXac_y * abXac_y + abXac_z * abXac_z);
                    const T eps = T(1e-12);
                    if (std::abs(denom) < eps) {
                        // 退化：2点の最大直径
                        BoundingSphere s2; s2.radius = T(-1);
                        auto try2 = [&](const Vec3& p, const Vec3& q) {
                            BoundingSphere t; ball_from_2(p, q);
                            if (t.radius > s2.radius) s2 = t;
                            };
                        try2(a, b); try2(a, c); try2(b, c);
                        s = s2;
                        return;
                    }
                    auto ab2 = v3_len2(ab);
                    auto ac2 = v3_len2(ac);
                    Vec3 num = {
                        (ab2 * ac.y - ac2 * ab.y) * abXac_z - (ab2 * ac.z - ac2 * ab.z) * abXac_y,
                        (ab2 * ac.z - ac2 * ab.z) * abXac_x - (ab2 * ac.x - ac2 * ab.x) * abXac_z,
                        (ab2 * ac.x - ac2 * ab.x) * abXac_y - (ab2 * ac.y - ac2 * ab.y) * abXac_x
                    };
                    s.center = v3_add(a, v3_muls(num, T(1) / denom));
                    s.radius = v3_len(v3_sub(s.center, a));
                    };
                auto ball_from_4 = [&](const Vec3& p, const Vec3& q, const Vec3& r, const Vec3& t) {
                    // 4点の外接球。行列式ベースで求める（数値的にはやや繊細）。
                    // 退化（ほぼ共面/共直線）の場合は3点/2点にフォールバック。
                    auto det4 = [](T a11, T a12, T a13, T a14,
                        T a21, T a22, T a23, T a24,
                        T a31, T a32, T a33, T a34,
                        T a41, T a42, T a43, T a44)->T {
                            // ラプラス展開の手書き版（小規模なのでOK）
                            T m11 = a22 * (a33 * a44 - a34 * a43) - a23 * (a32 * a44 - a34 * a42) + a24 * (a32 * a43 - a33 * a42);
                            T m12 = a21 * (a33 * a44 - a34 * a43) - a23 * (a31 * a44 - a34 * a41) + a24 * (a31 * a43 - a33 * a41);
                            T m13 = a21 * (a32 * a44 - a34 * a42) - a22 * (a31 * a44 - a34 * a41) + a24 * (a31 * a42 - a32 * a41);
                            T m14 = a21 * (a32 * a43 - a33 * a42) - a22 * (a31 * a43 - a33 * a41) + a23 * (a31 * a42 - a32 * a41);
                            return a11 * m11 - a12 * m12 + a13 * m13 - a14 * m14;
                        };
                    auto S = [&](const Vec3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; };
                    T a = det4(p.x, p.y, p.z, T(1),
                        q.x, q.y, q.z, T(1),
                        r.x, r.y, r.z, T(1),
                        t.x, t.y, t.z, T(1));
                    const T eps = T(1e-12);
                    if (std::abs(a) < eps) {
                        // 退化：3点/2点からの最大にフォールバック
                        BoundingSphere best; best.radius = T(-1);
                        auto try3 = [&](const Vec3& A, const Vec3& B, const Vec3& C) {
                            BoundingSphere tmp; ball_from_3(A, B, C);
                            if (tmp.radius > best.radius) best = tmp;
                            };
                        auto try2 = [&](const Vec3& A, const Vec3& B) {
                            BoundingSphere tmp; ball_from_2(A, B);
                            if (tmp.radius > best.radius) best = tmp;
                            };
                        try3(p, q, r); try3(p, q, t); try3(p, r, t); try3(q, r, t);
                        try2(p, q); try2(p, r); try2(p, t); try2(q, r); try2(q, t); try2(r, t);
                        s = best;
                        return;
                    }
                    T dx = det4(S(p), p.y, p.z, T(1),
                        S(q), q.y, q.z, T(1),
                        S(r), r.y, r.z, T(1),
                        S(t), t.y, t.z, T(1));
                    T dy = det4(p.x, S(p), p.z, T(1),
                        q.x, S(q), q.z, T(1),
                        r.x, S(r), r.z, T(1),
                        t.x, S(t), t.z, T(1));
                    T dz = det4(p.x, p.y, S(p), T(1),
                        q.x, q.y, S(q), T(1),
                        r.x, r.y, S(r), T(1),
                        t.x, t.y, S(t), T(1));
                    T c = det4(p.x, p.y, p.z, S(p),
                        q.x, q.y, q.z, S(q),
                        r.x, r.y, r.z, S(r),
                        t.x, t.y, t.z, S(t));
                    s.center = Vec3{ dx / (T(2) * a), dy / (T(2) * a), dz / (T(2) * a) };
                    s.radius = std::sqrt((dx * dx + dy * dy + dz * dz) / (T(4) * a * a) - c / a);
                    };

                auto make_ball_from_R = [&](const std::array<Vec3, 4>& R, size_t rsz) {
                    switch (rsz) {
                    case 1: ball_from_1(R[0]); break;
                    case 2: ball_from_2(R[0], R[1]); break;
                    case 3: ball_from_3(R[0], R[1], R[2]); break;
                    case 4: ball_from_4(R[0], R[1], R[2], R[3]); break;
                    default: s.center = Vec3{ T(0),T(0),T(0) }; s.radius = T(0); break;
                    }
                    };

                // 非再帰版 Welzl：R を拡張しながらループ
                s.center = pts[0]; s.radius = T(0);
                for (size_t i = 0; i < n; ++i) {
                    if (s.contains(pts[i])) continue;
                    // pts[i] を必ず境界に載せる最小球を作る
                    R[0] = pts[i]; rsz = 1; make_ball_from_R(R, rsz);
                    for (size_t j = 0; j < i; ++j) {
                        if (s.contains(pts[j])) continue;
                        R[1] = pts[j]; rsz = 2; make_ball_from_R(R, rsz);
                        for (size_t k = 0; k < j; ++k) {
                            if (s.contains(pts[k])) continue;
                            R[2] = pts[k]; rsz = 3; make_ball_from_R(R, rsz);
                            for (size_t m = 0; m < k; ++m) {
                                if (s.contains(pts[m])) continue;
                                R[3] = pts[m]; rsz = 4; make_ball_from_R(R, rsz);
                            }
                        }
                    }
                }
                return s;
            }

            // ------------- 汎用トランスフォーム -------------
            // 一様スケール + 並進だけなら正確。非一様スケール/回転を含む場合は
            // 「線形部の最大伸長率（列ベクトル長の最大）」で半径を保守的に拡大。
            template<class Mat4, class Vec3Like>
            static BoundingSphere Transform(const BoundingSphere& s, const Mat4& M,
                const Vec3Like& col0, const Vec3Like& col1, const Vec3Like& col2,
                const Vec3Like& translation) {
                // center' = R*center + t,  radius' = radius * max( ||col0||, ||col1||, ||col2|| )
                Vec3 newC{
                    M(0,0) * s.center.x + M(0,1) * s.center.y + M(0,2) * s.center.z + M(0,3),
                    M(1,0) * s.center.x + M(1,1) * s.center.y + M(1,2) * s.center.z + M(1,3),
                    M(2,0) * s.center.x + M(2,1) * s.center.y + M(2,2) * s.center.z + M(2,3)
                };
                auto len0 = std::sqrt(col0.x * col0.x + col0.y * col0.y + col0.z * col0.z);
                auto len1 = std::sqrt(col1.x * col1.x + col1.y * col1.y + col1.z * col1.z);
                auto len2 = std::sqrt(col2.x * col2.x + col2.y * col2.y + col2.z * col2.z);
                T scale = (T)(std::max)({ len0,len1,len2 });
                return { newC, s.radius * scale };
            }

            // 一様スケール s と並進 t のみ既知な場合
            template<class Vec3LikeT>
            static BoundingSphere TransformUniform(const BoundingSphere& bs, const Vec3LikeT& translation, T uniform_scale) {
                return { v3_add(bs.center, translation), bs.radius * std::abs(uniform_scale) };
            }

            //----------------------------------------------
            // WVP (= proj * view * world) だけでの可視判定
            // ・列ベクトル規約: clip = WVP * [x,y,z,1]^T
            // ・LH × ZeroToOne を想定（x,y ∈ [-w,w], z ∈ [0,w], w>0）
            // ・中心と局所軸 ±R を投影して NDC 半径を近似（保守的）
            // ・outWmin: clip-space の最小W（MOC TestRect 用）
            //----------------------------------------------
            template<class Mat4>
            bool IsVisible_WVP(const Mat4& WVP,
                T* outNdcXmin = nullptr, T* outNdcYmin = nullptr,
                T* outNdcXmax = nullptr, T* outNdcYmax = nullptr,
                T* outWmin = nullptr, T* depth = nullptr) const noexcept
            {
                using ::SFW::Math::MulPoint_RowMajor_ColVec;

                // 1) 中心を clip に
                T cx, cy, cz, cw;
                MulPoint_RowMajor_ColVec(WVP, center.x, center.y, center.z, cx, cy, cz, cw);

                // 2) 局所軸 ±R を 3方向サンプリング（+X, +Y, +Z）
                auto proj_pt = [&](T ox, T oy, T oz,
                    T& x, T& y, T& z, T& w) {
                        MulPoint_RowMajor_ColVec(WVP, center.x + ox, center.y + oy, center.z + oz, x, y, z, w);
                    };

                T pxx, pxy, pxz, pxw;
                T pyx, pyy, pyz, pyw;
                T pzx, pzy, pzz, pzw;
                proj_pt(+radius, 0.0f, 0.0f, pxx, pxy, pxz, pxw); // +X
                proj_pt(0.0f, +radius, 0.0f, pyx, pyy, pyz, pyw); // +Y
                proj_pt(0.0f, 0.0f, +radius, pzx, pzy, pzz, pzw); // +Z（z評価にも使用）

                // 3) NDC へ（同次除算）
                auto safe_div = [](T a, T b) {
                    const T eps = 1e-6f;
                    return a / ((std::fabs(b) < eps) ? (b < 0 ? -eps : eps) : b);
                    };

                const T ndc_cx = safe_div(cx, cw);
                const T ndc_cy = safe_div(cy, cw);
                const T ndc_cz = safe_div(cz, cw);   // ZeroToOne なら [0,1] が可視

                const T ndc_pxx = safe_div(pxx, pxw);
                const T ndc_pxy = safe_div(pxy, pxw);

                const T ndc_pyx = safe_div(pyx, pyw);
                const T ndc_pyy = safe_div(pyy, pyw);

                const T ndc_pzz = safe_div(pzz, pzw); // z方向の奥/手前評価に利用

                // 4) 画面半径（保守的近似）
                const T r_ndc_x = std::fabs(ndc_pxx - ndc_cx);
                const T r_ndc_y = std::fabs(ndc_pyy - ndc_cy);
                const T r_ndc = (std::max)(r_ndc_x, r_ndc_y);

                // 5) NDC 矩形
                T xmin = ndc_cx - r_ndc;
                T xmax = ndc_cx + r_ndc;
                T ymin = ndc_cy - r_ndc;
                T ymax = ndc_cy + r_ndc;

                // 6) クリップとの交差（x,y は [-1,1]、z は [0,1]）
                const T zmin_est = (std::min)(ndc_cz, ndc_pzz);
                const T zmax_est = (std::max)(ndc_cz, ndc_pzz);

                const bool x_overlap = !(xmax < -1.0f || xmin > 1.0f);
                const bool y_overlap = !(ymax < -1.0f || ymin > 1.0f);
                const bool z_overlap = !(zmax_est < 0.0f || zmin_est > 1.0f);

                // 出力
                if (outNdcXmin) *outNdcXmin = xmin;
                if (outNdcXmax) *outNdcXmax = xmax;
                if (outNdcYmin) *outNdcYmin = ymin;
                if (outNdcYmax) *outNdcYmax = ymax;
                if (depth)      *depth = cw;  // 手前側の深度の代表として中心の W を返す

                // wmin（clip-space 最小W）— 保守的に中心/+X/+Y/+Z の最小
                if (outWmin) {
                    const T raw_minw = (std::min)((std::min)(cw, pxw), (std::min)(pyw, pzw));
                    const T epsW = 1e-6f;                  // 数値安定用
                    *outWmin = (raw_minw < epsW) ? epsW : raw_minw;
                }

                return x_overlap && y_overlap && z_overlap;
            }

            template<class Mat4, class NDC>
            bool IsVisible_WVP(const Mat4& WVP, NDC* outNDC, T* depth = nullptr) const noexcept
            {
                using ::SFW::Math::MulPoint_RowMajor_ColVec;

                // 1) 中心を clip に
                T cx, cy, cz, cw;
                MulPoint_RowMajor_ColVec(WVP, center.x, center.y, center.z, cx, cy, cz, cw);

                // 2) 局所軸 ±R を 3方向サンプリング（+X, +Y, +Z）
                auto proj_pt = [&](T ox, T oy, T oz,
                    T& x, T& y, T& z, T& w) {
                        MulPoint_RowMajor_ColVec(WVP, center.x + ox, center.y + oy, center.z + oz, x, y, z, w);
                    };

                T pxx, pxy, pxz, pxw;
                T pyx, pyy, pyz, pyw;
                T pzx, pzy, pzz, pzw;
                proj_pt(+radius, 0.0f, 0.0f, pxx, pxy, pxz, pxw); // +X
                proj_pt(0.0f, +radius, 0.0f, pyx, pyy, pyz, pyw); // +Y
                proj_pt(0.0f, 0.0f, +radius, pzx, pzy, pzz, pzw); // +Z（z評価にも使用）

                // 3) NDC へ（同次除算）
                auto safe_div = [](T a, T b) {
                    const T eps = 1e-6f;
                    return a / ((std::fabs(b) < eps) ? (b < 0 ? -eps : eps) : b);
                    };

                const T ndc_cx = safe_div(cx, cw);
                const T ndc_cy = safe_div(cy, cw);
                const T ndc_cz = safe_div(cz, cw);   // ZeroToOne なら [0,1] が可視

                const T ndc_pxx = safe_div(pxx, pxw);
                const T ndc_pxy = safe_div(pxy, pxw);

                const T ndc_pyx = safe_div(pyx, pyw);
                const T ndc_pyy = safe_div(pyy, pyw);

                const T ndc_pzz = safe_div(pzz, pzw); // z方向の奥/手前評価に利用

                // 4) 画面半径（保守的近似）
                const T r_ndc_x = std::fabs(ndc_pxx - ndc_cx);
                const T r_ndc_y = std::fabs(ndc_pyy - ndc_cy);
                const T r_ndc = (std::max)(r_ndc_x, r_ndc_y);

                // 5) NDC 矩形
                T xmin = ndc_cx - r_ndc;
                T xmax = ndc_cx + r_ndc;
                T ymin = ndc_cy - r_ndc;
                T ymax = ndc_cy + r_ndc;

                // 6) クリップとの交差（x,y は [-1,1]、z は [0,1]）
                const T zmin_est = (std::min)(ndc_cz, ndc_pzz);
                const T zmax_est = (std::max)(ndc_cz, ndc_pzz);

                const bool x_overlap = !(xmax < -1.0f || xmin > 1.0f);
                const bool y_overlap = !(ymax < -1.0f || ymin > 1.0f);
                const bool z_overlap = !(zmax_est < 0.0f || zmin_est > 1.0f);

                // 出力
                if (outNDC) {
                    outNDC->xmin = xmin;
					outNDC->xmax = xmax;
					outNDC->ymin = ymin;
					outNDC->ymax = ymax;
                    const T raw_minw = (std::min)((std::min)(cw, pxw), (std::min)(pyw, pzw));
                    const T epsW = 1e-6f;                  // 数値安定用
					outNDC->wmin = (raw_minw < epsW) ? epsW : raw_minw;
                }

                if (depth)      *depth = cw;  // 手前側の深度の代表として中心の W を返す

                return x_overlap && y_overlap && z_overlap;
            }


            // center: 球中心（ワールド）/ radius: 半径
            // camRight, camUp, camForward: カメラのワールド空間基底（正規直交・単位長を想定）
            //
            // 可視なら true。outNDC は NDC の AABB（x,y は [-1,1]、z は [0,1] で交差判定）
            // depth には「中心の clip.w」を返します（既存コード互換）。
            template<class Mat4, class NDC>
            bool IsVisible_WVP_CamBasis(
                const Mat4& WVP,
                const Vec3& camRight, const Vec3& camUp, const Vec3& camForward,
                NDC* outNDC,
                T* depth = nullptr, Vec3* outWP = nullptr) const noexcept
            {
                using ::SFW::Math::MulPoint_RowMajor_ColVec;

                auto safe_div = [](T a, T b) {
                    const T eps = 1e-6f;
                    return a / ((std::fabs(b) < eps) ? (b < 0 ? -eps : eps) : b);
                    };

                // 1) 中心を clip に
                T cx, cy, cz, cw;
                MulPoint_RowMajor_ColVec(WVP, center.x, center.y, center.z, cx, cy, cz, cw);

                // 2) 基底方向に ±radius だけずらした点を投影
                auto proj_pt = [&](const Vec3& p, T& x, T& y, T& z, T& w) {
                    MulPoint_RowMajor_ColVec(WVP, p.x, p.y, p.z, x, y, z, w);
                    };

                const Vec3 Prp = { center.x + camRight.x * radius,  center.y + camRight.y * radius,  center.z + camRight.z * radius };
                const Vec3 Prm = { center.x - camRight.x * radius,  center.y - camRight.y * radius,  center.z - camRight.z * radius };
                const Vec3 Pup = { center.x + camUp.x * radius,  center.y + camUp.y * radius,  center.z + camUp.z * radius };
                const Vec3 Pum = { center.x - camUp.x * radius,  center.y - camUp.y * radius,  center.z - camUp.z * radius };
                const Vec3 Pfp = { center.x + camForward.x * radius,center.y + camForward.y * radius,center.z + camForward.z * radius };
                const Vec3 Pfm = { center.x - camForward.x * radius,center.y - camForward.y * radius,center.z - camForward.z * radius };

                T rpx, rpy, rpz, rpw; proj_pt(Prp, rpx, rpy, rpz, rpw); // +Right
                T rm_x, rm_y, rm_z, rm_w; proj_pt(Prm, rm_x, rm_y, rm_z, rm_w); // -Right
                T upx, upy, upz, upw; proj_pt(Pup, upx, upy, upz, upw); // +Up
                T umx, umy, umz, umw; proj_pt(Pum, umx, umy, umz, umw); // -Up
                T fpx, fpy, fpz, fpw; proj_pt(Pfp, fpx, fpy, fpz, fpw); // +Forward（奥）
                T fmx, fmy, fmz, fmw; proj_pt(Pfm, fmx, fmy, fmz, fmw); // -Forward（手前）

                // 3) NDC へ（同次除算）
                const T ndc_cx = safe_div(cx, cw);
                const T ndc_cy = safe_div(cy, cw);
                const T ndc_cz = safe_div(cz, cw);

                const T ndc_rp_x = safe_div(rpx, rpw);
                const T ndc_rm_x = safe_div(rm_x, rm_w);
                const T ndc_up_y = safe_div(upy, upw);
                const T ndc_um_y = safe_div(umy, umw);

                const T ndc_fp_z = safe_div(fpz, fpw);
                const T ndc_fm_z = safe_div(fmz, fmw);

                // 4) スクリーン半径（保守的）：左右は Right±、上下は Up± の最大差分
                const T r_ndc_x = (std::max)(std::fabs(ndc_rp_x - ndc_cx),
                    std::fabs(ndc_rm_x - ndc_cx));
                const T r_ndc_y = (std::max)(std::fabs(ndc_up_y - ndc_cy),
                    std::fabs(ndc_um_y - ndc_cy));
                const T r_ndc = (std::max)(r_ndc_x, r_ndc_y);

                // 5) NDC 矩形
                T xmin = ndc_cx - r_ndc;
                T xmax = ndc_cx + r_ndc;
                T ymin = ndc_cy - r_ndc;
                T ymax = ndc_cy + r_ndc;

                // 6) z 範囲（保守的）：中心と Forward± の 3 サンプル
                const T zmin_est = (std::min)((std::min)(ndc_cz, ndc_fm_z), ndc_fp_z);
                const T zmax_est = (std::max)((std::max)(ndc_cz, ndc_fm_z), ndc_fp_z);

                // 7) クリップとの交差（x,y は [-1,1]、z は [0,1]）
                const bool x_overlap = !(xmax < -1.0f || xmin > 1.0f);
                const bool y_overlap = !(ymax < -1.0f || ymin > 1.0f);
                const bool z_overlap = !(zmax_est < 0.0f || zmin_est > 1.0f);

                // 8) 出力
                if (outNDC) {
                    outNDC->xmin = xmin; outNDC->xmax = xmax;
                    outNDC->ymin = ymin; outNDC->ymax = ymax;

                    const T raw_minw = (std::min)(
                        (std::min)((std::min)(cw, rpw), (std::min)(rm_w, upw)),
                        (std::min)(umw, (std::min)(fpw, fmw)));
                        const T epsW = 1e-6f;
                        outNDC->wmin = (raw_minw < epsW) ? epsW : raw_minw;
                }
                if (depth) *depth = cw; // 互換用

                if (outWP) {
					*outWP = { cx, cy, cz };
                }

                return x_overlap && y_overlap && z_overlap;
            }

            template<class Mat4, class NDC>
            bool IsVisible_WVP_CamBasis_Fast(
                const Mat4& WVP,
                const Vec3& camRight, const Vec3& camUp, const Vec3& camForward,
                NDC* outNDC, T* depth = nullptr, Vec3* outWP = nullptr) const noexcept
            {
                using ::SFW::Math::MulPoint_RowMajor_ColVec;

                // 1) 中心を clip に
                T cx, cy, cz, cw;
                MulPoint_RowMajor_ColVec(WVP, center.x, center.y, center.z, cx, cy, cz, cw);

                // 2) WVP の線形部分を抽出
                const T m00 = WVP.m00, m01 = WVP.m01, m02 = WVP.m02;
                const T m10 = WVP.m10, m11 = WVP.m11, m12 = WVP.m12;
                const T m20 = WVP.m20, m21 = WVP.m21, m22 = WVP.m22;
                const T m30 = WVP.m30, m31 = WVP.m31, m32 = WVP.m32; // w行成分も必要

                // 3) 半径方向を clip空間へ変換（ベクトル変換：平行移動は含まない）
                auto to_clip = [&](const Vec3& v)->Vec3 {
                    return {
                        m00 * v.x + m01 * v.y + m02 * v.z,
                        m10 * v.x + m11 * v.y + m12 * v.z,
                        m20 * v.x + m21 * v.y + m22 * v.z
                    };
                    };
                auto to_clip_w = [&](const Vec3& v)->T {
                    return m30 * v.x + m31 * v.y + m32 * v.z;
                    };

                const Vec3 clipR = to_clip(camRight);
                const Vec3 clipU = to_clip(camUp);
                const Vec3 clipF = to_clip(camForward);
                const T wR = to_clip_w(camRight);
                const T wU = to_clip_w(camUp);
                const T wF = to_clip_w(camForward);

                // 4) clip 半径（+R, +U の大きい方）
                auto safe_div = [](T a, T b) {
                    const T eps = 1e-6f;
                    return a / ((std::fabs(b) < eps) ? (b < 0 ? -eps : eps) : b);
                    };

                const T invCW = 1.0f / ((std::fabs(cw) < 1e-6f) ? ((cw < 0) ? -1e-6f : 1e-6f) : cw);

                const T ndc_cx = cx * invCW;
                const T ndc_cy = cy * invCW;
                const T ndc_cz = cz * invCW;

                // 5) NDC半径計算（近似：微分ベース）
                //   d(x/w) ≈ (dx*w - x*dw)/w²
                const T dxR = clipR.x * cw - cx * wR;
                const T dyU = clipU.y * cw - cy * wU;
                const T dzF = clipF.z * cw - cz * wF;
                const T invCW2 = invCW * invCW;
                const T r_ndc_x = std::fabs(radius * dxR * invCW2);
                const T r_ndc_y = std::fabs(radius * dyU * invCW2);
                const T r_ndc = (std::max)(r_ndc_x, r_ndc_y);

                // 6) z 範囲（前後 ±radius * Forward）
                const T r_ndc_z = std::fabs(radius * dzF * invCW2);
                const T zmin_est = ndc_cz - r_ndc_z;
                const T zmax_est = ndc_cz + r_ndc_z;

                // 7) 矩形・交差判定
                const T xmin = ndc_cx - r_ndc;
                const T xmax = ndc_cx + r_ndc;
                const T ymin = ndc_cy - r_ndc;
                const T ymax = ndc_cy + r_ndc;

                const bool x_overlap = !(xmax < -1.0f || xmin > 1.0f);
                const bool y_overlap = !(ymax < -1.0f || ymin > 1.0f);
                const bool z_overlap = !(zmax_est < 0.0f || zmin_est > 1.0f);

                if (outNDC) {
                    outNDC->xmin = xmin; outNDC->xmax = xmax;
                    outNDC->ymin = ymin; outNDC->ymax = ymax;
                    outNDC->wmin = cw;
                }
                if (depth) *depth = cw;

                if (outWP) {
                    *outWP = { cx, cy, cz };
                }

                return x_overlap && y_overlap && z_overlap;
            }

            template<class NDC>
            bool IsVisible_WVP_CamBasis_ExactFast(
                const Matrix4x4f& WVP,
                const Vec3& camRight, const Vec3& camUp, const Vec3& camForward,
                NDC* outNDC, float* depth = nullptr, Vec3* outWP = nullptr) const noexcept
            {
                using ::SFW::Math::MulPoint_RowMajor_ColVec;

                // 1) center を clip に
                float cx, cy, cz, cw;
                MulPoint_RowMajor_ColVec(WVP, center.x, center.y, center.z, cx, cy, cz, cw);

                // 2) WVP の線形部分を「方向ベクトル用」に使う
                auto dir_to_clip = [&](const Vec3& v,
                    float& dx, float& dy, float& dz, float& dw) {
                        dx = WVP.m00 * v.x + WVP.m01 * v.y + WVP.m02 * v.z;
                        dy = WVP.m10 * v.x + WVP.m11 * v.y + WVP.m12 * v.z;
                        dz = WVP.m20 * v.x + WVP.m21 * v.y + WVP.m22 * v.z;
                        dw = WVP.m30 * v.x + WVP.m31 * v.y + WVP.m32 * v.z;
                    };

                float dRx, dRy, dRz, dRw; dir_to_clip(camRight, dRx, dRy, dRz, dRw);
                float dUx, dUy, dUz, dUw; dir_to_clip(camUp, dUx, dUy, dUz, dUw);
                float dFx, dFy, dFz, dFw; dir_to_clip(camForward, dFx, dFy, dFz, dFw);

                auto safe_div = [](float a, float b) {
                    const float eps = 1e-6f;
                    return a / ((std::fabs(b) < eps) ? (b < 0 ? -eps : eps) : b);
                    };

                // 3) center の NDC
                const float ndc_cx = safe_div(cx, cw);
                const float ndc_cy = safe_div(cy, cw);
                const float ndc_cz = safe_div(cz, cw);

                // 4) ±Right / ±Up / ±Forward を「中心＋方向×radius」で生成して NDC へ
                auto ndc_from_clip = [&](float x, float y, float z, float w,
                    float& ox, float& oy, float& oz) {
                        ox = safe_div(x, w);
                        oy = safe_div(y, w);
                        oz = safe_div(z, w);
                    };

                // Right
                float rpx, rpy, rpz, rpw;
                float rmx, rmy, rmz, rmw;
                rpx = cx + radius * dRx; rpy = cy + radius * dRy; rpz = cz + radius * dRz; rpw = cw + radius * dRw;
                rmx = cx - radius * dRx; rmy = cy - radius * dRy; rmz = cz - radius * dRz; rmw = cw - radius * dRw;

                float ndc_rp_x, ndc_rp_y, ndc_rp_z;
                float ndc_rm_x, ndc_rm_y, ndc_rm_z;
                ndc_from_clip(rpx, rpy, rpz, rpw, ndc_rp_x, ndc_rp_y, ndc_rp_z);
                ndc_from_clip(rmx, rmy, rmz, rmw, ndc_rm_x, ndc_rm_y, ndc_rm_z);

                // Up
                float upx, upy, upz, upw;
                float umx, umy, umz, umw;
                upx = cx + radius * dUx; upy = cy + radius * dUy; upz = cz + radius * dUz; upw = cw + radius * dUw;
                umx = cx - radius * dUx; umy = cy - radius * dUy; umz = cz - radius * dUz; umw = cw - radius * dUw;

                float ndc_up_y, ndc_um_y, dummy;
                ndc_from_clip(upx, upy, upz, upw, dummy, ndc_up_y, dummy);
                ndc_from_clip(umx, umy, umz, umw, dummy, ndc_um_y, dummy);

                // Forward
                float fpx, fpy, fpz, fpw;
                float fmx, fmy, fmz, fmw;
                fpx = cx + radius * dFx; fpy = cy + radius * dFy; fpz = cz + radius * dFz; fpw = cw + radius * dFw;
                fmx = cx - radius * dFx; fmy = cy - radius * dFy; fmz = cz - radius * dFz; fmw = cw - radius * dFw;

                float ndc_fp_z, ndc_fm_z;
                ndc_from_clip(fpx, fpy, fpz, fpw, dummy, dummy, ndc_fp_z);
                ndc_from_clip(fmx, fmy, fmz, fmw, dummy, dummy, ndc_fm_z);

                // 5) スクリーン半径
                const float r_ndc_x = (std::max)(std::fabs(ndc_rp_x - ndc_cx),
                    std::fabs(ndc_rm_x - ndc_cx));
                const float r_ndc_y = (std::max)(std::fabs(ndc_up_y - ndc_cy),
                    std::fabs(ndc_um_y - ndc_cy));
                const float r_ndc = (std::max)(r_ndc_x, r_ndc_y);

                const float xmin = ndc_cx - r_ndc;
                const float xmax = ndc_cx + r_ndc;
                const float ymin = ndc_cy - r_ndc;
                const float ymax = ndc_cy + r_ndc;

                // 6) z 範囲（center と ±Forward）
                const float zmin_est = (std::min)((std::min)(ndc_cz, ndc_fm_z), ndc_fp_z);
                const float zmax_est = (std::max)((std::max)(ndc_cz, ndc_fm_z), ndc_fp_z);

                const bool x_overlap = !(xmax < -1.0f || xmin > 1.0f);
                const bool y_overlap = !(ymax < -1.0f || ymin > 1.0f);
                const bool z_overlap = !(zmax_est < 0.0f || zmin_est > 1.0f);

                if (outNDC) {
                    outNDC->xmin = xmin; outNDC->xmax = xmax;
                    outNDC->ymin = ymin; outNDC->ymax = ymax;

                    const float raw_minw = (std::min)(
                        (std::min)((std::min)(cw, rpw), (std::min)(rmw, upw)),
                        (std::min)(umw, (std::min)(fpw, fmw)));
                    const float epsW = 1e-6f;
                    outNDC->wmin = (raw_minw < epsW) ? epsW : raw_minw;
                }
                if (depth) *depth = cw;

                if (outWP) {
					*outWP = { cx, cy, cz };
                }

                return x_overlap && y_overlap && z_overlap;
            }


        };

        using BoundingSpheref = BoundingSphere<float, Vec3f>;

    } // namespace Math
} // namespace SectorFW

/* 使い方（例）:

// 1) 近似（高速）: Ritter
auto sphereR = BoundingSphere<float, Vec3f>::FromPointsRitter(points.data(), points.size());

// 2) 厳密最小球: Welzl
auto sphereW = BoundingSphere<float, Vec3f>::FromPointsWelzl(std::vector<Vec3f>(points.begin(), points.end()));

// 3) AABB から
auto sphereAABB = BoundingSphere<float, Vec3f>::FromAABB(aabb.lb, aabb.ub);

// 4) 2球マージ
auto merged = BoundingSphere<float, Vec3f>::Merge(sphereR, sphereW);

// 5) 逐次拡張
BoundingSphere<float, Vec3f> s{center0, r0};
for (auto& p : stream) s.expandToFit(p);

*/

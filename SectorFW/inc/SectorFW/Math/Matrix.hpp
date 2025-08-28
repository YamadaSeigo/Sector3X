/*****************************************************************//**
 * @file   Matrix.hpp
 * @brief 行列を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

#if defined(_MSC_VER)
#include <immintrin.h> // SSE2/AVX/FMA (MSVC x64 ではSSE2は常に有効)
#elif defined(__SSE2__)
#include <immintrin.h>
#endif

#include "Vector.hpp"
#include "Quaternion.hpp"

namespace SectorFW
{
	namespace Math
	{
        //==============================
// 先行宣言: 乗算カーネル
//==============================
        template<size_t R, size_t K, size_t C, typename T>
        struct MatMulKernel;

        //==============================
        // 行列 (row-major 保存)
        //==============================
        template <size_t Rows, size_t Cols, typename T>
        struct Matrix {
            static constexpr size_t kRows = Rows;
            static constexpr size_t kCols = Cols;

            std::array<std::array<T, Cols>, Rows> m{};

            static Matrix Identity() noexcept {
                static_assert(Rows == Cols, "Identity matrix must be square.");
                Matrix I{};
                for (size_t i = 0; i < Rows; ++i) I.m[i][i] = T(1);
                return I;
            }

            // 添字
            std::array<T, Cols>& operator[](size_t r) noexcept { return m[r]; }
            const std::array<T, Cols>& operator[](size_t r) const noexcept { return m[r]; }

            // 連続ポインタ（SIMDヘルパー用）
            T* data()       noexcept { return &m[0][0]; }
            const T* data() const noexcept { return &m[0][0]; }
            const T* ToPointer() const noexcept { return &m[0][0]; } // T* で安全

            // 行列積 (ディスパッチ)
            template <size_t OtherCols>
            Matrix<Rows, OtherCols, T>
                operator*(const Matrix<Cols, OtherCols, T>& rhs) const noexcept {
                return MatMulKernel<Rows, Cols, OtherCols, T>::eval(*this, rhs);
            }
        };

        // 型エイリアス
        using Matrix4x4f = Matrix<4, 4, float>;
        using Matrix4x4d = Matrix<4, 4, double>;

        //==============================
        // スカラー汎用カーネル
        //==============================
        template<size_t R, size_t K, size_t C, typename T>
        struct MatMulKernel {
            static Matrix<R, C, T> eval(const Matrix<R, K, T>& A, const Matrix<K, C, T>& B) noexcept {
                Matrix<R, C, T> out{};
                for (size_t i = 0; i < R; ++i) {
                    for (size_t j = 0; j < C; ++j) {
                        T s = T(0);
                        for (size_t k = 0; k < K; ++k) s += A.m[i][k] * B.m[k][j];
                        out.m[i][j] = s;
                    }
                }
                return out;
            }
        };

        //==============================
        // 4x4 float SIMD 特化 (SSE2+)
        //==============================
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
        namespace detail {
            static inline __m128 fmadd_ps(__m128 a, __m128 b, __m128 c) noexcept {
#if defined(__FMA__) || (defined(_MSC_VER) && !defined(_M_IX86) /* x64 */)
                // MSVC x64 では /arch:AVX2 + FMA 有効時に _mm_fmadd_ps 使用可
#if defined(__FMA__)
                return _mm_fmadd_ps(a, b, c);
#else
    // FMA命令が使えない場合は MUL+ADD
                return _mm_add_ps(_mm_mul_ps(a, b), c);
#endif
#else
                return _mm_add_ps(_mm_mul_ps(a, b), c);
#endif
            }
        }

        // C = A(4x4) * B(4x4)  row-major, 列ベクトル規約
        template<>
        struct MatMulKernel<4, 4, 4, float> {
            static Matrix4x4f eval(const Matrix4x4f& A, const Matrix4x4f& B) noexcept {
                Matrix4x4f C{};

                // B の 4 行をベクトルでロード（各行が連続）
                const __m128 b0 = _mm_loadu_ps(B.m[0].data());
                const __m128 b1 = _mm_loadu_ps(B.m[1].data());
                const __m128 b2 = _mm_loadu_ps(B.m[2].data());
                const __m128 b3 = _mm_loadu_ps(B.m[3].data());

                for (int i = 0; i < 4; ++i) {
                    const __m128 a0 = _mm_set1_ps(A.m[i][0]);
                    const __m128 a1 = _mm_set1_ps(A.m[i][1]);
                    const __m128 a2 = _mm_set1_ps(A.m[i][2]);
                    const __m128 a3 = _mm_set1_ps(A.m[i][3]);

                    __m128 r = _mm_mul_ps(a0, b0);
                    r = detail::fmadd_ps(a1, b1, r);
                    r = detail::fmadd_ps(a2, b2, r);
                    r = detail::fmadd_ps(a3, b3, r);

                    _mm_storeu_ps(C.m[i].data(), r);
                }
                return C;
            }
        };
#endif

        //==============================
        // 転置
        //==============================
        template <size_t R, size_t C, typename T>
        Matrix<C, R, T> TransposeMatrix(const Matrix<R, C, T>& M) noexcept {
            Matrix<C, R, T> Rm{};
            for (size_t i = 0; i < R; ++i)
                for (size_t j = 0; j < C; ++j)
                    Rm.m[j][i] = M.m[i][j];
            return Rm;
        }

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
        inline Matrix4x4f TransposeMatrix(const Matrix4x4f& M) noexcept {
            Matrix4x4f Rt{};
            __m128 r0 = _mm_loadu_ps(M.m[0].data());
            __m128 r1 = _mm_loadu_ps(M.m[1].data());
            __m128 r2 = _mm_loadu_ps(M.m[2].data());
            __m128 r3 = _mm_loadu_ps(M.m[3].data());
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
            _mm_storeu_ps(Rt.m[0].data(), r0);
            _mm_storeu_ps(Rt.m[1].data(), r1);
            _mm_storeu_ps(Rt.m[2].data(), r2);
            _mm_storeu_ps(Rt.m[3].data(), r3);
            return Rt;
        }
#endif

        //==============================
        // 4x4 一般逆行列（スカラー）
        //==============================
        template<typename T>
        Matrix<4, 4, T> Inverse(const Matrix<4, 4, T>& M) noexcept {
            Matrix<4, 4, T> inv{};
            const auto& a = M.m;

            inv[0][0] = a[1][1] * a[2][2] * a[3][3] - a[1][1] * a[2][3] * a[3][2] - a[2][1] * a[1][2] * a[3][3]
                + a[2][1] * a[1][3] * a[3][2] + a[3][1] * a[1][2] * a[2][3] - a[3][1] * a[1][3] * a[2][2];

            inv[0][1] = -a[0][1] * a[2][2] * a[3][3] + a[0][1] * a[2][3] * a[3][2] + a[2][1] * a[0][2] * a[3][3]
                - a[2][1] * a[0][3] * a[3][2] - a[3][1] * a[0][2] * a[2][3] + a[3][1] * a[0][3] * a[2][2];

            inv[0][2] = a[0][1] * a[1][2] * a[3][3] - a[0][1] * a[1][3] * a[3][2] - a[1][1] * a[0][2] * a[3][3]
                + a[1][1] * a[0][3] * a[3][2] + a[3][1] * a[0][2] * a[1][3] - a[3][1] * a[0][3] * a[1][2];

            inv[0][3] = -a[0][1] * a[1][2] * a[2][3] + a[0][1] * a[1][3] * a[2][2] + a[1][1] * a[0][2] * a[2][3]
                - a[1][1] * a[0][3] * a[2][2] - a[2][1] * a[0][2] * a[1][3] + a[2][1] * a[0][3] * a[1][2];

            inv[1][0] = -a[1][0] * a[2][2] * a[3][3] + a[1][0] * a[2][3] * a[3][2] + a[2][0] * a[1][2] * a[3][3]
                - a[2][0] * a[1][3] * a[3][2] - a[3][0] * a[1][2] * a[2][3] + a[3][0] * a[1][3] * a[2][2];

            inv[1][1] = a[0][0] * a[2][2] * a[3][3] - a[0][0] * a[2][3] * a[3][2] - a[2][0] * a[0][2] * a[3][3]
                + a[2][0] * a[0][3] * a[3][2] + a[3][0] * a[0][2] * a[2][3] - a[3][0] * a[0][3] * a[2][2];

            inv[1][2] = -a[0][0] * a[1][2] * a[3][3] + a[0][0] * a[1][3] * a[3][2] + a[1][0] * a[0][2] * a[3][3]
                - a[1][0] * a[0][3] * a[3][2] - a[3][0] * a[0][2] * a[1][3] + a[3][0] * a[0][3] * a[1][2];

            inv[1][3] = a[0][0] * a[1][2] * a[2][3] - a[0][0] * a[1][3] * a[2][2] - a[1][0] * a[0][2] * a[2][3]
                + a[1][0] * a[0][3] * a[2][2] + a[2][0] * a[0][2] * a[1][3] - a[2][0] * a[0][3] * a[1][2];

            inv[2][0] = a[1][0] * a[2][1] * a[3][3] - a[1][0] * a[2][3] * a[3][1] - a[2][0] * a[1][1] * a[3][3]
                + a[2][0] * a[1][3] * a[3][1] + a[3][0] * a[1][1] * a[2][3] - a[3][0] * a[1][3] * a[2][1];

            inv[2][1] = -a[0][0] * a[2][1] * a[3][3] + a[0][0] * a[2][3] * a[3][1] + a[2][0] * a[0][1] * a[3][3]
                - a[2][0] * a[0][3] * a[3][1] - a[3][0] * a[0][1] * a[2][3] + a[3][0] * a[0][3] * a[2][1];

            inv[2][2] = a[0][0] * a[1][1] * a[3][3] - a[0][0] * a[1][3] * a[3][1] - a[1][0] * a[0][1] * a[3][3]
                + a[1][0] * a[0][3] * a[3][1] + a[3][0] * a[0][1] * a[1][3] - a[3][0] * a[0][3] * a[1][1];

            inv[2][3] = -a[0][0] * a[1][1] * a[2][3] + a[0][0] * a[1][3] * a[2][1] + a[1][0] * a[0][1] * a[2][3]
                - a[1][0] * a[0][3] * a[2][1] - a[2][0] * a[0][1] * a[1][3] + a[2][0] * a[0][3] * a[1][1];

            inv[3][0] = -a[1][0] * a[2][1] * a[3][2] + a[1][0] * a[2][2] * a[3][1] + a[2][0] * a[1][1] * a[3][2]
                - a[2][0] * a[1][2] * a[3][1] - a[3][0] * a[1][1] * a[2][2] + a[3][0] * a[1][2] * a[2][1];

            inv[3][1] = a[0][0] * a[2][1] * a[3][2] - a[0][0] * a[2][2] * a[3][1] - a[2][0] * a[0][1] * a[3][2]
                + a[2][0] * a[0][2] * a[3][1] + a[3][0] * a[0][1] * a[2][2] - a[3][0] * a[0][2] * a[2][1];

            inv[3][2] = -a[0][0] * a[1][1] * a[3][2] + a[0][0] * a[1][2] * a[3][1] + a[1][0] * a[0][1] * a[3][2]
                - a[1][0] * a[0][2] * a[3][1] - a[3][0] * a[0][1] * a[1][2] + a[3][0] * a[0][2] * a[1][1];

            inv[3][3] = a[0][0] * a[1][1] * a[2][2] - a[0][0] * a[1][2] * a[2][1] - a[1][0] * a[0][1] * a[2][2]
                + a[1][0] * a[0][2] * a[2][1] + a[2][0] * a[0][1] * a[1][2] - a[2][0] * a[0][2] * a[1][1];

            T det = a[0][0] * inv[0][0] + a[0][1] * inv[1][0] + a[0][2] * inv[2][0] + a[0][3] * inv[3][0];
            assert(det != T(0));
            T invDet = T(1) / det;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    inv[i][j] *= invDet;
            return inv;
        }

        //==============================
        // アフィン逆行列 (4x4 float)
        //==============================
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
// 回転が正規直交(=R^T=R^{-1}) と仮定した最速パス
        inline Matrix4x4f InverseAffineOrthonormal(const Matrix4x4f& M) noexcept {
            // 行列の4行をロード
            __m128 r0 = _mm_loadu_ps(M.m[0].data());
            __m128 r1 = _mm_loadu_ps(M.m[1].data());
            __m128 r2 = _mm_loadu_ps(M.m[2].data());
            __m128 r3 = _mm_loadu_ps(M.m[3].data());

            // 列に変換（col0, col1, col2, col3）
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
            const __m128 col0 = r0; // [m00 m10 m20 m30]
            const __m128 col1 = r1; // [m01 m11 m21 m31]
            const __m128 col2 = r2; // [m02 m12 m22 m32]
            const __m128 tcol = r3; // [tx  ty  tz  1]

            // R^T をそのまま行として使う（最後の要素は0）
            Matrix4x4f Out = Matrix4x4f::Identity();
            alignas(16) float tmp[4];

            // 行0
            _mm_storeu_ps(tmp, col0); // col0 = (m00,m10,m20,m30)
            Out.m[0][0] = tmp[0]; Out.m[0][1] = tmp[1]; Out.m[0][2] = tmp[2]; Out.m[0][3] = 0.f;
            // 行1
            _mm_storeu_ps(tmp, col1);
            Out.m[1][0] = tmp[0]; Out.m[1][1] = tmp[1]; Out.m[1][2] = tmp[2]; Out.m[1][3] = 0.f;
            // 行2
            _mm_storeu_ps(tmp, col2);
            Out.m[2][0] = tmp[0]; Out.m[2][1] = tmp[1]; Out.m[2][2] = tmp[2]; Out.m[2][3] = 0.f;

            // t' = -(R^T * t)
            const __m128 maskXYZ = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1)); // [1,1,1,0]
            const __m128 txyz = _mm_and_ps(tcol, maskXYZ);
            auto dot3 = [&](__m128 a)->float {
                __m128 prod = _mm_mul_ps(a, txyz);
                __m128 shuf = _mm_movehdup_ps(prod);      // (y y w w)
                __m128 sums = _mm_add_ps(prod, shuf);     // (x+y, y+y, z+w, w+w)
                shuf = _mm_movehl_ps(shuf, sums);         // (z+w, w+w, ?, ?)
                sums = _mm_add_ss(sums, shuf);            // x+y+z
                return _mm_cvtss_f32(sums);
                };
            Out.m[0][3] = -dot3(col0);
            Out.m[1][3] = -dot3(col1);
            Out.m[2][3] = -dot3(col2);
            // 最下行は (0,0,0,1)
            return Out;
        }
#endif

        // 一般アフィン（R が任意の非特異 3x3）の逆行列
        inline Matrix4x4f InverseAffine(const Matrix4x4f& M) noexcept {
            // 上左3x3 をスカラーで逆行列、平行移動は -R^{-1}t
            const float a00 = M.m[0][0], a01 = M.m[0][1], a02 = M.m[0][2];
            const float a10 = M.m[1][0], a11 = M.m[1][1], a12 = M.m[1][2];
            const float a20 = M.m[2][0], a21 = M.m[2][1], a22 = M.m[2][2];

            const float det =
                a00 * (a11 * a22 - a12 * a21) -
                a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);
            assert(std::fabs(det) > 0.0f);
            const float invDet = 1.0f / det;

            Matrix4x4f Rinv{};
            Rinv.m[0][0] = (a11 * a22 - a12 * a21) * invDet;
            Rinv.m[0][1] = -(a01 * a22 - a02 * a21) * invDet;
            Rinv.m[0][2] = (a01 * a12 - a02 * a11) * invDet;

            Rinv.m[1][0] = -(a10 * a22 - a12 * a20) * invDet;
            Rinv.m[1][1] = (a00 * a22 - a02 * a20) * invDet;
            Rinv.m[1][2] = -(a00 * a12 - a02 * a10) * invDet;

            Rinv.m[2][0] = (a10 * a21 - a11 * a20) * invDet;
            Rinv.m[2][1] = -(a00 * a21 - a01 * a20) * invDet;
            Rinv.m[2][2] = (a00 * a11 - a01 * a10) * invDet;

            const float tx = M.m[0][3], ty = M.m[1][3], tz = M.m[2][3];

            Matrix4x4f Out = Matrix4x4f::Identity();
            // 上左3x3
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    Out.m[r][c] = Rinv.m[r][c];
            // 右端の列（平行移動）
            Out.m[0][3] = -(Rinv.m[0][0] * tx + Rinv.m[0][1] * ty + Rinv.m[0][2] * tz);
            Out.m[1][3] = -(Rinv.m[1][0] * tx + Rinv.m[1][1] * ty + Rinv.m[1][2] * tz);
            Out.m[2][3] = -(Rinv.m[2][0] * tx + Rinv.m[2][1] * ty + Rinv.m[2][2] * tz);
            return Out;
        }

        // 近似で正規直交とみなせるか（しきい値は適宜調整）
        inline bool IsOrthonormalRotation3x3(const Matrix4x4f& M, float eps = 1e-4f) noexcept {
            // 行ベースでチェック（列でも本質同じ）
            const Vec3<float> r0{ M.m[0][0],M.m[0][1],M.m[0][2] };
            const Vec3<float> r1{ M.m[1][0],M.m[1][1],M.m[1][2] };
            const Vec3<float> r2{ M.m[2][0],M.m[2][1],M.m[2][2] };
            auto near1 = [&](float v) { return std::fabs(v - 1.f) <= eps; };
            auto near0 = [&](float v) { return std::fabs(v) <= eps; };
            return near1(r0.dot(r0)) && near1(r1.dot(r1)) && near1(r2.dot(r2)) &&
                near0(r0.dot(r1)) && near0(r1.dot(r2)) && near0(r2.dot(r0));
        }

        //==============================
        // バッチ演算
        //==============================
        inline void Multiply4x4Batch(const Matrix4x4f* A, const Matrix4x4f* B, Matrix4x4f* C, size_t n) noexcept {
            for (size_t i = 0; i < n; ++i) C[i] = A[i] * B[i];
        }

        // 右辺 B が同一のとき（B のロードをループ外へ）
        inline void MultiplyManyBySameRight(const Matrix4x4f* A, const Matrix4x4f& B, Matrix4x4f* C, size_t n) noexcept {
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
            const __m128 b0 = _mm_loadu_ps(B.m[0].data());
            const __m128 b1 = _mm_loadu_ps(B.m[1].data());
            const __m128 b2 = _mm_loadu_ps(B.m[2].data());
            const __m128 b3 = _mm_loadu_ps(B.m[3].data());

            for (size_t idx = 0; idx < n; ++idx) {
                const Matrix4x4f& A0 = A[idx];
                for (int i = 0; i < 4; ++i) {
                    const __m128 a0 = _mm_set1_ps(A0.m[i][0]);
                    const __m128 a1 = _mm_set1_ps(A0.m[i][1]);
                    const __m128 a2 = _mm_set1_ps(A0.m[i][2]);
                    const __m128 a3 = _mm_set1_ps(A0.m[i][3]);

                    __m128 r = _mm_mul_ps(a0, b0);
                    r = detail::fmadd_ps(a1, b1, r);
                    r = detail::fmadd_ps(a2, b2, r);
                    r = detail::fmadd_ps(a3, b3, r);

                    _mm_storeu_ps(C[idx].m[i].data(), r);
                }
            }
#else
            for (size_t i = 0; i < n; ++i) C[i] = A[i] * B;
#endif
        }

        // 点群変換（p' = M * [x y z 1]^T）
        inline void TransformPoints(const Matrix4x4f& M, const Vec3<float>* inPts, Vec3<float>* outPts, size_t n) noexcept {
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
            // 列ベクトル向けに列を一度だけ用意（転置で取得）
            __m128 r0 = _mm_loadu_ps(M.m[0].data());
            __m128 r1 = _mm_loadu_ps(M.m[1].data());
            __m128 r2 = _mm_loadu_ps(M.m[2].data());
            __m128 r3 = _mm_loadu_ps(M.m[3].data());
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3); // r0..r3 が列（col0..col3）

            for (size_t i = 0; i < n; ++i) {
                const __m128 vx = _mm_set1_ps(inPts[i].x);
                const __m128 vy = _mm_set1_ps(inPts[i].y);
                const __m128 vz = _mm_set1_ps(inPts[i].z);

                __m128 res = _mm_mul_ps(vx, r0);
                res = detail::fmadd_ps(vy, r1, res);
                res = detail::fmadd_ps(vz, r2, res);
                res = _mm_add_ps(res, r3);

                alignas(16) float tmp[4];
                _mm_storeu_ps(tmp, res);
                outPts[i].x = tmp[0];
                outPts[i].y = tmp[1];
                outPts[i].z = tmp[2];
                // w は不要なら破棄
            }
#else
            for (size_t i = 0; i < n; ++i) {
                const float x = inPts[i].x, y = inPts[i].y, z = inPts[i].z;
                outPts[i].x = M.m[0][0] * x + M.m[0][1] * y + M.m[0][2] * z + M.m[0][3];
                outPts[i].y = M.m[1][0] * x + M.m[1][1] * y + M.m[1][2] * z + M.m[1][3];
                outPts[i].z = M.m[2][0] * x + M.m[2][1] * y + M.m[2][2] * z + M.m[2][3];
            }
#endif
        }

        //==============================
        // 行列ビルダー (4x4)
        //==============================
        template<typename T>
        Matrix<4, 4, T> MakeTranslationMatrix(const Vec3<T>& t) noexcept {
            auto m = Matrix<4, 4, T>::Identity();
            m[0][3] = t.x; m[1][3] = t.y; m[2][3] = t.z;
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakeScalingMatrix(const Vec3<T>& s) noexcept {
            Matrix<4, 4, T> m{};
            m[0][0] = s.x; m[1][1] = s.y; m[2][2] = s.z; m[3][3] = T(1);
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakeRotationMatrix(const Quat<T>& q) noexcept {
            // q は正規化されている前提
            Matrix<4, 4, T> m{};
            const T x = q.x, y = q.y, z = q.z, w = q.w;
            const T xx = x * x, yy = y * y, zz = z * z;
            const T xy = x * y, xz = x * z, yz = y * z;
            const T wx = w * x, wy = w * y, wz = w * z;

            m[0][0] = T(1) - T(2) * (yy + zz);
            m[0][1] = T(2) * (xy - wz);
            m[0][2] = T(2) * (xz + wy);
            m[0][3] = T(0);

            m[1][0] = T(2) * (xy + wz);
            m[1][1] = T(1) - T(2) * (xx + zz);
            m[1][2] = T(2) * (yz - wx);
            m[1][3] = T(0);

            m[2][0] = T(2) * (xz - wy);
            m[2][1] = T(2) * (yz + wx);
            m[2][2] = T(1) - T(2) * (xx + yy);
            m[2][3] = T(0);

            m[3][0] = T(0); m[3][1] = T(0); m[3][2] = T(0); m[3][3] = T(1);
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakeLookAtMatrixLH(const Vec3<T>& eye, const Vec3<T>& target, const Vec3<T>& up) noexcept {
            Vec3<T> z = (target - eye).normalized();   // forward
            Vec3<T> x = up.cross(z).normalized();      // right
            Vec3<T> y = z.cross(x);                    // up（再直交）

            Matrix<4, 4, T> m{};
            // 回転は「列に軸ベクトル」を入れる（＝行には x/y/z の各成分）
            m[0][0] = x.x; m[0][1] = y.x; m[0][2] = z.x; m[0][3] = T(0);
            m[1][0] = x.y; m[1][1] = y.y; m[1][2] = z.y; m[1][3] = T(0);
            m[2][0] = x.z; m[2][1] = y.z; m[2][2] = z.z; m[2][3] = T(0);

            // ← 平行移動は“最下行”
            m[3][0] = -x.dot(eye);
            m[3][1] = -y.dot(eye);
            m[3][2] = -z.dot(eye);
            m[3][3] = T(1);
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakeLookAtMatrixRH(const Vec3<T>& eye, const Vec3<T>& center, const Vec3<T>& up) noexcept {
            Vec3<T> f = (center - eye).normalized();
            Vec3<T> s = f.cross(up).normalized();
            Vec3<T> u = s.cross(f);

            Matrix<4, 4, T> m{};
            m[0][0] = s.x; m[0][1] = u.x; m[0][2] = -f.x; m[0][3] = T(0);
            m[1][0] = s.y; m[1][1] = u.y; m[1][2] = -f.y; m[1][3] = T(0);
            m[2][0] = s.z; m[2][1] = u.z; m[2][2] = -f.z; m[2][3] = T(0);

            m[3][0] = -s.dot(eye);
            m[3][1] = -u.dot(eye);
            m[3][2] = f.dot(eye);
            m[3][3] = T(1);
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakePerspectiveMatrixLH(T fovY, T aspect, T nearZ, T farZ) noexcept {
            const T f = T(1) / std::tan(fovY / T(2));
            Matrix<4, 4, T> m{};
            m[0][0] = f / aspect; m[1][1] = f;
            m[2][2] = farZ / (farZ - nearZ);
            m[2][3] = T(1);
            m[3][2] = (-nearZ * farZ) / (farZ - nearZ);
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakePerspectiveMatrixRH(T fovY, T aspect, T zNear, T zFar) noexcept {
            const T f = T(1) / std::tan(fovY / T(2));
            Matrix<4, 4, T> m{};
            m[0][0] = f / aspect; m[1][1] = f;
            m[2][2] = (zFar + zNear) / (zNear - zFar);
            m[2][3] = T(-1);
            m[3][2] = (T(2) * zFar * zNear) / (zNear - zFar);
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakeOrthographicMatrixLH(T l, T r, T b, T t, T n, T f) noexcept {
            Matrix<4, 4, T> m{};
            m[0][0] = T(2) / (r - l);
            m[1][1] = T(2) / (t - b);
            m[2][2] = T(1) / (f - n);
            // ← 平行移動は“最下行”
            m[3][0] = -(r + l) / (r - l);
            m[3][1] = -(t + b) / (t - b);
            m[3][2] = -n / (f - n);
            m[3][3] = T(1);
            return m;
        }

        template<typename T>
        Matrix<4, 4, T> MakeOrthographicMatrixRH(T l, T r, T b, T t, T n, T f) noexcept {
            Matrix<4, 4, T> m{};
            m[0][0] = T(2) / (r - l);
            m[1][1] = T(2) / (t - b);
            m[2][2] = T(-2) / (f - n);
            m[3][0] = -(r + l) / (r - l);
            m[3][1] = -(t + b) / (t - b);
            m[3][2] = -(f + n) / (f - n);
            m[3][3] = T(1);
            return m;
        }


        //==============================
        // ユーティリティ: 点/方向の単発変換
        //==============================
        template<typename T>
        inline Vec3<T> TransformPoint(const Matrix<4, 4, T>& M, const Vec3<T>& p) noexcept {
            return {
                M.m[0][0] * p.x + M.m[0][1] * p.y + M.m[0][2] * p.z + M.m[0][3],
                M.m[1][0] * p.x + M.m[1][1] * p.y + M.m[1][2] * p.z + M.m[1][3],
                M.m[2][0] * p.x + M.m[2][1] * p.y + M.m[2][2] * p.z + M.m[2][3],
            };
        }
        template<typename T>
        inline Vec3<T> TransformVector(const Matrix<4, 4, T>& M, const Vec3<T>& v) noexcept {
            return {
                M.m[0][0] * v.x + M.m[0][1] * v.y + M.m[0][2] * v.z,
                M.m[1][0] * v.x + M.m[1][1] * v.y + M.m[1][2] * v.z,
                M.m[2][0] * v.x + M.m[2][1] * v.y + M.m[2][2] * v.z,
            };
        }

        //==============================
        // 推奨: アフィン逆行列の統合入口
        //==============================
        inline Matrix4x4f InverseFastAffine(const Matrix4x4f& M) noexcept {
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
            if (IsOrthonormalRotation3x3(M)) return InverseAffineOrthonormal(M);
#endif
            return InverseAffine(M);
        }
	}
}
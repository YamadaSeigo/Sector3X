/*****************************************************************//**
 * @file   Matrix.hpp
 * @brief  行列を定義するヘッダーファイル（列ベクトル規約に統一）
 * @author seigo
 * @date   Revised: Oct 2025
 *********************************************************************/

#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#include <immintrin.h> // SSE2/AVX/FMA
#elif defined(__SSE2__)
#include <immintrin.h>
#endif

#include "Vector.hpp"      // Vec2/3/4
#include "Quaternion.hpp"  // Quat
#include "AABB.hpp"        // AABB<Vec3<T>>

 //==============================================================
 // 本ヘッダの前提（必ず守る）
 // - ストレージ: row-major（m[r][c]）
 // - 演算規約  : 列ベクトル p' = M * p
 // - 平行移動  : 最右列（m[0..2][3]）
 //==============================================================

namespace SFW {
    namespace Math {

        //==============================
        // 先行宣言: 乗算カーネル
        //==============================
        template<size_t R, size_t K, size_t C, typename T>
        struct MatMulKernel;

        //==============================
        // 行列（汎用テンプレート, row-major）
        //==============================
        template <size_t Rows, size_t Cols, typename T>
        struct Matrix {
            static constexpr size_t kRows = Rows;
            static constexpr size_t kCols = Cols;

            std::array<std::array<T, Cols>, Rows> m{};

            static constexpr Matrix Identity() noexcept {
                static_assert(Rows == Cols, "Identity matrix must be square.");
                Matrix I{};
                for (size_t i = 0; i < Rows; ++i) I.m[i][i] = T(1);
                return I;
            }

            inline std::array<T, Cols>& operator[](size_t r) noexcept { return m[r]; }
            inline const std::array<T, Cols>& operator[](size_t r) const noexcept { return m[r]; }

            inline T* data() noexcept { return &m[0][0]; }
            inline const T* data() const noexcept { return &m[0][0]; }
            inline const T* ToPointer() const noexcept { return &m[0][0]; }

            template <size_t OtherCols>
            Matrix<Rows, OtherCols, T>
                operator*(const Matrix<Cols, OtherCols, T>& rhs) const noexcept {
                return MatMulKernel<Rows, Cols, OtherCols, T>::eval(*this, rhs);
            }
        };

        // 型エイリアス
        using Matrix4x4f = Matrix<4, 4, float>;
        using Matrix4x4d = Matrix<4, 4, double>;

        using Matrix3x4f = Matrix<3, 4, float>;

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
        //==============================
        // 3x4 float 専用: 16B 整列 + 連続メモリ
        //==============================
        template<>
        struct alignas(16) Matrix<3, 4, float> {
            static constexpr size_t kRows = 3;
            static constexpr size_t kCols = 4;
            float m[3][4];

            static constexpr Matrix Identity() noexcept {
                Matrix I{};
                I.m[0][0] = 1.f; I.m[1][1] = 1.f; I.m[2][2] = 1.f;
                return I;
            }
            inline float* operator[](size_t r) noexcept { return m[r]; }
            inline const float* operator[](size_t r) const noexcept { return m[r]; }

            inline float* data() noexcept { return &m[0][0]; }
            inline const float* data() const noexcept { return &m[0][0]; }
            inline const float* ToPointer() const noexcept { return &m[0][0]; }

            template <size_t OtherCols>
            Matrix<3, OtherCols, float>
                operator*(const Matrix<3, OtherCols, float>& rhs) const noexcept {
                return MatMulKernel<3, 4, OtherCols, float>::eval(*this, rhs);
            }
        };
        static_assert(alignof(Matrix3x4f) >= 16, "Matrix4x4f must be 16B aligned");

        //==============================
        // 4x4 float 専用: 16B 整列 + 連続メモリ
        //==============================
        template<>
        struct alignas(16) Matrix<4, 4, float> {
            static constexpr size_t kRows = 4;
            static constexpr size_t kCols = 4;
            float m[4][4];

            static constexpr Matrix Identity() noexcept {
                Matrix I{};
                I.m[0][0] = 1.f; I.m[1][1] = 1.f; I.m[2][2] = 1.f; I.m[3][3] = 1.f;
                return I;
            }
            inline float* operator[](size_t r) noexcept { return m[r]; }
            inline const float* operator[](size_t r) const noexcept { return m[r]; }

            inline float* data() noexcept { return &m[0][0]; }
            inline const float* data() const noexcept { return &m[0][0]; }
            inline const float* ToPointer() const noexcept { return &m[0][0]; }

            template <size_t OtherCols>
            Matrix<4, OtherCols, float>
                operator*(const Matrix<4, OtherCols, float>& rhs) const noexcept {
                return MatMulKernel<4, 4, OtherCols, float>::eval(*this, rhs);
            }
        };
        static_assert(alignof(Matrix4x4f) >= 16, "Matrix4x4f must be 16B aligned");
#endif

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
#if defined(__FMA__) || defined(__AVX2__) || defined(_M_FMA) || defined(_M_AVX2)
                // FMA が使える場合は 1 命令
                return _mm_fmadd_ps(a, b, c);  // a*b + c
#else
                return _mm_add_ps(_mm_mul_ps(a, b), c);
#endif
            }
        }

        // C = A(4x4) * B(4x4)  （row-major ストレージ、演算は列ベクトル規約）
        template<>
        struct MatMulKernel<4, 4, 4, float> {
            static Matrix4x4f eval(const Matrix4x4f& A, const Matrix4x4f& B) noexcept {
                Matrix4x4f C{};

                const __m128 b0 = _mm_loadu_ps(B.m[0]);
                const __m128 b1 = _mm_loadu_ps(B.m[1]);
                const __m128 b2 = _mm_loadu_ps(B.m[2]);
                const __m128 b3 = _mm_loadu_ps(B.m[3]);

                for (int i = 0; i < 4; ++i) {
                    const __m128 a0 = _mm_set1_ps(A.m[i][0]);
                    const __m128 a1 = _mm_set1_ps(A.m[i][1]);
                    const __m128 a2 = _mm_set1_ps(A.m[i][2]);
                    const __m128 a3 = _mm_set1_ps(A.m[i][3]);

                    __m128 r = _mm_mul_ps(a0, b0);
                    r = detail::fmadd_ps(a1, b1, r);
                    r = detail::fmadd_ps(a2, b2, r);
                    r = detail::fmadd_ps(a3, b3, r);

                    _mm_storeu_ps(C.m[i], r);
                }
                return C;
            }
        };

#endif // SSE2

        //==============================
        // 転置
        //==============================
        template <size_t R, size_t C, typename T>
        constexpr Matrix<C, R, T> TransposeMatrix(const Matrix<R, C, T>& M) noexcept {
            Matrix<C, R, T> Rt{};
            for (size_t i = 0; i < R; ++i) for (size_t j = 0; j < C; ++j) Rt.m[j][i] = M.m[i][j];
            return Rt;
        }

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
        inline Matrix4x4f TransposeMatrix(const Matrix4x4f& M) noexcept {
            Matrix4x4f Rt{};
            __m128 r0 = _mm_loadu_ps(M.m[0]);
            __m128 r1 = _mm_loadu_ps(M.m[1]);
            __m128 r2 = _mm_loadu_ps(M.m[2]);
            __m128 r3 = _mm_loadu_ps(M.m[3]);
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
            _mm_storeu_ps(Rt.m[0], r0);
            _mm_storeu_ps(Rt.m[1], r1);
            _mm_storeu_ps(Rt.m[2], r2);
            _mm_storeu_ps(Rt.m[3], r3);
            return Rt;
        }
#endif

        //==============================
        // ベクトルとの積（列ベクトル規約）
        //==============================
        template<typename T>
        inline Vec4<T> operator*(const Matrix<4, 4, T>& M, const Vec4<T>& v) noexcept {
            Vec4<T> out{};
            out.x = M.m[0][0] * v.x + M.m[0][1] * v.y + M.m[0][2] * v.z + M.m[0][3] * v.w;
            out.y = M.m[1][0] * v.x + M.m[1][1] * v.y + M.m[1][2] * v.z + M.m[1][3] * v.w;
            out.z = M.m[2][0] * v.x + M.m[2][1] * v.y + M.m[2][2] * v.z + M.m[2][3] * v.w;
            out.w = M.m[3][0] * v.x + M.m[3][1] * v.y + M.m[3][2] * v.z + M.m[3][3] * v.w;
            return out;
        }

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
        inline Vec4f operator*(const Matrix4x4f& M, const Vec4f& v) noexcept {
            // 列ベクトル = 列の線形結合（列を得るために転置）
            __m128 r0 = _mm_loadu_ps(M.m[0]);
            __m128 r1 = _mm_loadu_ps(M.m[1]);
            __m128 r2 = _mm_loadu_ps(M.m[2]);
            __m128 r3 = _mm_loadu_ps(M.m[3]);
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3); // r0..r3 が列

            const __m128 vx = _mm_set1_ps(v.x);
            const __m128 vy = _mm_set1_ps(v.y);
            const __m128 vz = _mm_set1_ps(v.z);
            const __m128 vw = _mm_set1_ps(v.w);

            __m128 out = _mm_mul_ps(vx, r0);
            out = detail::fmadd_ps(vy, r1, out);
            out = detail::fmadd_ps(vz, r2, out);
            out = detail::fmadd_ps(vw, r3, out);

            alignas(16) float t[4];
            _mm_storeu_ps(t, out);
            return Vec4f{ t[0],t[1],t[2],t[3] };
        }
#endif

        //（行ベクトル規約の v*M は今回の統一では使わないため未提供／必要なら別名で実装）

        //==============================
        // AABB 変換（列ベクトル規約対応）
        //==============================
        template<typename T, typename VecT>
        inline AABB<T, VecT> operator*(const Matrix<4, 4, T>& M, const AABB<T, VecT>& box) noexcept {
            // ADL 安全な abs（float/double 両対応）
            using std::abs;

            static_assert(std::is_same_v<VecT, Vec3<T>>, "This overload expects Vec3<T> AABB.");

            const VecT c = (box.lb + box.ub) * T(0.5);
            const VecT e = (box.ub - box.lb) * T(0.5);

            // c' = R*c + t  （右列が t）
            VecT c2;
            c2.x = M.m[0][0] * c.x + M.m[0][1] * c.y + M.m[0][2] * c.z + M.m[0][3];
            c2.y = M.m[1][0] * c.x + M.m[1][1] * c.y + M.m[1][2] * c.z + M.m[1][3];
            c2.z = M.m[2][0] * c.x + M.m[2][1] * c.y + M.m[2][2] * c.z + M.m[2][3];

            const T ex = abs(e.x), ey = abs(e.y), ez = abs(e.z);

            // e' = |R| * e  （列ベクトル規約は「行の絶対値」を使う）
            VecT e2;
            e2.x = ex * abs(M.m[0][0]) + ey * abs(M.m[0][1]) + ez * abs(M.m[0][2]);
            e2.y = ex * abs(M.m[1][0]) + ey * abs(M.m[1][1]) + ez * abs(M.m[1][2]);
            e2.z = ex * abs(M.m[2][0]) + ey * abs(M.m[2][1]) + ez * abs(M.m[2][2]);

            AABB<T, VecT> out;
            out.lb = c2 - e2;
            out.ub = c2 + e2;
            return out;
        }

        // C = A(4x4) * B(3x4)  を 4x4 に出力（B の下段は [0,0,0,1] と仮定）
        static inline Matrix4x4f Mul4x4x3x4_SSE_rowcombine(const Matrix4x4f& A,
            const Matrix3x4f& B) noexcept
        {
            Matrix4x4f C;

            // B の「行」をそのままロード
            const __m128 b0 = _mm_loadu_ps(&B.m[0][0]); // [r01 r02 r03 t01]
            const __m128 b1 = _mm_loadu_ps(&B.m[1][0]); // [r11 r12 r13 t02]
            const __m128 b2 = _mm_loadu_ps(&B.m[2][0]); // [r21 r22 r23 t03]
            const __m128 b3 = _mm_set_ps(1.f, 0.f, 0.f, 0.f); // 下段 = [0,0,0,1] を "行" として

            for (int i = 0; i < 4; ++i) {
                const __m128 a0 = _mm_set1_ps(A.m[i][0]);
                const __m128 a1 = _mm_set1_ps(A.m[i][1]);
                const __m128 a2 = _mm_set1_ps(A.m[i][2]);
                const __m128 a3 = _mm_set1_ps(A.m[i][3]);

                __m128 r = _mm_mul_ps(a0, b0);
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                r = _mm_fmadd_ps(a1, b1, r);
                r = _mm_fmadd_ps(a2, b2, r);
                r = _mm_fmadd_ps(a3, b3, r);
#else
                r = _mm_add_ps(r, _mm_mul_ps(a1, b1));
                r = _mm_add_ps(r, _mm_mul_ps(a2, b2));
                r = _mm_add_ps(r, _mm_mul_ps(a3, b3));
#endif
                _mm_storeu_ps(C.m[i], r);
            }
            return C;
        }

        // 3x4 を列ベクトル4本（c0..c3）に（c3 は [tx ty tz 1]）
        static inline void MakeColsFromMat3x4_SSE(const Matrix3x4f& B,
            __m128& c0, __m128& c1,
            __m128& c2, __m128& c3) noexcept
        {
            const __m128 r0 = _mm_loadu_ps(&B.m[0][0]); // [r00 r01 r02 tx]
            const __m128 r1 = _mm_loadu_ps(&B.m[1][0]); // [r10 r11 r12 ty]
            const __m128 r2 = _mm_loadu_ps(&B.m[2][0]); // [r20 r21 r22 tz]
            __m128 r3 = _mm_setzero_ps();               // ダミー

            // 転置で c0=[r00 r10 r20 0], c1=[r01 r11 r21 0], c2=[r02 r12 r22 0], c3=[tx ty tz 0]
            __m128 t0 = r0, t1 = r1, t2 = r2, t3 = r3;
            _MM_TRANSPOSE4_PS(t0, t1, t2, t3);
            c0 = t0; c1 = t1; c2 = t2;
            // c3 の W を 1 に差し替える
            c3 = _mm_set_ps(1.0f, B.m[2][3], B.m[1][3], B.m[0][3]); // [tx ty tz 1]
        }

        static inline void Mul4x4x3x4_Batch_To4x4_SSE_RowCombine(
            const Matrix4x4f& VP,
            const Matrix3x4f* __restrict W,   // N 個の World(3x4)
            Matrix4x4f* __restrict C,         // N 個の出力(4x4)
            std::size_t N) noexcept
        {
            // VP の各行を一発で引けるようにしておく
            const __m128 b3_row = _mm_set_ps(1.f, 0.f, 0.f, 0.f); // 下段 = [0,0,0,1]

            // 各行の係数（a0..a3 = VP.m[i][0..3]）をブロードキャストしておく
            __m128 a0[4], a1[4], a2[4], a3[4];
            for (int i = 0; i < 4; ++i) {
                a0[i] = _mm_set1_ps(VP.m[i][0]);
                a1[i] = _mm_set1_ps(VP.m[i][1]);
                a2[i] = _mm_set1_ps(VP.m[i][2]);
                a3[i] = _mm_set1_ps(VP.m[i][3]);
            }

            for (std::size_t i = 0; i < N; ++i) {
                const __m128 b0 = _mm_loadu_ps(&W[i].m[0][0]); // [r01 r02 r03 t01]
                const __m128 b1 = _mm_loadu_ps(&W[i].m[1][0]); // [r11 r12 r13 t02]
                const __m128 b2 = _mm_loadu_ps(&W[i].m[2][0]); // [r21 r22 r23 t03]

                // row_i(C) = a0[i]*b0 + a1[i]*b1 + a2[i]*b2 + a3[i]*[0,0,0,1]
                for (int r = 0; r < 4; ++r) {
                    __m128 v = _mm_mul_ps(a0[r], b0);
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    v = _mm_fmadd_ps(a1[r], b1, v);
                    v = _mm_fmadd_ps(a2[r], b2, v);
                    v = _mm_fmadd_ps(a3[r], b3_row, v);
#else
                    v = _mm_add_ps(v, _mm_mul_ps(a1[r], b1));
                    v = _mm_add_ps(v, _mm_mul_ps(a2[r], b2));
                    v = _mm_add_ps(v, _mm_mul_ps(a3[r], b3_row));
#endif
                    _mm_storeu_ps(C[i].m[r], v);
                }
            }
        }

        // AVX2/FMA: 同一 VP × 複数 W(3x4) → C(4x4)
        // 2体ずつ処理。端数1体は SSE 版に回します。
        static inline void Mul4x4x3x4_Batch_To4x4_AVX2_RowCombine(
            const Matrix4x4f& VP,
            const Matrix3x4f* __restrict W,
            Matrix4x4f* __restrict C,
            std::size_t N) noexcept
        {
#if !defined(__AVX2__) && !(defined(_MSC_VER) && defined(__AVX2__))
            // 環境が AVX2 でなければ SSE 版にフォールバック
            Mul4x4x3x4_Batch_To4x4_SSE_RowCombine(VP, W, C, N);
            return;
#else
            // 下段 [0,0,0,1] を 2 体分（下位/上位 128bit）用意
            const __m256 b3_row = _mm256_set_ps(1.f, 0.f, 0.f, 0.f,
                1.f, 0.f, 0.f, 0.f);

            // VP の各行の係数を __m256（全レーン同値）で保持
            __m256 a0[4], a1[4], a2[4], a3[4];
            for (int r = 0; r < 4; ++r) {
                a0[r] = _mm256_set1_ps(VP.m[r][0]);
                a1[r] = _mm256_set1_ps(VP.m[r][1]);
                a2[r] = _mm256_set1_ps(VP.m[r][2]);
                a3[r] = _mm256_set1_ps(VP.m[r][3]);
            }

            std::size_t i = 0;
            for (; i + 1 < N; i += 2) {
                // 2ワールド分の B の「行」を 256bit にパック
                const __m128 b0_lo = _mm_loadu_ps(&W[i + 0].m[0][0]); // [r01 r02 r03 t01]
                const __m128 b0_hi = _mm_loadu_ps(&W[i + 1].m[0][0]);
                const __m128 b1_lo = _mm_loadu_ps(&W[i + 0].m[1][0]); // [r11 r12 r13 t02]
                const __m128 b1_hi = _mm_loadu_ps(&W[i + 1].m[1][0]);
                const __m128 b2_lo = _mm_loadu_ps(&W[i + 0].m[2][0]); // [r21 r22 r23 t03]
                const __m128 b2_hi = _mm_loadu_ps(&W[i + 1].m[2][0]);

                const __m256 b0 = _mm256_set_m128(b0_hi, b0_lo);
                const __m256 b1 = _mm256_set_m128(b1_hi, b1_lo);
                const __m256 b2 = _mm256_set_m128(b2_hi, b2_lo);

                // 4 行ぶんまとめて： row_r(C) = a0*b0 + a1*b1 + a2*b2 + a3*[0,0,0,1]
                for (int r = 0; r < 4; ++r) {
                    __m256 v = _mm256_mul_ps(a0[r], b0);
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    v = _mm256_fmadd_ps(a1[r], b1, v);
                    v = _mm256_fmadd_ps(a2[r], b2, v);
                    v = _mm256_fmadd_ps(a3[r], b3_row, v);
#else
                    v = _mm256_add_ps(v, _mm256_mul_ps(a1[r], b1));
                    v = _mm256_add_ps(v, _mm256_mul_ps(a2[r], b2));
                    v = _mm256_add_ps(v, _mm256_mul_ps(a3[r], b3_row));
#endif
                    // 下位/上位 128 をそれぞれ 2 体分にストア
                    _mm_storeu_ps(C[i + 0].m[r], _mm256_castps256_ps128(v));
                    _mm_storeu_ps(C[i + 1].m[r], _mm256_extractf128_ps(v, 1));
                }
            }

            // 端数1体が残れば SSE 版で処理
            if (i < N) {
                Mul4x4x3x4_Batch_To4x4_SSE_RowCombine(VP, W + i, C + i, 1);
            }
#endif
        }

        static inline void Mul4x4x3x4_Batch_To3x4_SSE_RowCombine(
            const Matrix4x4f& VP,
            const Matrix3x4f* __restrict W,   // N 個
            Matrix3x4f* __restrict C,         // N 個（上3行のみ）
            std::size_t N) noexcept
        {
            const __m128 b3_row = _mm_set_ps(1.f, 0.f, 0.f, 0.f);

            __m128 a0[3], a1[3], a2[3], a3[3];
            for (int i = 0; i < 3; ++i) {
                a0[i] = _mm_set1_ps(VP.m[i][0]);
                a1[i] = _mm_set1_ps(VP.m[i][1]);
                a2[i] = _mm_set1_ps(VP.m[i][2]);
                a3[i] = _mm_set1_ps(VP.m[i][3]);
            }

            for (std::size_t i = 0; i < N; ++i) {
                const __m128 b0 = _mm_loadu_ps(&W[i].m[0][0]);
                const __m128 b1 = _mm_loadu_ps(&W[i].m[1][0]);
                const __m128 b2 = _mm_loadu_ps(&W[i].m[2][0]);

                for (int r = 0; r < 3; ++r) {
                    __m128 v = _mm_mul_ps(a0[r], b0);
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    v = _mm_fmadd_ps(a1[r], b1, v);
                    v = _mm_fmadd_ps(a2[r], b2, v);
                    v = _mm_fmadd_ps(a3[r], b3_row, v);
#else
                    v = _mm_add_ps(v, _mm_mul_ps(a1[r], b1));
                    v = _mm_add_ps(v, _mm_mul_ps(a2[r], b2));
                    v = _mm_add_ps(v, _mm_mul_ps(a3[r], b3_row));
#endif
                    _mm_storeu_ps(&C[i].m[r][0], v);
                }
            }
        }

        // 3x4 ワールド行列の SoA（行優先 3x4: [r00 r01 r02 tx; r10 r11 r12 ty; r20 r21 r22 tz]）
        struct Matrix3x4fSoA {
            float* m00 = nullptr; float* m01 = nullptr; float* m02 = nullptr; float* tx = nullptr;
            float* m10 = nullptr; float* m11 = nullptr; float* m12 = nullptr; float* ty = nullptr;
            float* m20 = nullptr; float* m21 = nullptr; float* m22 = nullptr; float* tz = nullptr;
            std::size_t count = 0;

            Matrix3x4fSoA() = default;

            Matrix3x4fSoA(float* buf, size_t size) noexcept {
                m00 = buf + 0  * size;
				m01 = buf + 1  * size;
				m02 = buf + 2  * size;
				tx  = buf + 3  * size;
				m10 = buf + 4  * size;
				m11 = buf + 5  * size;
				m12 = buf + 6  * size;
				ty  = buf + 7  * size;
				m20 = buf + 8  * size;
				m21 = buf + 9  * size;
				m22 = buf + 10 * size;
				tz  = buf + 11 * size;
				count = size;
            }

            Matrix3x4f AoS(size_t index) const noexcept {
                Matrix3x4f mat{};
                mat[0][0] = m00[index]; mat[0][1] = m01[index]; mat[0][2] = m02[index]; mat[0][3] = tx[index];
                mat[1][0] = m10[index]; mat[1][1] = m11[index]; mat[1][2] = m12[index]; mat[1][3] = ty[index];
                mat[2][0] = m20[index]; mat[2][1] = m21[index]; mat[2][2] = m22[index]; mat[2][3] = tz[index];
                return mat;
            }
        };

        //----------------------------------------------
        // SSE: 同一 VP × SoA World(3x4) → C(4x4)
        //----------------------------------------------
        static inline void Mul4x4x3x4_Batch_To4x4_SSE_RowCombine_SoA(
            const Matrix4x4f& VP,
            const Matrix3x4fSoA& W,           // SoA
            Matrix4x4f* __restrict C      // N 個の出力(4x4)
        ) noexcept
        {
            const std::size_t N = W.count;

            // 末行 [0,0,0,1]
            const __m128 b3_row = _mm_set_ps(1.f, 0.f, 0.f, 0.f);

            // VP の各行の係数をブロードキャスト
            __m128 a0[4], a1[4], a2[4], a3[4];
            for (int r = 0; r < 4; ++r) {
                a0[r] = _mm_set1_ps(VP.m[r][0]);
                a1[r] = _mm_set1_ps(VP.m[r][1]);
                a2[r] = _mm_set1_ps(VP.m[r][2]);
                a3[r] = _mm_set1_ps(VP.m[r][3]);
            }

            for (std::size_t i = 0; i < N; ++i) {
                // SoA から行ベクトルを組み立て（[x,y,z,w] の順で _mm_set_ps(w,z,y,x)）
                const __m128 b0 = _mm_set_ps(W.tx[i], W.m02[i], W.m01[i], W.m00[i]); // [m00 m01 m02 tx]
                const __m128 b1 = _mm_set_ps(W.ty[i], W.m12[i], W.m11[i], W.m10[i]); // [m10 m11 m12 ty]
                const __m128 b2 = _mm_set_ps(W.tz[i], W.m22[i], W.m21[i], W.m20[i]); // [m20 m21 m22 tz]

                for (int r = 0; r < 4; ++r) {
                    __m128 v = _mm_mul_ps(a0[r], b0);
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    v = _mm_fmadd_ps(a1[r], b1, v);
                    v = _mm_fmadd_ps(a2[r], b2, v);
                    v = _mm_fmadd_ps(a3[r], b3_row, v);
#else
                    v = _mm_add_ps(v, _mm_mul_ps(a1[r], b1));
                    v = _mm_add_ps(v, _mm_mul_ps(a2[r], b2));
                    v = _mm_add_ps(v, _mm_mul_ps(a3[r], b3_row));
#endif
                    _mm_storeu_ps(C[i].m[r], v);
                }
            }
        }

        //----------------------------------------------
        // AVX2/FMA: 同一 VP × SoA World(3x4) → C(4x4)
        // 2体ずつ処理（端数1体は SSE へ）
        //----------------------------------------------
        static inline void Mul4x4x3x4_Batch_To4x4_AVX2_RowCombine_SoA(
            const Matrix4x4f& VP,
            const Matrix3x4fSoA& W,             // SoA
            Matrix4x4f* __restrict C
        ) noexcept
        {
#if !defined(__AVX2__) && !(defined(_MSC_VER) && defined(__AVX2__))
            // 環境が AVX2 でなければ SSE 版にフォールバック
            Mul4x4x3x4_Batch_To4x4_SSE_RowCombine_SoA(VP, W, C);
            return;
#else
            const std::size_t N = W.count;

            // 2体分の末行 [0,0,0,1] を 256bit にパック（下位= i, 上位= i+1）
            const __m256 b3_row = _mm256_set_ps(1.f, 0.f, 0.f, 0.f,
                1.f, 0.f, 0.f, 0.f);

            // VP の各行の係数を __m256（全レーン同値）で保持
            __m256 a0[4], a1[4], a2[4], a3[4];
            for (int r = 0; r < 4; ++r) {
                a0[r] = _mm256_set1_ps(VP.m[r][0]);
                a1[r] = _mm256_set1_ps(VP.m[r][1]);
                a2[r] = _mm256_set1_ps(VP.m[r][2]);
                a3[r] = _mm256_set1_ps(VP.m[r][3]);
            }

            std::size_t i = 0;
            for (; i + 1 < N; i += 2) {
                // SoA から 2体分の行を 128bit×2 に組み立ててから 256bit へ
                const __m128 b0_lo = _mm_set_ps(W.tx[i + 0], W.m02[i + 0], W.m01[i + 0], W.m00[i + 0]);
                const __m128 b0_hi = _mm_set_ps(W.tx[i + 1], W.m02[i + 1], W.m01[i + 1], W.m00[i + 1]);

                const __m128 b1_lo = _mm_set_ps(W.ty[i + 0], W.m12[i + 0], W.m11[i + 0], W.m10[i + 0]);
                const __m128 b1_hi = _mm_set_ps(W.ty[i + 1], W.m12[i + 1], W.m11[i + 1], W.m10[i + 1]);

                const __m128 b2_lo = _mm_set_ps(W.tz[i + 0], W.m22[i + 0], W.m21[i + 0], W.m20[i + 0]);
                const __m128 b2_hi = _mm_set_ps(W.tz[i + 1], W.m22[i + 1], W.m21[i + 1], W.m20[i + 1]);

                const __m256 b0 = _mm256_set_m128(b0_hi, b0_lo);
                const __m256 b1 = _mm256_set_m128(b1_hi, b1_lo);
                const __m256 b2 = _mm256_set_m128(b2_hi, b2_lo);

                for (int r = 0; r < 4; ++r) {
                    __m256 v = _mm256_mul_ps(a0[r], b0);
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    v = _mm256_fmadd_ps(a1[r], b1, v);
                    v = _mm256_fmadd_ps(a2[r], b2, v);
                    v = _mm256_fmadd_ps(a3[r], b3_row, v);
#else
                    v = _mm256_add_ps(v, _mm256_mul_ps(a1[r], b1));
                    v = _mm256_add_ps(v, _mm256_mul_ps(a2[r], b2));
                    v = _mm256_add_ps(v, _mm256_mul_ps(a3[r], b3_row));
#endif
                    _mm_storeu_ps(C[i + 0].m[r], _mm256_castps256_ps128(v));
                    _mm_storeu_ps(C[i + 1].m[r], _mm256_extractf128_ps(v, 1));
                }
            }

            // 端数1体
            if (i < N) {
                Matrix3x4fSoA tail = W;
                tail.count = 1;
                // 各ポインタはそのまま i オフセットで参照されるので、SSE 版にそのまま渡せる
                Mul4x4x3x4_Batch_To4x4_SSE_RowCombine_SoA(VP, tail, C + i);
            }
#endif
        }


        static inline void MakeColsFromMat4x4_SSE(const Matrix4x4f& B,
            __m128& c0, __m128& c1,
            __m128& c2, __m128& c3) noexcept
        {
            __m128 r0 = _mm_loadu_ps(&B.m[0][0]); // [b00 b01 b02 b03]
            __m128 r1 = _mm_loadu_ps(&B.m[1][0]); // [b10 b11 b12 b13]
            __m128 r2 = _mm_loadu_ps(&B.m[2][0]); // [b20 b21 b22 b23]
            __m128 r3 = _mm_loadu_ps(&B.m[3][0]); // [b30 b31 b32 b33]
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);       // c0=r0=[b00 b10 b20 b30], ...
            c0 = r0; c1 = r1; c2 = r2; c3 = r3;
        }

        // out = A4x4 * B3x4(拡張)
        static inline Matrix4x4f Mul4x4x3x4_SSE(const Matrix4x4f& A, const Matrix3x4f& B) noexcept
        {
            Matrix4x4f out;

            __m128 c0, c1, c2, c3; MakeColsFromMat3x4_SSE(B, c0, c1, c2, c3);

            const __m128 r0 = _mm_loadu_ps(A.m[0]);
            const __m128 r1 = _mm_loadu_ps(A.m[1]);
            const __m128 r2 = _mm_loadu_ps(A.m[2]);
            const __m128 r3 = _mm_loadu_ps(A.m[3]);

            auto rowDotCols = [](__m128 r, __m128 c0, __m128 c1, __m128 c2, __m128 c3) {
                const __m128 rx = _mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 ry = _mm_shuffle_ps(r, r, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 rz = _mm_shuffle_ps(r, r, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 rw = _mm_shuffle_ps(r, r, _MM_SHUFFLE(3, 3, 3, 3));
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                __m128 v = _mm_mul_ps(rx, c0);
                v = _mm_fmadd_ps(ry, c1, v);
                v = _mm_fmadd_ps(rz, c2, v);
                v = _mm_fmadd_ps(rw, c3, v);
                return v;
#else
                __m128 v = _mm_add_ps(_mm_mul_ps(rx, c0), _mm_mul_ps(ry, c1));
                v = _mm_add_ps(v, _mm_mul_ps(rz, c2));
                v = _mm_add_ps(v, _mm_mul_ps(rw, c3));
                return v;
#endif
                };

            _mm_storeu_ps(out.m[0], rowDotCols(r0, c0, c1, c2, c3));
            _mm_storeu_ps(out.m[1], rowDotCols(r1, c0, c1, c2, c3));
            _mm_storeu_ps(out.m[2], rowDotCols(r2, c0, c1, c2, c3));
            _mm_storeu_ps(out.m[3], rowDotCols(r3, c0, c1, c2, c3));

            return out;
        }

        static inline Matrix3x4f Mul3x4x4x4_To3x4_SSE(const Matrix3x4f& A,
            const Matrix4x4f& B) noexcept
        {
			Matrix3x4f C;

            __m128 c0, c1, c2, c3; MakeColsFromMat4x4_SSE(B, c0, c1, c2, c3);

            const __m128 r0 = _mm_loadu_ps(&A.m[0][0]); // [r00 r01 r02 tx]
            const __m128 r1 = _mm_loadu_ps(&A.m[1][0]); // [r10 r11 r12 ty]
            const __m128 r2 = _mm_loadu_ps(&A.m[2][0]); // [r20 r21 r22 tz]

            auto RowTimesCols = [](__m128 r, __m128 c0, __m128 c1, __m128 c2, __m128 c3) {
                const __m128 rx = _mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 ry = _mm_shuffle_ps(r, r, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 rz = _mm_shuffle_ps(r, r, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 rw = _mm_shuffle_ps(r, r, _MM_SHUFFLE(3, 3, 3, 3));
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                __m128 v = _mm_mul_ps(rx, c0);
                v = _mm_fmadd_ps(ry, c1, v);
                v = _mm_fmadd_ps(rz, c2, v);
                v = _mm_fmadd_ps(rw, c3, v);
                return v;
#else
                __m128 v = _mm_add_ps(_mm_mul_ps(rx, c0), _mm_mul_ps(ry, c1));
                v = _mm_add_ps(v, _mm_mul_ps(rz, c2));
                v = _mm_add_ps(v, _mm_mul_ps(rw, c3));
                return v;
#endif
                };

            _mm_storeu_ps(&C.m[0][0], RowTimesCols(r0, c0, c1, c2, c3));
            _mm_storeu_ps(&C.m[1][0], RowTimesCols(r1, c0, c1, c2, c3));
            _mm_storeu_ps(&C.m[2][0], RowTimesCols(r2, c0, c1, c2, c3));

			return C;
        }

        static inline void Mul4x4x3x4_Batch_SSE(const Matrix4x4f& A,
            const Matrix3x4f* B, Matrix4x4f* out,
            std::size_t count) noexcept
        {
            const __m128 r0 = _mm_loadu_ps(A.m[0]);
            const __m128 r1 = _mm_loadu_ps(A.m[1]);
            const __m128 r2 = _mm_loadu_ps(A.m[2]);
            const __m128 r3 = _mm_loadu_ps(A.m[3]);

            const __m128 r0x = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r0y = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r0z = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r0w = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(3, 3, 3, 3));
            const __m128 r1x = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r1y = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r1z = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r1w = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(3, 3, 3, 3));
            const __m128 r2x = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r2y = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r2z = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r2w = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(3, 3, 3, 3));
            const __m128 r3x = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r3y = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r3z = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r3w = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(3, 3, 3, 3));

            for (std::size_t i = 0; i < count; ++i) {
                __m128 c0, c1, c2, c3; MakeColsFromMat3x4_SSE(B[i], c0, c1, c2, c3);
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                auto dot4 = [](__m128 rx, __m128 ry, __m128 rz, __m128 rw,
                    __m128 c0, __m128 c1, __m128 c2, __m128 c3) {
                        __m128 v = _mm_mul_ps(rx, c0);
                        v = _mm_fmadd_ps(ry, c1, v);
                        v = _mm_fmadd_ps(rz, c2, v);
                        v = _mm_fmadd_ps(rw, c3, v);
                        return v;
                    };
#else
                auto dot4 = [](__m128 rx, __m128 ry, __m128 rz, __m128 rw,
                    __m128 c0, __m128 c1, __m128 c2, __m128 c3) {
                        __m128 v = _mm_add_ps(_mm_mul_ps(rx, c0), _mm_mul_ps(ry, c1));
                        v = _mm_add_ps(v, _mm_mul_ps(rz, c2));
                        v = _mm_add_ps(v, _mm_mul_ps(rw, c3));
                        return v;
                    };
#endif
                _mm_storeu_ps(out[i].m[0], dot4(r0x, r0y, r0z, r0w, c0, c1, c2, c3));
                _mm_storeu_ps(out[i].m[1], dot4(r1x, r1y, r1z, r1w, c0, c1, c2, c3));
                _mm_storeu_ps(out[i].m[2], dot4(r2x, r2y, r2z, r2w, c0, c1, c2, c3));
                _mm_storeu_ps(out[i].m[3], dot4(r3x, r3y, r3z, r3w, c0, c1, c2, c3));
            }
        }

        static inline void Mul4x4x3x4_To3x4_SSE(const Matrix4x4f& A, const Matrix3x4f& B, Matrix3x4f& out) noexcept
        {
            __m128 c0, c1, c2, c3; MakeColsFromMat3x4_SSE(B, c0, c1, c2, c3);

            const __m128 r0 = _mm_loadu_ps(A.m[0]);
            const __m128 r1 = _mm_loadu_ps(A.m[1]);
            const __m128 r2 = _mm_loadu_ps(A.m[2]);

            auto rowDotCols = [](__m128 r, __m128 c0, __m128 c1, __m128 c2, __m128 c3) {
                const __m128 rx = _mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 ry = _mm_shuffle_ps(r, r, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 rz = _mm_shuffle_ps(r, r, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 rw = _mm_shuffle_ps(r, r, _MM_SHUFFLE(3, 3, 3, 3));
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                __m128 v = _mm_mul_ps(rx, c0);
                v = _mm_fmadd_ps(ry, c1, v);
                v = _mm_fmadd_ps(rz, c2, v);
                v = _mm_fmadd_ps(rw, c3, v);
                return v;
#else
                __m128 v = _mm_add_ps(_mm_mul_ps(rx, c0), _mm_mul_ps(ry, c1));
                v = _mm_add_ps(v, _mm_mul_ps(rz, c2));
                v = _mm_add_ps(v, _mm_mul_ps(rw, c3));
                return v;
#endif
                };

            __m128 o0 = rowDotCols(r0, c0, c1, c2, c3);
            __m128 o1 = rowDotCols(r1, c0, c1, c2, c3);
            __m128 o2 = rowDotCols(r2, c0, c1, c2, c3);

            // oN = [c0 c1 c2 t] の形で来るので、そのまま row-major 3x4 に格納
            _mm_storeu_ps(&out.m[0][0], o0);
            _mm_storeu_ps(&out.m[1][0], o1);
            _mm_storeu_ps(&out.m[2][0], o2);
        }


        //==============================
        // 4x4 一般逆行列（スカラー）
         // det 近傍 0 の場合は恒等を返す（Release 安全）
        //==============================
        template<typename T>
        Matrix<4, 4, T> Inverse(const Matrix<4, 4, T>& M) noexcept {
            using std::abs;

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
            // 数値安定化: 行列要素のスケールに応じた相対しきい値
            T s = T(0);
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    s = (abs(a[i][j]) > s) ? abs(a[i][j]) : s;
            const T eps = std::numeric_limits<T>::epsilon() * T(100) * (s > T(1) ? s : T(1));
            if (abs(det) <= eps) {
                // 非正則：恒等返し（用途に応じて変更可）
                return Matrix<4, 4, T>::Identity();
            }
            const T invDet = T(1) / det;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) inv[i][j] *= invDet;
            return inv;
        }

        //==============================
        // アフィン逆行列（列ベクトル規約）
        //==============================
        inline bool IsOrthonormalRotation3x3(const Matrix4x4f& M, float eps = 1e-4f) noexcept {
            const Vec3<float> r0{ M.m[0][0],M.m[0][1],M.m[0][2] };
            const Vec3<float> r1{ M.m[1][0],M.m[1][1],M.m[1][2] };
            const Vec3<float> r2{ M.m[2][0],M.m[2][1],M.m[2][2] };
            auto near1 = [&](float v) { return std::fabs(v - 1.f) <= eps; };
            auto near0 = [&](float v) { return std::fabs(v) <= eps; };
            const bool ortho = near1(r0.dot(r0)) && near1(r1.dot(r1)) && near1(r2.dot(r2)) &&
                near0(r0.dot(r1)) && near0(r1.dot(r2)) && near0(r2.dot(r0));
            const float det = r0.dot(r1.cross(r2));
            return ortho && (std::fabs(det - 1.f) <= 2e-3f); // det≈+1 もチェック
        }

#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
        // R が正規直交（R^{-1}=R^T）想定：M=[R t; 0 1] → M^{-1}=[R^T -R^T t; 0 1]
        inline Matrix4x4f InverseAffineOrthonormal(const Matrix4x4f& M) noexcept {
            // 行を読み込んで転置→列を得る
            __m128 r0 = _mm_loadu_ps(M.m[0]);
            __m128 r1 = _mm_loadu_ps(M.m[1]);
            __m128 r2 = _mm_loadu_ps(M.m[2]);
            __m128 r3 = _mm_loadu_ps(M.m[3]);
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3); // r0=col0=[m00 m10 m20 m30], r3=col3=[tx ty tz 1]

            // R^T は行としては元の行列の上左3x3 転置
            // col0..2 の xyz をそれぞれ行に置く
            Matrix4x4f Out = Matrix4x4f::Identity();

            alignas(16) float c0[4], c1[4], c2[4], c3[4];
            _mm_storeu_ps(c0, r0);
            _mm_storeu_ps(c1, r1);
            _mm_storeu_ps(c2, r2);
            _mm_storeu_ps(c3, r3); // t = (c3[0],c3[1],c3[2])

            // 上左3x3 = R^T
            Out.m[0][0] = c0[0]; Out.m[0][1] = c1[0]; Out.m[0][2] = c2[0];
            Out.m[1][0] = c0[1]; Out.m[1][1] = c1[1]; Out.m[1][2] = c2[1];
            Out.m[2][0] = c0[2]; Out.m[2][1] = c1[2]; Out.m[2][2] = c2[2];

            // t' = -R^T * t
            const float tx = c3[0], ty = c3[1], tz = c3[2];
            Out.m[0][3] = -(Out.m[0][0] * tx + Out.m[0][1] * ty + Out.m[0][2] * tz);
            Out.m[1][3] = -(Out.m[1][0] * tx + Out.m[1][1] * ty + Out.m[1][2] * tz);
            Out.m[2][3] = -(Out.m[2][0] * tx + Out.m[2][1] * ty + Out.m[2][2] * tz);
            return Out;
        }
#endif

        inline Matrix4x4f InverseAffine(const Matrix4x4f& M) noexcept {
            // 上左3x3 を逆行列、右列の t を -R^{-1}t に
            const float a00 = M.m[0][0], a01 = M.m[0][1], a02 = M.m[0][2];
            const float a10 = M.m[1][0], a11 = M.m[1][1], a12 = M.m[1][2];
            const float a20 = M.m[2][0], a21 = M.m[2][1], a22 = M.m[2][2];

            const float det =
                a00 * (a11 * a22 - a12 * a21) -
                a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);
            const float eps = 1e-20f;
            if (std::fabs(det) <= eps) {
                return Matrix4x4f::Identity();
            }
            const float invDet = 1.0f / det;

            Matrix4x4f Rinv = Matrix4x4f::Identity();
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
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) Out.m[r][c] = Rinv.m[r][c];

            Out.m[0][3] = -(Rinv.m[0][0] * tx + Rinv.m[0][1] * ty + Rinv.m[0][2] * tz);
            Out.m[1][3] = -(Rinv.m[1][0] * tx + Rinv.m[1][1] * ty + Rinv.m[1][2] * tz);
            Out.m[2][3] = -(Rinv.m[2][0] * tx + Rinv.m[2][1] * ty + Rinv.m[2][2] * tz);
            return Out;
        }

        inline Matrix4x4f InverseFastAffine(const Matrix4x4f& M) noexcept {
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
            if (IsOrthonormalRotation3x3(M)) return InverseAffineOrthonormal(M);
#endif
            return InverseAffine(M);
        }

        //==============================
        // バッチ演算
        //==============================
        inline void Multiply4x4Batch(const Matrix4x4f* A, const Matrix4x4f* B, Matrix4x4f* C, size_t n) noexcept {
            for (size_t i = 0; i < n; ++i) C[i] = A[i] * B[i];
        }

        inline void MultiplyManyBySameRight(const Matrix4x4f* A, const Matrix4x4f& B, Matrix4x4f* C, size_t n) noexcept {
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
            const __m128 b0 = _mm_loadu_ps(B.m[0]);
            const __m128 b1 = _mm_loadu_ps(B.m[1]);
            const __m128 b2 = _mm_loadu_ps(B.m[2]);
            const __m128 b3 = _mm_loadu_ps(B.m[3]);
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
                    _mm_storeu_ps(C[idx].m[i], r);
                }
            }
#else
            for (size_t i = 0; i < n; ++i) C[i] = A[i] * B;
#endif
        }

        //==============================
        /* 点群変換（列ベクトル規約）
         * out[i] = M * [in[i].x, in[i].y, in[i].z, 1]^T の xyz を返す
         */
         //==============================
        inline void TransformPoints(const Matrix4x4f& M, const Vec3<float>* inPts, Vec3<float>* outPts, size_t n) noexcept {
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
            __m128 r0 = _mm_loadu_ps(M.m[0]);
            __m128 r1 = _mm_loadu_ps(M.m[1]);
            __m128 r2 = _mm_loadu_ps(M.m[2]);
            __m128 r3 = _mm_loadu_ps(M.m[3]);
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3); // 列を取得

            for (size_t i = 0; i < n; ++i) {
                const __m128 vx = _mm_set1_ps(inPts[i].x);
                const __m128 vy = _mm_set1_ps(inPts[i].y);
                const __m128 vz = _mm_set1_ps(inPts[i].z);

                __m128 res = _mm_mul_ps(vx, r0);
                res = detail::fmadd_ps(vy, r1, res);
                res = detail::fmadd_ps(vz, r2, res);
                res = _mm_add_ps(res, r3); // +t

                alignas(16) float t[4];
                _mm_storeu_ps(t, res);
                outPts[i].x = t[0];
                outPts[i].y = t[1];
                outPts[i].z = t[2];
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
        // 行列ビルダー (4x4) ※列ベクトル規約, 右列=平行移動
        //==============================
        template<typename T>
        constexpr Matrix<4, 4, T> MakeTranslationMatrix(const Vec3<T>& t) noexcept {
            auto m = Matrix<4, 4, T>::Identity();
            m[0][3] = t.x; m[1][3] = t.y; m[2][3] = t.z;
            return m;
        }

        template<typename T>
        constexpr Matrix<4, 4, T> MakeScalingMatrix(const Vec3<T>& s) noexcept {
            Matrix<4, 4, T> m{};
            m[0][0] = s.x; m[1][1] = s.y; m[2][2] = s.z; m[3][3] = T(1);
            return m;
        }

        template<typename T>
        constexpr Matrix<4, 4, T> MakeRotationMatrix(const Quat<T>& qIn) noexcept {
            Quat<T> q = qIn; // 正規化前提（必要なら Normalize）
            // ※ここで Normalize してもよい
            const T x = q.x, y = q.y, z = q.z, w = q.w;
            const T xx = x * x, yy = y * y, zz = z * z;
            const T xy = x * y, xz = x * z, yz = y * z;
            const T wx = w * x, wy = w * y, wz = w * z;

            Matrix<4, 4, T> m{};
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

        // LookAt（左手系, 列ベクトル, 右列=平行移動, +Z）
        template<typename T>
        inline Matrix<4, 4, T> MakeLookAtMatrixLH(const Vec3<T>& eye,
            const Vec3<T>& target,
            const Vec3<T>& up) noexcept
        {
            const Vec3<T> z = (target - eye).normalized();   // forward (+Z)
            const Vec3<T> x = up.cross(z).normalized();      // right
            const Vec3<T> y = z.cross(x);                    // up (再直交)

            Matrix<4, 4, T> m{}; // 0初期化
            // 列ベクトル規約では「行」に x/y/z の成分を横並びで置く
            m[0][0] = x.x;  m[0][1] = x.y;  m[0][2] = x.z;  m[0][3] = -x.dot(eye);
            m[1][0] = y.x;  m[1][1] = y.y;  m[1][2] = y.z;  m[1][3] = -y.dot(eye);
            m[2][0] = z.x;  m[2][1] = z.y;  m[2][2] = z.z;  m[2][3] = -z.dot(eye);
            m[3][0] = 0;    m[3][1] = 0;    m[3][2] = 0;    m[3][3] = 1;
            return m;
        }

        // LookAt（右手系, 列ベクトル, 右列=平行移動, -Z）
        template<typename T>
        inline Matrix<4, 4, T> MakeLookAtMatrixRH(const Vec3<T>& eye,
            const Vec3<T>& target,
            const Vec3<T>& up) noexcept
        {
            const Vec3<T> z = (eye - target).normalized();   // forward (−Z)
            const Vec3<T> x = up.cross(z).normalized();      // right
            const Vec3<T> y = z.cross(x);                    // up

            Matrix<4, 4, T> m{};
            m[0][0] = x.x;  m[0][1] = x.y;  m[0][2] = x.z;  m[0][3] = -x.dot(eye);
            m[1][0] = y.x;  m[1][1] = y.y;  m[1][2] = y.z;  m[1][3] = -y.dot(eye);
            m[2][0] = z.x;  m[2][1] = z.y;  m[2][2] = z.z;  m[2][3] = -z.dot(eye);
            m[3][0] = 0;    m[3][1] = 0;    m[3][2] = 0;    m[3][3] = 1;
            return m;
        }

        enum class Handedness { LH, RH };
        enum class ClipZRange { ZeroToOne, NegOneToOne }; // DX, GL

        //==============================================================
        // 透視投影（FOV）— 列ベクトル規約 / row-major / 右列=平行移動
        // 注意: w' = z_view になるように m[3][2] を ±1 に置くのが肝
        //   LH+DX[0,1]:   m[3][2] = +1
        //   RH+DX[0,1]:   m[3][2] = -1
        //   LH/RH+GL[-1,1] でも m[3][2] の ±1 は維持（z 行の係数だけ変化）
        //==============================================================
        template<Handedness H, ClipZRange Z, typename T>
        constexpr Matrix<4, 4, T>
            MakePerspectiveFovT(T fovY, T aspect, T nearZ, T farZ) noexcept
        {
            const T s = T(1) / std::tan(fovY / T(2));
            Matrix<4, 4, T> m{};          // 0 初期化
            m[0][0] = s / aspect;
            m[1][1] = s;
            // m[3][3] = 0

            if constexpr (H == Handedness::LH) {
                // ---- Left-handed (+Z forward)
                if constexpr (Z == ClipZRange::ZeroToOne) {
                    // DX [0,1]
                    m[2][2] = farZ / (farZ - nearZ);
                    m[2][3] = (-nearZ * farZ) / (farZ - nearZ);
                    m[3][2] = T(1);               // w' = z_view
                }
                else {
                    // GL [-1,1]
                    m[2][2] = (farZ + nearZ) / (farZ - nearZ);
                    m[2][3] = (-T(2) * nearZ * farZ) / (farZ - nearZ);
                    m[3][2] = T(1);
                }
            }
            else {
                // ---- Right-handed (−Z forward)
                if constexpr (Z == ClipZRange::ZeroToOne) {
                    // DX [0,1]
                    m[2][2] = farZ / (nearZ - farZ);
                    m[2][3] = (nearZ * farZ) / (nearZ - farZ);
                    m[3][2] = T(-1);              // w' = −z_view
                }
                else {
                    // GL [-1,1]
                    m[2][2] = (farZ + nearZ) / (nearZ - farZ);
                    m[2][3] = (T(2) * nearZ * farZ) / (nearZ - farZ);
                    m[3][2] = T(-1);
                }
            }
            return m;
        }

        //==============================================================
        // 直交投影 — 列ベクトル規約 / row-major / 右列=平行移動
        // ※ オルソは w' = 1 のままなので m[3][2] は 0、m[3][3] = 1
        //==============================================================
        template<Handedness H, ClipZRange Z, typename T>
        constexpr Matrix<4, 4, T>
            MakeOrthographicT(T l, T r, T b, T t, T n, T f_) noexcept
        {
            Matrix<4, 4, T> m{}; // 0 初期化
            m[0][0] = T(2) / (r - l);
            m[1][1] = T(2) / (t - b);
            m[0][3] = -(r + l) / (r - l);
            m[1][3] = -(t + b) / (t - b);
            m[3][3] = T(1);                 // ★ w' = 1

            if constexpr (H == Handedness::LH) {
                if constexpr (Z == ClipZRange::ZeroToOne) {
                    // DX [0,1]
                    m[2][2] = T(1) / (f_ - n);
                    m[2][3] = -n / (f_ - n);
                }
                else {
                    // GL [-1,1]
                    m[2][2] = T(2) / (f_ - n);
                    m[2][3] = -(f_ + n) / (f_ - n);
                }
            }
            else {
                if constexpr (Z == ClipZRange::ZeroToOne) {
                    // DX [0,1]
                    m[2][2] = T(-1) / (f_ - n);
                    m[2][3] = -n / (f_ - n);
                }
                else {
                    // GL [-1,1]
                    m[2][2] = T(-2) / (f_ - n);
                    m[2][3] = -(f_ + n) / (f_ - n);
                }
            }
            return m;
        }



        //==============================
        // ユーティリティ: 点/方向の単発変換（列ベクトル）
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
        // Matrix4x4f への SIMD アサイン支援
        //==============================
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
        inline void SetZero(Matrix4x4f& M) noexcept {
            const __m128 z = _mm_setzero_ps();
            _mm_storeu_ps(M.m[0], z);
            _mm_storeu_ps(M.m[1], z);
            _mm_storeu_ps(M.m[2], z);
            _mm_storeu_ps(M.m[3], z);
        }
        inline void SetIdentity(Matrix4x4f& M) noexcept {
            SetZero(M);
            M.m[0][0] = 1.f; M.m[1][1] = 1.f; M.m[2][2] = 1.f; M.m[3][3] = 1.f;
        }
        inline void SetRow(Matrix4x4f& M, int r, const float* row4) noexcept {
            _mm_storeu_ps(M.m[r], _mm_loadu_ps(row4));
        }
        inline void SetRow(Matrix4x4f& M, int r, __m128 v) noexcept {
            _mm_storeu_ps(M.m[r], v);
        }
        inline void LoadRowMajor(Matrix4x4f& M, const float* p16_rowMajor) noexcept {
            _mm_storeu_ps(M.m[0], _mm_loadu_ps(p16_rowMajor + 0));
            _mm_storeu_ps(M.m[1], _mm_loadu_ps(p16_rowMajor + 4));
            _mm_storeu_ps(M.m[2], _mm_loadu_ps(p16_rowMajor + 8));
            _mm_storeu_ps(M.m[3], _mm_loadu_ps(p16_rowMajor + 12));
        }
        inline void LoadColumnMajor(Matrix4x4f& M, const float* p16_colMajor) noexcept {
            __m128 c0 = _mm_loadu_ps(p16_colMajor + 0);
            __m128 c1 = _mm_loadu_ps(p16_colMajor + 4);
            __m128 c2 = _mm_loadu_ps(p16_colMajor + 8);
            __m128 c3 = _mm_loadu_ps(p16_colMajor + 12);
            _MM_TRANSPOSE4_PS(c0, c1, c2, c3);
            _mm_storeu_ps(M.m[0], c0);
            _mm_storeu_ps(M.m[1], c1);
            _mm_storeu_ps(M.m[2], c2);
            _mm_storeu_ps(M.m[3], c3);
        }
        inline void StoreRowMajor(const Matrix4x4f& M, float* dst16_rowMajor) noexcept {
            _mm_storeu_ps(dst16_rowMajor + 0, _mm_loadu_ps(M.m[0]));
            _mm_storeu_ps(dst16_rowMajor + 4, _mm_loadu_ps(M.m[1]));
            _mm_storeu_ps(dst16_rowMajor + 8, _mm_loadu_ps(M.m[2]));
            _mm_storeu_ps(dst16_rowMajor + 12, _mm_loadu_ps(M.m[3]));
        }
        inline void SetRows(Matrix4x4f& M, __m128 r0, __m128 r1, __m128 r2, __m128 r3) noexcept {
            _mm_storeu_ps(M.m[0], r0);
            _mm_storeu_ps(M.m[1], r1);
            _mm_storeu_ps(M.m[2], r2);
            _mm_storeu_ps(M.m[3], r3);
        }
        inline void Fill(Matrix4x4f& M, float v) noexcept {
            __m128 s = _mm_set1_ps(v);
            _mm_storeu_ps(M.m[0], s);
            _mm_storeu_ps(M.m[1], s);
            _mm_storeu_ps(M.m[2], s);
            _mm_storeu_ps(M.m[3], s);
        }
        // 上左3x3 を __m128×3 で入れる（w は 0）
        inline void SetUpper3x3(Matrix4x4f& M, __m128 r0, __m128 r1, __m128 r2) noexcept {
            alignas(16) float a0[4], a1[4], a2[4];
            _mm_storeu_ps(a0, r0); _mm_storeu_ps(a1, r1); _mm_storeu_ps(a2, r2);
            a0[3] = 0.f; a1[3] = 0.f; a2[3] = 0.f;
            _mm_storeu_ps(M.m[0], _mm_loadu_ps(a0));
            _mm_storeu_ps(M.m[1], _mm_loadu_ps(a1));
            _mm_storeu_ps(M.m[2], _mm_loadu_ps(a2));
        }
        inline void SetBottomRowAffine(Matrix4x4f& M) noexcept {
            _mm_storeu_ps(M.m[3], _mm_set_ps(1.f, 0.f, 0.f, 0.f)); // [0,0,0,1]
        }
#else
        inline void SetZero(Matrix4x4f& M) noexcept {
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) M.m[r][c] = 0.0f;
        }
        inline void SetIdentity(Matrix4x4f& M) noexcept {
            SetZero(M);
            M.m[0][0] = 1.0f; M.m[1][1] = 1.0f; M.m[2][2] = 1.0f; M.m[3][3] = 1.0f;
        }
        inline void SetRow(Matrix4x4f& M, int r, const float* row4) noexcept {
            for (int c = 0; c < 4; ++c) M.m[r][c] = row4[c];
        }
        inline void LoadRowMajor(Matrix4x4f& M, const float* p) noexcept {
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) M.m[r][c] = p[r * 4 + c];
        }
        inline void LoadColumnMajor(Matrix4x4f& M, const float* p) noexcept {
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) M.m[r][c] = p[c * 4 + r];
        }
        inline void StoreRowMajor(const Matrix4x4f& M, float* p) noexcept {
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p[r * 4 + c] = M.m[r][c];
        }
        inline void SetRows(Matrix4x4f& M, const float* r0, const float* r1, const float* r2, const float* r3) noexcept {
            SetRow(M, 0, r0); SetRow(M, 1, r1); SetRow(M, 2, r2); SetRow(M, 3, r3);
        }
        inline void Fill(Matrix4x4f& M, float v) noexcept {
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) M.m[r][c] = v;
        }
        inline void SetUpper3x3(Matrix4x4f& M, const float* r0, const float* r1, const float* r2) noexcept {
            M.m[0][0] = r0[0]; M.m[0][1] = r0[1]; M.m[0][2] = r0[2]; M.m[0][3] = 0.0f;
            M.m[1][0] = r1[0]; M.m[1][1] = r1[1]; M.m[1][2] = r1[2]; M.m[1][3] = 0.0f;
            M.m[2][0] = r2[0]; M.m[2][1] = r2[1]; M.m[2][2] = r2[2]; M.m[2][3] = 0.0f;
        }
        inline void SetBottomRowAffine(Matrix4x4f& M) noexcept {
            M.m[3][0] = 0.f; M.m[3][1] = 0.f; M.m[3][2] = 0.f; M.m[3][3] = 1.f;
        }
#endif

        //==============================
        // SoA からワールド行列 (M = T * R * S) を一括生成
        //==============================
        struct MTransformSoA {
            const float* px; const float* py; const float* pz;                    // translation
            const float* qx; const float* qy; const float* qz; const float* qw;   // quaternion (unit 推奨)
            const float* sx; const float* sy; const float* sz;                    // scale
        };

        // q を正規化したい場合 true
        inline void BuildWorldMatrices_FromSoA(
            const MTransformSoA& t, size_t n, SFW::Math::Matrix4x4f* outM, bool renormalizeQuat = false) noexcept
        {
#if (defined(_MSC_VER) && defined(_M_X64)) || defined(__SSE2__)
            const size_t vecN = n & ~size_t(3);
            static const __m128 two = _mm_set1_ps(2.0f);
            static const __m128 one = _mm_set1_ps(1.0f);
            static const __m128 half = _mm_set1_ps(0.5f);
            static const __m128 onept5 = _mm_set1_ps(1.5f);
            static const __m128 tiny = _mm_set1_ps(1e-20f);

            auto normalize4 = [](__m128 x, __m128 y, __m128 z, __m128 w) {
                // s = x^2 + y^2 + z^2 + w^2
                __m128 s = _mm_add_ps(_mm_mul_ps(x, x),
                    _mm_add_ps(_mm_mul_ps(y, y),
                        _mm_add_ps(_mm_mul_ps(z, z), _mm_mul_ps(w, w))));
                __m128 rinv = _mm_rsqrt_ps(s);
                // 1 回ニュートン改良: y = y*(1.5 - 0.5*s*y*y)
                __m128 yy = _mm_mul_ps(rinv, rinv);
                rinv = _mm_mul_ps(rinv, _mm_sub_ps(onept5, _mm_mul_ps(half, _mm_mul_ps(s, yy))));
                __m128 nx = _mm_mul_ps(x, rinv);
                __m128 ny = _mm_mul_ps(y, rinv);
                __m128 nz = _mm_mul_ps(z, rinv);
                __m128 nw = _mm_mul_ps(w, rinv);
                // ゼロ四元数ガード: s < tiny のレーンは (0,0,0,1) に強制
                __m128 mask = _mm_cmplt_ps(s, tiny);
                __m128 zx = _mm_setzero_ps(), zy = _mm_setzero_ps(), zz = _mm_setzero_ps(), zw = _mm_set1_ps(1.0f);
                nx = _mm_or_ps(_mm_and_ps(mask, zx), _mm_andnot_ps(mask, nx));
                ny = _mm_or_ps(_mm_and_ps(mask, zy), _mm_andnot_ps(mask, ny));
                nz = _mm_or_ps(_mm_and_ps(mask, zz), _mm_andnot_ps(mask, nz));
                nw = _mm_or_ps(_mm_and_ps(mask, zw), _mm_andnot_ps(mask, nw));
                return std::tuple{ nx, ny, nz, nw };
                };

            for (size_t i = 0; i < vecN; i += 4) {
                __m128 px = _mm_loadu_ps(t.px + i), py = _mm_loadu_ps(t.py + i), pz = _mm_loadu_ps(t.pz + i);
                __m128 qx = _mm_loadu_ps(t.qx + i), qy = _mm_loadu_ps(t.qy + i), qz = _mm_loadu_ps(t.qz + i), qw = _mm_loadu_ps(t.qw + i);
                __m128 sx = _mm_loadu_ps(t.sx + i), sy = _mm_loadu_ps(t.sy + i), sz = _mm_loadu_ps(t.sz + i);

                if (renormalizeQuat) std::tie(qx, qy, qz, qw) = normalize4(qx, qy, qz, qw);

                __m128 xx = _mm_mul_ps(qx, qx), yy = _mm_mul_ps(qy, qy), zz = _mm_mul_ps(qz, qz);
                __m128 xy = _mm_mul_ps(qx, qy), xz = _mm_mul_ps(qx, qz), yz = _mm_mul_ps(qy, qz);
                __m128 wx = _mm_mul_ps(qw, qx), wy = _mm_mul_ps(qw, qy), wz = _mm_mul_ps(qw, qz);

                __m128 r00 = _mm_sub_ps(one, _mm_mul_ps(two, _mm_add_ps(yy, zz)));
                __m128 r01 = _mm_mul_ps(two, _mm_sub_ps(xy, wz));
                __m128 r02 = _mm_mul_ps(two, _mm_add_ps(xz, wy));

                __m128 r10 = _mm_mul_ps(two, _mm_add_ps(xy, wz));
                __m128 r11 = _mm_sub_ps(one, _mm_mul_ps(two, _mm_add_ps(xx, zz)));
                __m128 r12 = _mm_mul_ps(two, _mm_sub_ps(yz, wx));

                __m128 r20 = _mm_mul_ps(two, _mm_sub_ps(xz, wy));
                __m128 r21 = _mm_mul_ps(two, _mm_add_ps(yz, wx));
                __m128 r22 = _mm_sub_ps(one, _mm_mul_ps(two, _mm_add_ps(xx, yy)));

                // 列スケール（R * diag(sx,sy,sz)）
                __m128 m00 = _mm_mul_ps(r00, sx), m01 = _mm_mul_ps(r01, sy), m02 = _mm_mul_ps(r02, sz);
                __m128 m10 = _mm_mul_ps(r10, sx), m11 = _mm_mul_ps(r11, sy), m12 = _mm_mul_ps(r12, sz);
                __m128 m20 = _mm_mul_ps(r20, sx), m21 = _mm_mul_ps(r21, sy), m22 = _mm_mul_ps(r22, sz);

                // 右列 = 平行移動
                __m128 m03 = px, m13 = py, m23 = pz;

                // AoS に散らす
                alignas(16) float a00[4], a01[4], a02[4], a10[4], a11[4], a12[4], a20[4], a21[4], a22[4];
                alignas(16) float t0[4], t1[4], t2[4];
                _mm_store_ps(a00, m00); _mm_store_ps(a01, m01); _mm_store_ps(a02, m02);
                _mm_store_ps(a10, m10); _mm_store_ps(a11, m11); _mm_store_ps(a12, m12);
                _mm_store_ps(a20, m20); _mm_store_ps(a21, m21); _mm_store_ps(a22, m22);
                _mm_store_ps(t0, m03); _mm_store_ps(t1, m13); _mm_store_ps(t2, m23);

                for (int lane = 0; lane < 4; ++lane) {
                    Matrix4x4f& M = outM[i + lane];
                    // 行0..2, 右列に t
                    _mm_storeu_ps(M.m[0], _mm_set_ps(t0[lane], a02[lane], a01[lane], a00[lane]));
                    _mm_storeu_ps(M.m[1], _mm_set_ps(t1[lane], a12[lane], a11[lane], a10[lane]));
                    _mm_storeu_ps(M.m[2], _mm_set_ps(t2[lane], a22[lane], a21[lane], a20[lane]));
                    _mm_storeu_ps(M.m[3], _mm_set_ps(1.f, 0.f, 0.f, 0.f));
                }
            }
            // 端数
            for (size_t i = vecN; i < n; ++i) {
#else
            for (size_t i = 0; i < n; ++i) {
#endif
                Quat<float> q(t.qx[i], t.qy[i], t.qz[i], t.qw[i]);
                if (renormalizeQuat) q.Normalize();
                const auto R = MakeRotationMatrix(q);

                Matrix4x4f M{};
                M.m[0][0] = R.m[0][0] * t.sx[i]; M.m[0][1] = R.m[0][1] * t.sy[i]; M.m[0][2] = R.m[0][2] * t.sz[i]; M.m[0][3] = t.px[i];
                M.m[1][0] = R.m[1][0] * t.sx[i]; M.m[1][1] = R.m[1][1] * t.sy[i]; M.m[1][2] = R.m[1][2] * t.sz[i]; M.m[1][3] = t.py[i];
                M.m[2][0] = R.m[2][0] * t.sx[i]; M.m[2][1] = R.m[2][1] * t.sy[i]; M.m[2][2] = R.m[2][2] * t.sz[i]; M.m[2][3] = t.pz[i];
                M.m[3][0] = 0.f; M.m[3][1] = 0.f; M.m[3][2] = 0.f; M.m[3][3] = 1.f;
                outM[i] = M;
            }
        }

        static inline void BuildWorldMatrices3x4_FromSoA_Scalar(
            const float* px, const float* py, const float* pz,
            const float* qx, const float* qy, const float* qz, const float* qw,
            const float* sx, const float* sy, const float* sz, // null 可（=1）
            Matrix3x4f* out, std::size_t count) noexcept
        {
            for (std::size_t i = 0; i < count; ++i) {
                const float x = qx[i], y = qy[i], z = qz[i], w = qw[i];
                const float Sx = sx ? sx[i] : 1.f;
                const float Sy = sy ? sy[i] : 1.f;
                const float Sz = sz ? sz[i] : 1.f;

                const float xx = x * x, yy = y * y, zz = z * z;
                const float xy = x * y, xz = x * z, yz = y * z;
                const float wx = w * x, wy = w * y, wz = w * z;

                // 回転（列ベクトル規約の標準式）
                float r00 = 1.f - 2.f * (yy + zz);
                float r01 = 2.f * (xy - wz);
                float r02 = 2.f * (xz + wy);

                float r10 = 2.f * (xy + wz);
                float r11 = 1.f - 2.f * (xx + zz);
                float r12 = 2.f * (yz - wx);

                float r20 = 2.f * (xz - wy);
                float r21 = 2.f * (yz + wx);
                float r22 = 1.f - 2.f * (xx + yy);

                // 列スケーリング（各列に Sx,Sy,Sz）
                r00 *= Sx; r10 *= Sx; r20 *= Sx;
                r01 *= Sy; r11 *= Sy; r21 *= Sy;
                r02 *= Sz; r12 *= Sz; r22 *= Sz;

                Matrix3x4f& M = out[i];
                M.m[0][0] = r00; M.m[0][1] = r01; M.m[0][2] = r02; M.m[0][3] = px[i];
                M.m[1][0] = r10; M.m[1][1] = r11; M.m[1][2] = r12; M.m[1][3] = py[i];
                M.m[2][0] = r20; M.m[2][1] = r21; M.m[2][2] = r22; M.m[2][3] = pz[i];
            }
        }

#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
        static inline void BuildWorldMatrices3x4_FromSoA_AVX2x8(
            const float* px, const float* py, const float* pz,
            const float* qx, const float* qy, const float* qz, const float* qw,
            const float* sx, const float* sy, const float* sz,
            Matrix3x4f* out, std::size_t n) noexcept
        {
            const __m256 one = _mm256_set1_ps(1.0f);
            const __m256 two = _mm256_set1_ps(2.0f);

            std::size_t i = 0;
            for (; i + 7 < n; i += 8) {
                __m256 X = _mm256_loadu_ps(qx + i);
                __m256 Y = _mm256_loadu_ps(qy + i);
                __m256 Z = _mm256_loadu_ps(qz + i);
                __m256 W = _mm256_loadu_ps(qw + i);

                __m256 Sx = sx ? _mm256_loadu_ps(sx + i) : _mm256_set1_ps(1.f);
                __m256 Sy = sy ? _mm256_loadu_ps(sy + i) : _mm256_set1_ps(1.f);
                __m256 Sz = sz ? _mm256_loadu_ps(sz + i) : _mm256_set1_ps(1.f);

                __m256 xx = _mm256_mul_ps(X, X);
                __m256 yy = _mm256_mul_ps(Y, Y);
                __m256 zz = _mm256_mul_ps(Z, Z);

                __m256 xy = _mm256_mul_ps(X, Y);
                __m256 xz = _mm256_mul_ps(X, Z);
                __m256 yz = _mm256_mul_ps(Y, Z);

                __m256 wx = _mm256_mul_ps(W, X);
                __m256 wy = _mm256_mul_ps(W, Y);
                __m256 wz = _mm256_mul_ps(W, Z);

                // r00 = 1 - 2(yy+zz)
                __m256 r00 = _mm256_fnmadd_ps(two, _mm256_add_ps(yy, zz), one);
                // r01 = 2(xy - wz)
                __m256 r01 = _mm256_mul_ps(two, _mm256_sub_ps(xy, wz));
                // r02 = 2(xz + wy)
                __m256 r02 = _mm256_mul_ps(two, _mm256_add_ps(xz, wy));

                // r10 = 2(xy + wz)
                __m256 r10 = _mm256_mul_ps(two, _mm256_add_ps(xy, wz));
                // r11 = 1 - 2(xx+zz)
                __m256 r11 = _mm256_fnmadd_ps(two, _mm256_add_ps(xx, zz), one);
                // r12 = 2(yz - wx)
                __m256 r12 = _mm256_mul_ps(two, _mm256_sub_ps(yz, wx));

                // r20 = 2(xz - wy)
                __m256 r20 = _mm256_mul_ps(two, _mm256_sub_ps(xz, wy));
                // r21 = 2(yz + wx)
                __m256 r21 = _mm256_mul_ps(two, _mm256_add_ps(yz, wx));
                // r22 = 1 - 2(xx+yy)
                __m256 r22 = _mm256_fnmadd_ps(two, _mm256_add_ps(xx, yy), one);

                // 列スケーリング（列ごと）
                r00 = _mm256_mul_ps(r00, Sx);
                r10 = _mm256_mul_ps(r10, Sx);
                r20 = _mm256_mul_ps(r20, Sx);

                r01 = _mm256_mul_ps(r01, Sy);
                r11 = _mm256_mul_ps(r11, Sy);
                r21 = _mm256_mul_ps(r21, Sy);

                r02 = _mm256_mul_ps(r02, Sz);
                r12 = _mm256_mul_ps(r12, Sz);
                r22 = _mm256_mul_ps(r22, Sz);

                __m256 TX = _mm256_loadu_ps(px + i);
                __m256 TY = _mm256_loadu_ps(py + i);
                __m256 TZ = _mm256_loadu_ps(pz + i);

                // out は AoS（Matrix3x4f 連続）なので、8 個ぶんストア（ギャザなしのスカラストア）
                // ループ解体で 8 回分まとめて書く
                alignas(32) float r00a[8], r01a[8], r02a[8],
                    r10a[8], r11a[8], r12a[8],
                    r20a[8], r21a[8], r22a[8],
                    TXa[8], TYa[8], TZa[8];
                _mm256_store_ps(r00a, r00); _mm256_store_ps(r01a, r01); _mm256_store_ps(r02a, r02);
                _mm256_store_ps(r10a, r10); _mm256_store_ps(r11a, r11); _mm256_store_ps(r12a, r12);
                _mm256_store_ps(r20a, r20); _mm256_store_ps(r21a, r21); _mm256_store_ps(r22a, r22);
                _mm256_store_ps(TXa, TX);  _mm256_store_ps(TYa, TY);  _mm256_store_ps(TZa, TZ);

                for (int k = 0; k < 8; ++k) {
                    Matrix3x4f& M = out[i + k];
                    M.m[0][0] = r00a[k]; M.m[0][1] = r01a[k]; M.m[0][2] = r02a[k]; M.m[0][3] = TXa[k];
                    M.m[1][0] = r10a[k]; M.m[1][1] = r11a[k]; M.m[1][2] = r12a[k]; M.m[1][3] = TYa[k];
                    M.m[2][0] = r20a[k]; M.m[2][1] = r21a[k]; M.m[2][2] = r22a[k]; M.m[2][3] = TZa[k];
                }
            }
        }
#endif

#if defined(__SSE2__) || defined(_MSC_VER)
        static inline std::size_t BuildWorldMatrices3x4_FromSoA_SSE4x(
            const float* px, const float* py, const float* pz,
            const float* qx, const float* qy, const float* qz, const float* qw,
            const float* sx, const float* sy, const float* sz,
            Matrix3x4f* out, std::size_t n) noexcept
        {
            std::size_t i = 0;
            for (; i + 3 < n; i += 4) {
                __m128 X = _mm_loadu_ps(qx + i);
                __m128 Y = _mm_loadu_ps(qy + i);
                __m128 Z = _mm_loadu_ps(qz + i);
                __m128 W = _mm_loadu_ps(qw + i);

                __m128 Sx = sx ? _mm_loadu_ps(sx + i) : _mm_set1_ps(1.f);
                __m128 Sy = sy ? _mm_loadu_ps(sy + i) : _mm_set1_ps(1.f);
                __m128 Sz = sz ? _mm_loadu_ps(sz + i) : _mm_set1_ps(1.f);

                __m128 xx = _mm_mul_ps(X, X);
                __m128 yy = _mm_mul_ps(Y, Y);
                __m128 zz = _mm_mul_ps(Z, Z);

                __m128 xy = _mm_mul_ps(X, Y);
                __m128 xz = _mm_mul_ps(X, Z);
                __m128 yz = _mm_mul_ps(Y, Z);

                __m128 wx = _mm_mul_ps(W, X);
                __m128 wy = _mm_mul_ps(W, Y);
                __m128 wz = _mm_mul_ps(W, Z);

                const __m128 one = _mm_set1_ps(1.f);
                const __m128 two = _mm_set1_ps(2.f);

                // r00 = 1 - 2(yy+zz)
                __m128 r00 = _mm_sub_ps(one, _mm_mul_ps(two, _mm_add_ps(yy, zz)));
                __m128 r01 = _mm_mul_ps(two, _mm_sub_ps(xy, wz));
                __m128 r02 = _mm_mul_ps(two, _mm_add_ps(xz, wy));

                __m128 r10 = _mm_mul_ps(two, _mm_add_ps(xy, wz));
                __m128 r11 = _mm_sub_ps(one, _mm_mul_ps(two, _mm_add_ps(xx, zz)));
                __m128 r12 = _mm_mul_ps(two, _mm_sub_ps(yz, wx));

                __m128 r20 = _mm_mul_ps(two, _mm_sub_ps(xz, wy));
                __m128 r21 = _mm_mul_ps(two, _mm_add_ps(yz, wx));
                __m128 r22 = _mm_sub_ps(one, _mm_mul_ps(two, _mm_add_ps(xx, yy)));

                // 列スケーリング
                r00 = _mm_mul_ps(r00, Sx); r10 = _mm_mul_ps(r10, Sx); r20 = _mm_mul_ps(r20, Sx);
                r01 = _mm_mul_ps(r01, Sy); r11 = _mm_mul_ps(r11, Sy); r21 = _mm_mul_ps(r21, Sy);
                r02 = _mm_mul_ps(r02, Sz); r12 = _mm_mul_ps(r12, Sz); r22 = _mm_mul_ps(r22, Sz);

                __m128 TX = _mm_loadu_ps(px + i);
                __m128 TY = _mm_loadu_ps(py + i);
                __m128 TZ = _mm_loadu_ps(pz + i);

                alignas(16) float r00a[4], r01a[4], r02a[4],
                    r10a[4], r11a[4], r12a[4],
                    r20a[4], r21a[4], r22a[4],
                    TXa[4], TYa[4], TZa[4];
                _mm_store_ps(r00a, r00); _mm_store_ps(r01a, r01); _mm_store_ps(r02a, r02);
                _mm_store_ps(r10a, r10); _mm_store_ps(r11a, r11); _mm_store_ps(r12a, r12);
                _mm_store_ps(r20a, r20); _mm_store_ps(r21a, r21); _mm_store_ps(r22a, r22);
                _mm_store_ps(TXa, TX);  _mm_store_ps(TYa, TY);  _mm_store_ps(TZa, TZ);

                for (int k = 0; k < 4; ++k) {
                    Matrix3x4f& M = out[i + k];
                    M.m[0][0] = r00a[k]; M.m[0][1] = r01a[k]; M.m[0][2] = r02a[k]; M.m[0][3] = TXa[k];
                    M.m[1][0] = r10a[k]; M.m[1][1] = r11a[k]; M.m[1][2] = r12a[k]; M.m[1][3] = TYa[k];
                    M.m[2][0] = r20a[k]; M.m[2][1] = r21a[k]; M.m[2][2] = r22a[k]; M.m[2][3] = TZa[k];
                }
            }
            return i; // 消化した件数
        }
#endif

        //--------------------------------------------------------------
        // フロント API
        //--------------------------------------------------------------
        static inline void BuildWorldMatrices3x4_FromSoA(
            const float* px, const float* py, const float* pz,
            const float* qx, const float* qy, const float* qz, const float* qw,
            const float* sx, const float* sy, const float* sz, // null 可
            Matrix3x4f* out, std::size_t count) noexcept
        {
            std::size_t done = 0;

#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
            if (count >= 8) {
                BuildWorldMatrices3x4_FromSoA_AVX2x8(px, py, pz, qx, qy, qz, qw, sx, sy, sz, out, count);
                // AVX2x8 内で端数処理はしないので、ここで何件残ったかを再計算
                done = (count / 8) * 8;
            }
#endif

#if defined(__SSE2__) || defined(_MSC_VER)
            if (done < count) {
                done += BuildWorldMatrices3x4_FromSoA_SSE4x(px + done, py + done, pz + done,
                    qx + done, qy + done, qz + done, qw + done,
                    sx ? sx + done : nullptr,
                    sy ? sy + done : nullptr,
                    sz ? sz + done : nullptr,
                    out + done, count - done);
            }
#endif
            if (done < count) {
                BuildWorldMatrices3x4_FromSoA_Scalar(px + done, py + done, pz + done,
                    qx + done, qy + done, qz + done, qw + done,
                    sx ? sx + done : nullptr,
                    sy ? sy + done : nullptr,
                    sz ? sz + done : nullptr,
                    out + done, count - done);
            }
        }

        static inline void BuildWorldMatrices3x4_FromSoA(
			const MTransformSoA& t, std::size_t n, Matrix3x4f* out) noexcept
		{
			BuildWorldMatrices3x4_FromSoA(t.px, t.py, t.pz,
				t.qx, t.qy, t.qz, t.qw,
				t.sx, t.sy, t.sz,
				out, n);
		}


        //======================================================================
        // 列ベクトル規約 × 行優先行列：点の変換（clip = M * [x,y,z,1]^T）
        //======================================================================
        inline void MulPoint_RowMajor_ColVec(const Matrix4x4f& M,
            float x, float y, float z,
            float& cx, float& cy, float& cz, float& cw) noexcept
        {
            // row-major 保存だが、計算は「行と列のドット」：clip = row_i · vec4
            cx = M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3] * 1.0f;
            cy = M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3] * 1.0f;
            cz = M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3] * 1.0f;
            cw = M[3][0] * x + M[3][1] * y + M[3][2] * z + M[3][3] * 1.0f;
        }

        //----------------------------------------------
        // ユーティリティ: 3x4 => 列ベクトル4本(c0..c3)を __m128 で構築
        //----------------------------------------------
        static inline void MakeColsFromWorld3x4_SSE(const Matrix3x4f& W,
            __m128& c0, __m128& c1,
            __m128& c2, __m128& c3) noexcept
        {
            // 行をロード
            const __m128 r0 = _mm_loadu_ps(&W.m[0][0]); // [r00 r01 r02 t0]
            const __m128 r1 = _mm_loadu_ps(&W.m[1][0]); // [r10 r11 r12 t1]
            const __m128 r2 = _mm_loadu_ps(&W.m[2][0]); // [r20 r21 r22 t2]
            const __m128 z = _mm_setzero_ps();         // [0 0 0 0]
            // 4x4 転置で列を作る（最後の行は [0 0 0 1] を後で設定）
            __m128 c0r = r0, c1r = r1, c2r = r2, c3r = z;
            _MM_TRANSPOSE4_PS(c0r, c1r, c2r, c3r);      // ここで c0r=[r00 r10 r20 0], c1r=[r01 r11 r21 0]...
            c0 = c0r;                                   // [r00 r10 r20 0]
            c1 = c1r;                                   // [r01 r11 r21 0]
            c2 = c2r;                                   // [r02 r12 r22 0]
            // c3 = [t0 t1 t2 1]
            c3 = _mm_set_ps(1.0f, W.m[2][3], W.m[1][3], W.m[0][3]);
        }

        //----------------------------------------------
        // 単発: Mat4f out = VP * World3x4
        //----------------------------------------------
        static inline void MulVPxWorld3x4_SSE(const Matrix4x4f& VP, const Matrix3x4f& W, Matrix4x4f& out) noexcept
        {
            __m128 c0, c1, c2, c3;
            MakeColsFromWorld3x4_SSE(W, c0, c1, c2, c3);

            const __m128 r0 = _mm_loadu_ps(VP.m[0]);
            const __m128 r1 = _mm_loadu_ps(VP.m[1]);
            const __m128 r2 = _mm_loadu_ps(VP.m[2]);
            const __m128 r3 = _mm_loadu_ps(VP.m[3]);

            auto mad4 = [](__m128 a, __m128 b, __m128 c, __m128 d, __m128 e) {
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                // (((a*b)+c*d)+e*f) 形式で FMA 使用
                __m128 ab = _mm_mul_ps(a, b);
                __m128 cd = _mm_fmadd_ps(c, d, ab);
                return _mm_add_ps(cd, e);
#else
                __m128 ab = _mm_mul_ps(a, b);
                __m128 cd = _mm_add_ps(ab, _mm_mul_ps(c, d));
                return _mm_add_ps(cd, e);
#endif
                };

            // out.row(i) = r_i.xxxx*c0 + r_i.yyyy*c1 + r_i.zzzz*c2 + r_i.wwww*c3
            const __m128 r0x = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r0y = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r0z = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r0w = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(3, 3, 3, 3));
            __m128 o0 = mad4(r0x, c0, r0y, c1, _mm_add_ps(_mm_mul_ps(r0z, c2), _mm_mul_ps(r0w, c3)));

            const __m128 r1x = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r1y = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r1z = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r1w = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(3, 3, 3, 3));
            __m128 o1 = mad4(r1x, c0, r1y, c1, _mm_add_ps(_mm_mul_ps(r1z, c2), _mm_mul_ps(r1w, c3)));

            const __m128 r2x = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r2y = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r2z = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r2w = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(3, 3, 3, 3));
            __m128 o2 = mad4(r2x, c0, r2y, c1, _mm_add_ps(_mm_mul_ps(r2z, c2), _mm_mul_ps(r2w, c3)));

            const __m128 r3x = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r3y = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r3z = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r3w = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(3, 3, 3, 3));
            __m128 o3 = mad4(r3x, c0, r3y, c1, _mm_add_ps(_mm_mul_ps(r3z, c2), _mm_mul_ps(r3w, c3)));

            _mm_storeu_ps(out.m[0], o0);
            _mm_storeu_ps(out.m[1], o1);
            _mm_storeu_ps(out.m[2], o2);
            _mm_storeu_ps(out.m[3], o3);
        }

        //----------------------------------------------
        // バッチ: 同一 VP × N 個の World3x4 -> N 個の Mat4f
        // worlds は連続（sizeof(World3x4f) ごと）
        //----------------------------------------------
        static inline void MulVPxWorld3x4_Batch_SSE(const Matrix4x4f& VP,
            const Matrix3x4f* worlds,
            Matrix4x4f* out, std::size_t count) noexcept
        {
            const __m128 r0 = _mm_loadu_ps(VP.m[0]);
            const __m128 r1 = _mm_loadu_ps(VP.m[1]);
            const __m128 r2 = _mm_loadu_ps(VP.m[2]);
            const __m128 r3 = _mm_loadu_ps(VP.m[3]);

            for (std::size_t i = 0; i < count; ++i) {
                __m128 c0, c1, c2, c3;
                MakeColsFromWorld3x4_SSE(worlds[i], c0, c1, c2, c3);

                auto madd2 = [](__m128 a, __m128 b, __m128 c, __m128 d) {
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    return _mm_fmadd_ps(a, b, _mm_mul_ps(c, d));
#else
                    return _mm_add_ps(_mm_mul_ps(a, b), _mm_mul_ps(c, d));
#endif
                    };

                const __m128 r0x = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 r0y = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 r0z = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 r0w = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(3, 3, 3, 3));
                __m128 o0 = _mm_add_ps(madd2(r0x, c0, r0y, c1), _mm_add_ps(_mm_mul_ps(r0z, c2), _mm_mul_ps(r0w, c3)));

                const __m128 r1x = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 r1y = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 r1z = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 r1w = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(3, 3, 3, 3));
                __m128 o1 = _mm_add_ps(madd2(r1x, c0, r1y, c1), _mm_add_ps(_mm_mul_ps(r1z, c2), _mm_mul_ps(r1w, c3)));

                const __m128 r2x = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 r2y = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 r2z = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 r2w = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(3, 3, 3, 3));
                __m128 o2 = _mm_add_ps(madd2(r2x, c0, r2y, c1), _mm_add_ps(_mm_mul_ps(r2z, c2), _mm_mul_ps(r2w, c3)));

                const __m128 r3x = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 r3y = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 r3z = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 r3w = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(3, 3, 3, 3));
                __m128 o3 = _mm_add_ps(madd2(r3x, c0, r3y, c1), _mm_add_ps(_mm_mul_ps(r3z, c2), _mm_mul_ps(r3w, c3)));

                _mm_storeu_ps(out[i].m[0], o0);
                _mm_storeu_ps(out[i].m[1], o1);
                _mm_storeu_ps(out[i].m[2], o2);
                _mm_storeu_ps(out[i].m[3], o3);
            }
        }

        //----------------------------------------------
        // （任意）AVX2 4個まとめ最適化版（配置: World3x4f が AoS で連続）
        // gather を避け、4つずつ手作業で列を組みます。
        // メモリ帯域が許せば SSE 版でも十分高速なため、必要な時だけ使ってください。
        //----------------------------------------------
        static inline void MulVPxWorld3x4_Batch4_AVX2(const Matrix4x4f& VP,
            const Matrix3x4f* worlds,
            Matrix4x4f* out, std::size_t count) noexcept
        {
#if !defined(__AVX2__) && !(defined(_MSC_VER) && defined(__AVX2__))
            // 環境が AVX2 でなければ SSE 版にフォールバック
            return MulVPxWorld3x4_Batch_SSE(VP, worlds, out, count);
#else
            const __m128 r0 = _mm_loadu_ps(VP.m[0]);
            const __m128 r1 = _mm_loadu_ps(VP.m[1]);
            const __m128 r2 = _mm_loadu_ps(VP.m[2]);
            const __m128 r3 = _mm_loadu_ps(VP.m[3]);

            auto dot4 = [](__m128 rx, __m128 ry, __m128 rz, __m128 rw,
                __m128 c0, __m128 c1, __m128 c2, __m128 c3) {
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    __m128 v = _mm_mul_ps(rx, c0);
                    v = _mm_fmadd_ps(ry, c1, v);
                    v = _mm_fmadd_ps(rz, c2, v);
                    v = _mm_fmadd_ps(rw, c3, v);
                    return v;
#else
                    __m128 v = _mm_add_ps(_mm_mul_ps(rx, c0), _mm_mul_ps(ry, c1));
                    v = _mm_add_ps(v, _mm_mul_ps(rz, c2));
                    v = _mm_add_ps(v, _mm_mul_ps(rw, c3));
                    return v;
#endif
                };

            const __m128 r0x = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r0y = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r0z = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r0w = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(3, 3, 3, 3));

            const __m128 r1x = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r1y = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r1z = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r1w = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(3, 3, 3, 3));

            const __m128 r2x = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r2y = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r2z = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r2w = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(3, 3, 3, 3));

            const __m128 r3x = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(0, 0, 0, 0));
            const __m128 r3y = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(1, 1, 1, 1));
            const __m128 r3z = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(2, 2, 2, 2));
            const __m128 r3w = _mm_shuffle_ps(r3, r3, _MM_SHUFFLE(3, 3, 3, 3));

            std::size_t i = 0;
            for (; i + 3 < count; i += 4) {
                // 4つ分の World から列 c0..c3 を4本ずつ構築 → 4回 dot
                __m128 c0[4], c1[4], c2[4], c3[4];
                for (int k = 0; k < 4; ++k) {
                    MakeColsFromWorld3x4_SSE(worlds[i + k], c0[k], c1[k], c2[k], c3[k]);
                }

                __m128 o0 = dot4(r0x, r0y, r0z, r0w, c0[0], c1[0], c2[0], c3[0]);
                __m128 o1 = dot4(r1x, r1y, r1z, r1w, c0[0], c1[0], c2[0], c3[0]);
                __m128 o2 = dot4(r2x, r2y, r2z, r2w, c0[0], c1[0], c2[0], c3[0]);
                __m128 o3 = dot4(r3x, r3y, r3z, r3w, c0[0], c1[0], c2[0], c3[0]);
                _mm_storeu_ps(out[i + 0].m[0], o0);
                _mm_storeu_ps(out[i + 0].m[1], o1);
                _mm_storeu_ps(out[i + 0].m[2], o2);
                _mm_storeu_ps(out[i + 0].m[3], o3);

                o0 = dot4(r0x, r0y, r0z, r0w, c0[1], c1[1], c2[1], c3[1]);
                o1 = dot4(r1x, r1y, r1z, r1w, c0[1], c1[1], c2[1], c3[1]);
                o2 = dot4(r2x, r2y, r2z, r2w, c0[1], c1[1], c2[1], c3[1]);
                o3 = dot4(r3x, r3y, r3z, r3w, c0[1], c1[1], c2[1], c3[1]);
                _mm_storeu_ps(out[i + 1].m[0], o0);
                _mm_storeu_ps(out[i + 1].m[1], o1);
                _mm_storeu_ps(out[i + 1].m[2], o2);
                _mm_storeu_ps(out[i + 1].m[3], o3);

                o0 = dot4(r0x, r0y, r0z, r0w, c0[2], c1[2], c2[2], c3[2]);
                o1 = dot4(r1x, r1y, r1z, r1w, c0[2], c1[2], c2[2], c3[2]);
                o2 = dot4(r2x, r2y, r2z, r2w, c0[2], c1[2], c2[2], c3[2]);
                o3 = dot4(r3x, r3y, r3z, r3w, c0[2], c1[2], c2[2], c3[2]);
                _mm_storeu_ps(out[i + 2].m[0], o0);
                _mm_storeu_ps(out[i + 2].m[1], o1);
                _mm_storeu_ps(out[i + 2].m[2], o2);
                _mm_storeu_ps(out[i + 2].m[3], o3);

                o0 = dot4(r0x, r0y, r0z, r0w, c0[3], c1[3], c2[3], c3[3]);
                o1 = dot4(r1x, r1y, r1z, r1w, c0[3], c1[3], c2[3], c3[3]);
                o2 = dot4(r2x, r2y, r2z, r2w, c0[3], c1[3], c2[3], c3[3]);
                o3 = dot4(r3x, r3y, r3z, r3w, c0[3], c1[3], c2[3], c3[3]);
                _mm_storeu_ps(out[i + 3].m[0], o0);
                _mm_storeu_ps(out[i + 3].m[1], o1);
                _mm_storeu_ps(out[i + 3].m[2], o2);
                _mm_storeu_ps(out[i + 3].m[3], o3);
            }
            if (i < count) {
                MulVPxWorld3x4_Batch_SSE(VP, worlds + i, out + i, count - i);
            }
#endif
        }

        //==============================
        // TransformSoA → WorldMatrixSoA
        //==============================
        // normalizeQ = true なら軽量正規化（rsqrt）を入れる
        inline void BuildWorldMatrixSoA_FromTransformSoA(
            const MTransformSoA& in, Matrix3x4fSoA& M, bool normalizeQ = true) noexcept
        {
            if (M.count == 0) return;

            // 必須入力チェック（px/py/pz + qx/qy/qz/qw は必須）
            if (!in.px || !in.py || !in.pz || !in.qx || !in.qy || !in.qz || !in.qw) return;
            // 出力チェック
            if (!M.m00 || !M.m01 || !M.m02 || !M.tx ||
                !M.m10 || !M.m11 || !M.m12 || !M.ty ||
                !M.m20 || !M.m21 || !M.m22 || !M.tz) return;

#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
            const __m256 one = _mm256_set1_ps(1.0f);
            const __m256 two = _mm256_set1_ps(2.0f);

            std::size_t i = 0;
            for (; i + 7 < M.count; i += 8) {
                // 8体ロード
                __m256 X = _mm256_loadu_ps(in.qx + i);
                __m256 Y = _mm256_loadu_ps(in.qy + i);
                __m256 Z = _mm256_loadu_ps(in.qz + i);
                __m256 W = _mm256_loadu_ps(in.qw + i);

                // スケール（未指定なら 1）
                __m256 Sx = in.sx ? _mm256_loadu_ps(in.sx + i) : one;
                __m256 Sy = in.sy ? _mm256_loadu_ps(in.sy + i) : one;
                __m256 Sz = in.sz ? _mm256_loadu_ps(in.sz + i) : one;

                // 軽量正規化（品質が必要ならニュートン1回追加も可）
                if (normalizeQ) {
// n2 = X*X + Y*Y + Z*Z + W*W
#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
                    __m256 t = _mm256_fmadd_ps(Z, Z, _mm256_mul_ps(W, W));  // t = Z*Z + W*W
                    t = _mm256_fmadd_ps(Y, Y, t);                   // t += Y*Y
                    __m256 n2 = _mm256_fmadd_ps(X, X, t);                   // n2 = X*X + t
#else
                    __m256 n2 = _mm256_add_ps(_mm256_mul_ps(X, X), _mm256_mul_ps(Y, Y));
                    n2 = _mm256_add_ps(n2, _mm256_mul_ps(Z, Z));
                    n2 = _mm256_add_ps(n2, _mm256_mul_ps(W, W));
#endif

                    __m256 rinv = _mm256_rsqrt_ps(n2); // 1/sqrt(n2) の近似

                    // 必要ならニュートン1回で精度アップ: rinv = rinv*(1.5 - 0.5*n2*rinv*rinv)
#if 1
                    const __m256 half = _mm256_set1_ps(0.5f);
                    const __m256 three_halfs = _mm256_set1_ps(1.5f);
                    __m256 rinv2 = _mm256_mul_ps(rinv, rinv);
                    __m256 corr = _mm256_fnmadd_ps(_mm256_mul_ps(n2, rinv2), half, three_halfs);
                    rinv = _mm256_mul_ps(rinv, corr);
#endif

                    X = _mm256_mul_ps(X, rinv);
                    Y = _mm256_mul_ps(Y, rinv);
                    Z = _mm256_mul_ps(Z, rinv);
                    W = _mm256_mul_ps(W, rinv);
                }

                // 回転 3x3
                __m256 xx = _mm256_mul_ps(X, X), yy = _mm256_mul_ps(Y, Y), zz = _mm256_mul_ps(Z, Z);
                __m256 xy = _mm256_mul_ps(X, Y), xz = _mm256_mul_ps(X, Z), yz = _mm256_mul_ps(Y, Z);
                __m256 wx = _mm256_mul_ps(W, X), wy = _mm256_mul_ps(W, Y), wz = _mm256_mul_ps(W, Z);

                __m256 r00 = _mm256_fnmadd_ps(two, _mm256_add_ps(yy, zz), one);
                __m256 r01 = _mm256_mul_ps(two, _mm256_sub_ps(xy, wz));
                __m256 r02 = _mm256_mul_ps(two, _mm256_add_ps(xz, wy));

                __m256 r10 = _mm256_mul_ps(two, _mm256_add_ps(xy, wz));
                __m256 r11 = _mm256_fnmadd_ps(two, _mm256_add_ps(xx, zz), one);
                __m256 r12 = _mm256_mul_ps(two, _mm256_sub_ps(yz, wx));

                __m256 r20 = _mm256_mul_ps(two, _mm256_sub_ps(xz, wy));
                __m256 r21 = _mm256_mul_ps(two, _mm256_add_ps(yz, wx));
                __m256 r22 = _mm256_fnmadd_ps(two, _mm256_add_ps(xx, yy), one);

                // 列スケーリング
                r00 = _mm256_mul_ps(r00, Sx); r10 = _mm256_mul_ps(r10, Sx); r20 = _mm256_mul_ps(r20, Sx);
                r01 = _mm256_mul_ps(r01, Sy); r11 = _mm256_mul_ps(r11, Sy); r21 = _mm256_mul_ps(r21, Sy);
                r02 = _mm256_mul_ps(r02, Sz); r12 = _mm256_mul_ps(r12, Sz); r22 = _mm256_mul_ps(r22, Sz);

                // 並進
                __m256 TX = _mm256_loadu_ps(in.px + i);
                __m256 TY = _mm256_loadu_ps(in.py + i);
                __m256 TZ = _mm256_loadu_ps(in.pz + i);

                // SoA なのでそのままベクトルストア
                _mm256_storeu_ps(M.m00 + i, r00); _mm256_storeu_ps(M.m01 + i, r01); _mm256_storeu_ps(M.m02 + i, r02); _mm256_storeu_ps(M.tx + i, TX);
                _mm256_storeu_ps(M.m10 + i, r10); _mm256_storeu_ps(M.m11 + i, r11); _mm256_storeu_ps(M.m12 + i, r12); _mm256_storeu_ps(M.ty + i, TY);
                _mm256_storeu_ps(M.m20 + i, r20); _mm256_storeu_ps(M.m21 + i, r21); _mm256_storeu_ps(M.m22 + i, r22); _mm256_storeu_ps(M.tz + i, TZ);
            }
            // テイル（スカラ）
            for (; i < M.count; ++i) {
                float X = in.qx[i], Y = in.qy[i], Z = in.qz[i], W = in.qw[i];
                if (normalizeQ) {
                    float n2 = X * X + Y * Y + Z * Z + W * W; float rinv = 1.0f / std::sqrt(n2);
                    X *= rinv; Y *= rinv; Z *= rinv; W *= rinv;
                }
                float sx = in.sx ? in.sx[i] : 1.f;
                float sy = in.sy ? in.sy[i] : 1.f;
                float sz = in.sz ? in.sz[i] : 1.f;

                float xx = X * X, yy = Y * Y, zz = Z * Z;
                float xy = X * Y, xz = X * Z, yz = Y * Z;
                float wx = W * X, wy = W * Y, wz = W * Z;

                float r00 = 1 - 2 * (yy + zz), r01 = 2 * (xy - wz), r02 = 2 * (xz + wy);
                float r10 = 2 * (xy + wz), r11 = 1 - 2 * (xx + zz), r12 = 2 * (yz - wx);
                float r20 = 2 * (xz - wy), r21 = 2 * (yz + wx), r22 = 1 - 2 * (xx + yy);

                M.m00[i] = r00 * sx; M.m01[i] = r01 * sy; M.m02[i] = r02 * sz; M.tx[i] = in.px[i];
                M.m10[i] = r10 * sx; M.m11[i] = r11 * sy; M.m12[i] = r12 * sz; M.ty[i] = in.py[i];
                M.m20[i] = r20 * sx; M.m21[i] = r21 * sy; M.m22[i] = r22 * sz; M.tz[i] = in.pz[i];
            }
#else
            // AVX2 なし：スカラのみ
            for (std::size_t i = 0; i < count; ++i) {
                float X = in.qx[i], Y = in.qy[i], Z = in.qz[i], W = in.qw[i];
                if (normalizeQ) {
                    float n2 = X * X + Y * Y + Z * Z + W * W; float rinv = 1.0f / std::sqrt(n2);
                    X *= rinv; Y *= rinv; Z *= rinv; W *= rinv;
                }
                float sx = in.sx ? in.sx[i] : 1.f;
                float sy = in.sy ? in.sy[i] : 1.f;
                float sz = in.sz ? in.sz[i] : 1.f;

                float xx = X * X, yy = Y * Y, zz = Z * Z;
                float xy = X * Y, xz = X * Z, yz = Y * Z;
                float wx = W * X, wy = W * Y, wz = W * Z;

                float r00 = 1 - 2 * (yy + zz), r01 = 2 * (xy - wz), r02 = 2 * (xz + wy);
                float r10 = 2 * (xy + wz), r11 = 1 - 2 * (xx + zz), r12 = 2 * (yz - wx);
                float r20 = 2 * (xz - wy), r21 = 2 * (yz + wx), r22 = 1 - 2 * (xx + yy);

                out.m00[i] = r00 * sx; out.m01[i] = r01 * sy; out.m02[i] = r02 * sz; out.tx[i] = in.px[i];
                out.m10[i] = r10 * sx; out.m11[i] = r11 * sy; out.m12[i] = r12 * sz; out.ty[i] = in.py[i];
                out.m20[i] = r20 * sx; out.m21[i] = r21 * sy; out.m22[i] = r22 * sz; out.tz[i] = in.pz[i];
            }
#endif
        }


    } // namespace Math
} // namespace SectorFW

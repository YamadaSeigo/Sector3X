#include "Graphics/LODPolicy.h"
#include <limits>

namespace SectorFW::Graphics {

    static inline float clamp01(float x) { return (std::min)((std::max)(x, 0.0f), 1.0f); }
    static inline float lg10(float x) { return std::log10((std::max)(1.0f, x)); }

    LodThresholdsPx BuildLodThresholdsPx(const LodAssetStats& a, int lodCount, int baseW, int baseH)
    {
        LodThresholdsPx th{};
        th.baseW = baseW; th.baseH = baseH;

        // base[ ] は “基準解像度での画面面積比” のラフ目安（LOD0/1/2の境界）
        constexpr float baseFrac[3] = { 0.10f, 0.05f, 0.01f };

        // 係数 k：たくさん出る/遠近幅広い→早めに落とす（大きく）
        float perfPush =
            0.10f * std::clamp(lg10(float((std::max)(1u, a.instancesPeak))), 0.0f, 2.0f) +
            0.08f * std::clamp(lg10((std::max)(1.0f, a.viewMax / (std::max)(0.5f, a.viewMin))), 0.0f, 2.0f);

        // 品質寄り要因：ヒーロー/スキン/カットアウト → 粘る（小さく）
        float qualPull =
            (a.hero ? 0.15f : 0.0f) +
            (a.skinned ? 0.10f : 0.0f) +
            (a.alphaCutout ? 0.05f : 0.0f);

        float k = std::clamp(1.0f + perfPush - qualPull, 0.6f, 1.6f);

        // 画面面積比 → 基準解像度ピクセルへ変換
        const float basePixels = float(baseW * baseH);
        for (int i = 0; i < 3; ++i) {
            float depthMul = 1.0f + 0.05f * i; // 深い LOD ほど少し厳しく
            float frac = std::clamp(baseFrac[i] * k * depthMul, 0.005f, 0.6f);
            th.Tpx[i] = frac * basePixels;
        }
        th.Tpx[3] = 0.0f; // 番兵
        if (a.hero) { th.hysteresisUp = 0.20f; th.hysteresisDown = 0.12f; }

        return th;
    }

    int SelectLodByPixels(float ndcAreaFrac, const LodThresholdsPx& th, int lodCount, int prevLod,
        int renderW, int renderH, float globalBias)
    {
        if (lodCount <= 1) return 0;

        // 実ピクセル → 基準解像度換算ピクセル sP
        float P = CoveragePixelsFromNdcArea(ndcAreaFrac, renderW, renderH);
        float sP = ToBasePixels(P, renderW, renderH, th.baseW, th.baseH);

        // globalBias：段数バイアス（±1段 ≒ しきい値×2^±1 の感覚）
        float biasScale = std::pow(2.0f, globalBias);

        auto T = [&](int i, bool up) {
            float h = up ? (1.0f + th.hysteresisUp) : (1.0f - th.hysteresisDown);
            // 深い LOD ほど少しだけ調整（同じ感覚を踏襲）
            return th.Tpx[i] * biasScale * (1.0f - 0.1f * i) * h;
            };

        bool goingUp = (prevLod > 0 && sP > th.Tpx[prevLod - 1]);

        if (sP > T(0, goingUp)) return 0;
        if (lodCount == 2)     return 1;

        if (sP > T(1, goingUp)) return 1;
        if (lodCount == 3)      return 2;

        if (sP > T(2, goingUp)) return 2;
        return (std::min)(lodCount - 1, 3);
    }

    Extents ExtentsFromAABB(const Math::AABB3f& aabb)
    {
        // size() = ub - lb が使える前提
        const auto s = aabb.size(); // Vec3f
        Extents e;
        e.ex = 0.5f * (std::max)(s.x, 0.0f);
        e.ey = 0.5f * (std::max)(s.y, 0.0f);
        e.ez = 0.5f * (std::max)(s.z, 0.0f);
        return e;
    }

    // LOD しきい値（基準解像度 Px）に対して sP が“近い”か
    static bool NearAnyLodBoundary_BasePx(float sP, const LodThresholdsPx& th, int lodCount, float bandFrac)
    {
        const int lastIdx = (std::max)(0, (std::min)(lodCount - 2, 2)); // 0..2 まで
        for (int i = 0; i <= lastIdx; ++i) {
            const float T = th.Tpx[i];
            const float band = (std::max)(1.0f, T * bandFrac); // 最低 1px
            if (std::fabs(sP - T) <= band) return true;
        }
        return false;
    }

    RefineState EvaluateRefineState(const Math::NdcRectWithW& sphereRect,
        float ndcAreaFrac,
        int renderW, int renderH,
        float zCam, float nearZ,
        const Extents& e,
        const LodThresholdsPx& lodPx,
        int lodCount,
        const LodRefinePolicy& policy)
    {
        RefineState st{};

        if (!sphereRect.valid || sphereRect.wmin <= 0.0f) {
            return st; // 無効 or 裏側 → 再投影不要
        }

        // 実ピクセル → 基準解像度換算ピクセル
        ndcAreaFrac = clamp01(ndcAreaFrac);
        const float P = ndcAreaFrac * float(renderW * renderH);
        const float sP = ToBasePixels(P, renderW, renderH, policy.baseW, policy.baseH);

        // 1) 中間帯（曖昧帯域）
        if (sP > policy.midBandMinPxBase && sP < policy.midBandMaxPxBase) {
            st.reasons |= RefineReason::MidBand;
        }

        // 2) 細長い AABB
        const float emax = (std::max)({ e.ex, e.ey, e.ez });
        const float emin = (std::max)(1e-6f, (std::min)({ e.ex, e.ey, e.ez }));
        if (emax / emin >= policy.elongationRatio) {
            st.reasons |= RefineReason::Elongated;
        }

        // 3) 近クリップ近傍
        if (zCam <= (std::max)(nearZ, 1e-6f) * policy.nearClipMul) {
            st.reasons |= RefineReason::NearClip;
        }

        // 4) NDC 端に接触
        const float ax = (std::max)(std::fabs(sphereRect.xmin), std::fabs(sphereRect.xmax));
        const float ay = (std::max)(std::fabs(sphereRect.ymin), std::fabs(sphereRect.ymax));
        if (ax >= policy.edgeNdcAbs || ay >= policy.edgeNdcAbs) {
            st.reasons |= RefineReason::NearEdge;
        }

        // 5) LOD 境界付近
        if (NearAnyLodBoundary_BasePx(sP, lodPx, lodCount, policy.lodBoundaryBandFrac)) {
            st.reasons |= RefineReason::LodBoundary;
        }

        return st;
    }

} // namespace

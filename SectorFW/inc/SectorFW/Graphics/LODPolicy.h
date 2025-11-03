#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>


#include "../Math/Rectangle.hpp"
#include "../Math/AABB.hpp"

namespace SFW::Graphics {

    static inline constexpr int BASE_SCREEN_WIDTH = 1920;
	static inline constexpr int BASE_SCREEN_HEIGHT = 1080;

    struct LodThresholdsPx {
        // 「基準解像度」でのピクセル面積しきい値（LOD0→1→2…へ落ちる境界）
        // 例: Tpx[0] を超えると LOD0、Tpx[1] < P ≤ Tpx[0] で LOD1 …
        std::array<float, 4> Tpx{}; // 使うのは lodCount-1 個
        float hysteresisUp = 0.15f; // 粗→細 で厳しめ（%換算の感覚）
        float hysteresisDown = 0.01f; // 細→粗 で甘め
        // 基準解像度（正規化用）
        int baseW = 1920;
        int baseH = 1080;
    };

    struct LodAssetStats {
        uint32_t vertices = 0;
        uint32_t instancesPeak = 1;
        float viewMin = 0.0f, viewMax = 100.0f;
        bool skinned = false;
        bool alphaCutout = false;
        bool hero = false;
    };

    // 球→AABB の「再投影」を行うべきかどうかの判定パラメータ
    struct LodRefinePolicy {
        // 基準解像度（ピクセル面積正規化用）
        int baseW = BASE_SCREEN_WIDTH;
        int baseH = BASE_SCREEN_HEIGHT;

        // 球のNDCから計算した「実ピクセル面積 P（renderW*renderH 基準）」を
        // 基準解像度に換算したときの“曖昧帯域（中間帯）”
        // 例：1080p基準で 400px ～ 10000px の間は AABB に再投影して確定取り
        float midBandMinPxBase = 400.0f;
        float midBandMaxPxBase = 10000.0f;

        // 追加トリガ
        float elongationRatio = 3.0f;   // 細長いAABBの比率閾値（max/min）
        float nearClipMul = 2.0f;   // zCam <= nearZ * nearClipMul なら近クリップ近傍扱い
        float edgeNdcAbs = 0.95f;  // NDC 端（|x| or |y| がこの値以上）に触れたら再投影

        // LOD 境界またぎ付近（ヒステリシス目的で AABB 確定取得）
        // “A は NDC 面積比（0..1）”で与える。±幅で境界付近を検出。
        float lodBoundaryBandFrac = 0.20f; // 境界の ±20% を再投影帯にする
    };

    // 画面占有率（NDC 矩形面積比 A∈[0,1]）→ ピクセル面積 P へ変換
    inline float CoveragePixelsFromNdcArea(float ndcAreaFrac, int renderW, int renderH) {
        ndcAreaFrac = std::clamp(ndcAreaFrac, 0.0f, 1.0f);
        return ndcAreaFrac * float(renderW * renderH);
    }

    // レンダ解像度→基準解像度へのスケール
    inline float PixelScaleToBase(int renderW, int renderH, int baseW, int baseH) {
        return float(renderW * renderH) / float(baseW * baseH);
    }

    // P（実ピクセル）→ 基準解像度換算ピクセル Pbase
    inline float ToBasePixels(float P, int renderW, int renderH, int baseW, int baseH) {
        const float s = PixelScaleToBase(renderW, renderH, baseW, baseH);
        return (s > 0.f) ? (P / s) : P;
    }

    // LOD しきい値（ピクセル基準）をビルド（Asset に応じて自動調整）
    LodThresholdsPx BuildLodThresholdsPx(const LodAssetStats& a, int lodCount, int baseW = 1920, int baseH = 1080);

    // LOD 選択：引数は “NDC 面積比” と現在の解像度
    int SelectLodByPixels(float ndcAreaFrac, const LodThresholdsPx& thPx,
        int lodCount, int prevLod,
        int renderW, int renderH,
        float globalBias /*±段*/, float* outSp = nullptr);

    struct Extents { float ex, ey, ez; };

    Extents ExtentsFromAABB(const Math::AABB3f& aabb);

    // 再計算理由のビットマスク
    enum class RefineReason : uint32_t {
        None = 0,
        MidBand = 1 << 0, // 画面占有の“曖昧帯域”
        Elongated = 1 << 1, // AABB が細長い（球の過大評価が大きい）
        NearClip = 1 << 2, // 近クリップ近傍
        NearEdge = 1 << 3, // NDC 端（±1 付近）
        LodBoundary = 1 << 4  // LOD しきい値の近傍（確定取りしたい）
    };

    // ビット演算子
    inline RefineReason operator|(RefineReason a, RefineReason b) {
        return static_cast<RefineReason>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline RefineReason& operator|=(RefineReason& a, RefineReason b) { return a = (a | b); }
    inline bool any(RefineReason r) { return static_cast<uint32_t>(r) != 0; }

    // 呼び出し側が使う軽量ステート
    struct RefineState {
        RefineReason reasons = RefineReason::None;  // 1つ以上立っていれば再計算推奨
        inline bool shouldRefine() const { return any(reasons); }
    };

    /**
	 * @brief LOD 用の「球→AABB 再投影」を行うべきか評価する
	 * @param sphereRect 球の NDC 矩形（wmin > 0 なら表側）
	 * @param ndcAreaFrac 球の NDC 面積比（(x1-x0)*(y1-y0)/4 を [0,1] にクランプした値）
	 * @param renderW レンダ解像度幅
	 * @param renderH レンダ解像度高さ
	 * @param zCam カメラ空間Z（正値、球中心）(ZDepth)
	 * @param nearZ カメラ近クリップ距離（正値）
	 * @param lodCount 利用する LOD 数
	 * @param policy 判定ポリシー（省略時はデフォルト構築）
     */
    RefineState EvaluateRefineState(
        const Math::NdcRectWithW& sphereRect,
        float ndcAreaFrac,
        int renderW, int renderH,
        float zCam, float nearZ,
        const Extents& aabbExtents,
        const LodThresholdsPx& lodPx,
        int lodCount,
        const LodRefinePolicy& policy = {}
    );

} // namespace

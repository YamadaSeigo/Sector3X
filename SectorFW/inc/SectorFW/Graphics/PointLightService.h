// PointLightService.hpp
#pragma once
#include <cstdint>
#include <vector>
#include <shared_mutex>
#include <algorithm>
#include <cmath>
#include <cassert>

#include "../Core/ECS/ServiceContext.hpp"

namespace SFW::Graphics
{
    /// 世代付きハンドル（refcount無し）
    struct PointLightHandle
    {
        uint32_t index = 0xFFFFFFFFu;
        uint32_t generation = 0;

        bool IsValid() const noexcept { return index != 0xFFFFFFFFu; }
        friend bool operator==(const PointLightHandle& a, const PointLightHandle& b) noexcept {
            return a.index == b.index && a.generation == b.generation;
        }
    };

    // ゲーム側のライトパラメータ
    struct PointLightDesc
    {
        Math::Vec3f positionWS = { 0,0,0 };
        Math::Vec3f color = { 1,1,1 };
        float  intensity = 1.0f;   // 明るさ
        float  range = 10.0f;  // 影響距離
        bool   castsShadow = false;
    };

    // GPU転送用（例：StructuredBuffer向け）
    // ※あなたのシェーダー定義に合わせて変更してOK
    struct alignas(16) GpuPointLight
    {
		GpuPointLight() = default;
        GpuPointLight(
            const Math::Vec3f& posWS,
            float range_,
            const Math::Vec3f& color_,
            float intensity_,
            uint32_t flags_)
            : positionWS(posWS)
            , range(range_)
            , color(color_)
            , intensity(intensity_)
            , flags(flags_)
		{
		}

        Math::Vec3f positionWS; float range;
        Math::Vec3f color;      float intensity;
        uint32_t flags;    uint32_t _pad0[3];
        // flags bit0: castsShadow など
    };

    class PointLightService
    {
    public:
        static inline constexpr uint32_t MAX_POINT_LIGHT_NUM = 1u << 10;

        enum DirtyFlags : uint32_t
        {
            Dirty_None = 0,
            Dirty_Pos = 1u << 0,
            Dirty_Params = 1u << 1,
            Dirty_All = Dirty_Pos | Dirty_Params,
        };

        PointLightService() = default;

        // 予約（オープンワールドは事前reserveが効く）
        void Reserve(uint32_t capacity);

        // 生成/破棄
        PointLightHandle Create(const PointLightDesc& desc);
        void Destroy(PointLightHandle h);

        // 検証
        bool IsValid(PointLightHandle h) const;

        // 更新（dirtyを立てる）
        void SetPosition(PointLightHandle h, const Math::Vec3f& posWS);
        void SetParams(PointLightHandle h, const Math::Vec3f& color, float intensity, float range, bool castsShadow);

        // 取得（読み取り）
        PointLightDesc Get(PointLightHandle h) const;

        /**
         * @brief 読み取り用ロックを取得する関数
         * @return std::shared_lock<std::shared_mutex> 取得した読み取り用ロック
         */
        [[nodiscard]] std::shared_lock<std::shared_mutex> AcquireReadLock() const {
            return std::shared_lock<std::shared_mutex>(m_mtx);
        }
        /**
         * @brief 書き込み用ロックを取得する関数
         * @return std::unique_lock<std::shared_mutex> 取得した書き込み用ロック
         */
        [[nodiscard]] std::unique_lock<std::shared_mutex> AcquireWriteLock() {
            return std::unique_lock<std::shared_mutex>(m_mtx);
        }

        // 取得（読み取り）
        PointLightDesc GetNoLock(PointLightHandle h) const;

        // 毎フレーム：GPUへ渡すライト配列を作る
        // - まずは “全ライト” を詰めて返すシンプル版
        // - あとで「カメラ周辺だけ」「Clustered用にタイル分け」などに発展させやすい
        void BuildGpuLights(std::vector<GpuPointLight>& out) const;

        // dirty範囲だけ欲しい場合（UpdateSubresource/Mapの最適化用）
        // ※ここでは「dirty index一覧」を返す方式にしておく（範囲化は後で可能）
        void CollectDirtyIndices(std::vector<uint32_t>& outDirtyIndices);
        void ClearDirty();

        // シャドウ対象候補を取る（例：近い順にN個）
        void CollectShadowCandidatesNear(
            const Math::Vec3f& cameraPosWS,
            uint32_t maxCount,
            std::vector<PointLightHandle>& out) const;

        uint32_t AliveCount() const noexcept { return m_aliveCount; }
        uint32_t Capacity()  const noexcept { return (uint32_t)m_alive.size(); }

    private:
        struct Slot
        {
            PointLightDesc desc{};
            uint32_t dirty = Dirty_All;
            bool alive = false;
        };

    private:
        // 内部ユーティリティ（ロック済み前提）
        bool isValidNoLock(PointLightHandle h) const;

    private:
        mutable std::shared_mutex m_mtx;

        std::vector<Slot>     m_slots;
        std::vector<uint32_t> m_generation; // indexごとの世代
        std::vector<uint32_t> m_freeList;   // 空きスロット
        std::vector<uint8_t>  m_alive;      // aliveフラグ（Slotにもあるが簡易参照用）

        uint32_t m_aliveCount = 0;

        // dirty index一覧（「どこが変わったか」）
        // Create/Destroy/Updateで push、Collect/Clearで消す
        std::vector<uint32_t> m_dirtyIndices;
    public:
        STATIC_SERVICE_TAG
    };

}
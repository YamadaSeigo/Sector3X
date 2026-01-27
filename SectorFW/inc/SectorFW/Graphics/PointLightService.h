// PointLightService.hpp
#pragma once
#include <cstdint>
#include <vector>
#include <shared_mutex>
#include <algorithm>
#include <cmath>
#include <cassert>

#include "../Core/ECS/ServiceContext.hpp"
#include "RenderTypes.h"

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
			, invRange(1.0f / range)
			, flags(flags_)
		{
		}

		Math::Vec3f positionWS = {}; float range = {};
		Math::Vec3f color = {};      float intensity = {};
		float invRange = {};
		uint32_t flags = {};    uint32_t _pad0[2] = {};
		// flags bit0: castsShadow など
	};

	class PointLightService : public ECS::IUpdateService
	{
	public:
		static inline constexpr uint32_t MAX_FRAME_POINTLIGHT = 1u << 8;

		PointLightService() = default;

		void PreUpdate(double deltaTime) override;

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

		/**
		 * @brief 指定した点光源を表示リストに追加する
		 * @param h　追加する点光源ハンドル
		 * @return　追加に成功したら true、失敗なら false
		 */
		bool PushShowHandle(PointLightHandle h);

		const GpuPointLight* BuildGpuLights(uint32_t& outCount) noexcept;

		uint32_t AliveCount() const noexcept { return m_aliveCount; }
		uint32_t Capacity()  const noexcept { return (uint32_t)m_alive.size(); }

	private:
		struct Slot
		{
			PointLightDesc desc{};
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

		size_t m_frameIndex = 0;
		uint32_t m_showIndex[2][MAX_FRAME_POINTLIGHT] = {};
		std::atomic<uint32_t > m_showCount[2] = { 0,0 };

		GpuPointLight m_gpuLights[RENDER_BUFFER_COUNT][MAX_FRAME_POINTLIGHT] = {};

	public:
		STATIC_SERVICE_TAG
	};
}
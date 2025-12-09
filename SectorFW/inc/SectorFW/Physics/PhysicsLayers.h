/*****************************************************************//**
 * @file   PhysicsLayers.h
 * @brief 物理レイヤーの定義ヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <cstdint>

namespace SFW {
	namespace Physics {
		// タイトル都合で差し替え前提の最小セット
		namespace Layers {
			// ObjectLayer（用途別に増やせる）
			inline static constexpr JPH::ObjectLayer NON_MOVING = 0;
			inline static constexpr JPH::ObjectLayer MOVING = 1;
			inline static constexpr JPH::ObjectLayer SENSOR = 2;
			inline static constexpr uint32_t NUM_LAYERS = 3;

			// BroadPhaseLayer（小さめの集合分類）
			class BPLayers final {
			public:
				inline static constexpr JPH::BroadPhaseLayer NON_MOVING = JPH::BroadPhaseLayer(0);
				inline static constexpr JPH::BroadPhaseLayer MOVING = JPH::BroadPhaseLayer(1);
				inline static constexpr JPH::BroadPhaseLayer SENSOR = JPH::BroadPhaseLayer(2);
				inline static constexpr uint32_t NUM_LAYERS = 3;
			};
		}

		// BroadPhaseLayer -> マスクビット の変換
		inline constexpr BroadPhaseLayerMask MakeBPMask(JPH::BroadPhaseLayer layer)
		{
			return 1u << (JPH::uint8)layer;
		}

		// ObjectLayer -> マスクビット の変換
		inline constexpr ObjectLayerMask MakeObjectLayerMask(JPH::ObjectLayer layer)
		{
			return 1u << (ObjectLayerMask)layer;
		}


		// ObjectLayer → BroadPhaseLayer の対応
		class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
		public:
			BroadPhaseLayerInterfaceImpl() {
				//ObjectLayerをBroadPhaseLayerに変換するマッピングを設定
				m_object_to_broad[(size_t)Layers::NON_MOVING] = Layers::BPLayers::NON_MOVING;
				m_object_to_broad[(size_t)Layers::MOVING] = Layers::BPLayers::MOVING;
				m_object_to_broad[(size_t)Layers::SENSOR] = Layers::BPLayers::SENSOR;
			}
			virtual uint32_t GetNumBroadPhaseLayers() const override { return Layers::BPLayers::NUM_LAYERS; }
			virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
				return m_object_to_broad[(size_t)layer];
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
				switch (layer) {
				case Layers::BPLayers::NON_MOVING: return "NON_MOVING";
				case Layers::BPLayers::MOVING:     return "MOVING";
				case Layers::BPLayers::SENSOR:     return "SENSOR";
				default: return "UNKNOWN";
				}
			}
#endif
		private:
			JPH::BroadPhaseLayer m_object_to_broad[Layers::NUM_LAYERS]{};
		};

		// ObjectLayer × BroadPhaseLayer の粗フィルタ
		class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
		public:
			virtual bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override {
				if (layer1 == Layers::SENSOR) return layer2 == Layers::BPLayers::MOVING; // sensorは動く物体のみ検出
				// 非移動は移動と、移動は非移動＆移動と
				if (layer1 == Layers::NON_MOVING) return layer2 == Layers::BPLayers::MOVING;
				if (layer1 == Layers::MOVING)     return layer2 == Layers::BPLayers::NON_MOVING || layer2 == Layers::BPLayers::MOVING;
				return true;
			}
		};

		// ObjectLayer × ObjectLayer の詳細フィルタ
		class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
		public:
			virtual bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
				if (a == Layers::SENSOR || b == Layers::SENSOR) return true; // センサーは何でも当たる（Narrow側で無反発に）
				if (a == Layers::NON_MOVING && b == Layers::NON_MOVING) return false; // 静的×静的は不要
				return true;
			}
		};

		class BroadPhaseLayerFilterMask final : public JPH::BroadPhaseLayerFilter
		{
		public:
			explicit BroadPhaseLayerFilterMask(BroadPhaseLayerMask mask) : mMask(mask) {}

			bool ShouldCollide(JPH::BroadPhaseLayer inLayer) const override
			{
				return (mMask & (1u << (BroadPhaseLayerMask)inLayer.GetValue())) != 0;
			}

		private:
			BroadPhaseLayerMask mMask;
		};

		class ObjectLayerFilterMask final : public JPH::ObjectLayerFilter
		{
		public:
			explicit ObjectLayerFilterMask(ObjectLayerMask mask) : mMask(mask) {}

			bool ShouldCollide(JPH::ObjectLayer inLayer) const override
			{
				return (mMask & (1u << (ObjectLayerMask)inLayer)) != 0;
			}

		private:
			ObjectLayerMask mMask;
		};

		// 自分自身（自キャラ）の Body を除外する BodyFilter
		class RayBodyFilterIgnoreSelf final : public JPH::BodyFilter
		{
		public:
			explicit RayBodyFilterIgnoreSelf(JPH::BodyID self)
				: mSelf(self)
			{
			}

			// BodyID だけで判定する版
			bool ShouldCollide(const JPH::BodyID& inBodyID) const override
			{
				// 自分の BodyID は無視
				return inBodyID != mSelf;
			}

			// Body の中身を見て判定する版（必要なら）
			bool ShouldCollideLocked(const JPH::Body& inBody) const override
			{
				// 例えば UserData で "Trigger" を除外するなどもここで可能
				// auto tag = static_cast<MyTag>(inBody.GetUserData());
				// if (tag == MyTag::Trigger) return false;
				return inBody.GetID() != mSelf;
			}

		private:
			JPH::BodyID mSelf;
		};
	}
}
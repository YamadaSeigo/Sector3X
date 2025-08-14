// PhysicsCommands.h
#pragma once
#include <cstdint>
#include <variant>
#include <vector>
#include <atomic>
#include <optional>

#include "Core/ECS/entity.h"
#include "Math/Quaternion.hpp"

namespace SectorFW
{
	namespace Physics
	{
		typedef ECS::EntityID Entity; // EntityID を PhysicsDevice 流儀で使う
		typedef Math::Vec3f Vec3f; // Jolt Vec3f 流儀で使う
		typedef Math::Quatf Quatf; // Jolt Quatf 流儀で使う

		struct ShapeHandle { uint32_t index{ 0 }; uint32_t generation{ 0 }; }; // ResourceManagerBase流儀

		struct Mat34f {
			Vec3f pos; Quatf rot; // スケールは Jolt 側の ShapeScale で扱う前提
		};

		// ========= コマンド =========

		// 生成
		struct CreateBodyCmd {
			Entity	e;
			ShapeHandle shape;
			Mat34f     worldTM;
			bool       kinematic{ false };
			float      density{ 1000.0f };
			uint16_t   layer{ 0 };
			uint16_t   broadphase{ 0 };
			float      friction{ 0.6f };
			float      restitution{ 0.0f };
		};

		// 破棄
		struct DestroyBodyCmd {
			Entity e;
		};

		// テレポート（強制ワープ）
		struct TeleportCmd {
			Entity e;
			Mat34f worldTM;
			bool   wake{ true };
		};

		// 速度設定 / インパルス
		struct SetLinearVelocityCmd { Entity e; Vec3f v; };
		struct SetAngularVelocityCmd { Entity e; Vec3f w; };
		struct AddImpulseCmd { Entity e; Vec3f impulse; Vec3f atWorldPos; bool useAtPos{ false }; };

		// キネマティック目標姿勢（運動学主体）
		struct SetKinematicTargetCmd { Entity e; Mat34f worldTM; };

		// 衝突マスクやレイヤ
		struct SetCollisionMaskCmd { Entity e; uint32_t mask; };
		struct SetObjectLayerCmd { Entity e; uint16_t layer; uint16_t broadphase; };

		// RayCast（結果はイベントで返す想定）
		struct RayCastCmd {
			uint32_t requestId;
			Vec3f    origin;
			Vec3f    dir;   // 正規化前提
			float    maxDist;
		};

		// まとめ
		using PhysicsCommand = std::variant<
			CreateBodyCmd, DestroyBodyCmd, TeleportCmd,
			SetLinearVelocityCmd, SetAngularVelocityCmd, AddImpulseCmd,
			SetKinematicTargetCmd, SetCollisionMaskCmd, SetObjectLayerCmd,
			RayCastCmd
		>;

		struct BoxDesc { Vec3f halfExtents; };
		struct SphereDesc { float radius; };
		struct CapsuleDesc { float halfHeight; float radius; };

		// 三角メッシュ（凸性チェック・BVHは Jolt 側）
		struct MeshDesc {
			std::vector<Vec3f> vertices;
			std::vector<uint32_t> indices; // 3*i の連続三角形
		};

		// 高さマップ
		struct HeightFieldDesc {
			int sizeX{ 0 }, sizeY{ 0 };                // サンプル数（格子点）
			std::vector<float> samples;            // sizeX*sizeY
			float scaleY{ 1.0f };                    // 高さスケール
			float cellSizeX{ 1.0f }, cellSizeY{ 1.0f }; // セル間隔
		};

		struct ConvexHullDesc {
			std::vector<Vec3f> points;  // 凸包頂点候補（重複OK・Jolt側で整理）
			float maxConvexRadius = 0.05f; // シュリンク半径（狭間隔クエリのロバスト性向上）
			float hullTolerance = 0.005f;
		};

		using ShapeDesc = std::variant<
			BoxDesc, SphereDesc, CapsuleDesc, MeshDesc, HeightFieldDesc, ConvexHullDesc
		>;

		// 非一様スケールに対応したいときに使う
		struct ShapeScale {
			Vec3f s{ 1,1,1 }; // (1,1,1) ならスケール無し
		};

		struct ShapeCreateDesc {
			ShapeDesc   shape;
			ShapeScale  scale;     // オプション
			// 必要ならマテリアル等を追加
		};
	}
}

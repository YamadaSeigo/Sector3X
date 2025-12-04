/*****************************************************************//**
 * @file   PhysicsTypes.h
 * @brief PhysicsDeviceで使う型を定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include <cstdint>
#include <variant>
#include <vector>
#include <atomic>
#include <optional>

#include "../Core/ECS/entity.h"
#include "../Math/Quaternion.hpp"
#include "../Core/RegistryTypes.h"

namespace SFW
{
	namespace Physics
	{
		typedef ECS::EntityID Entity; // EntityID を PhysicsDevice 流儀で使う
		typedef Math::Vec3f Vec3f; // Jolt Vec3f 流儀で使う
		typedef Math::Quatf Quatf; // Jolt Quatf 流儀で使う

		/**
		 * @brief 形状ハンドル（ShapeHandle）
		 */
		struct ShapeHandle { uint32_t index{ 0 }; uint32_t generation{ 0 }; }; // ResourceManagerBase流儀

		struct Mat34f {
			Vec3f pos; Quatf rot; // スケールは Jolt 側の ShapeScale で扱う前提
		};

		// ========= コマンド =========

		/**
		 * @brief 剛体生成コマンド(CreateBodyCmd)
		 */
		struct CreateBodyCmd {
			Entity      e{};
			SpatialChunkKey  owner{};     // ManagerKey に拡張
			ShapeHandle shape;
			Mat34f     worldTM;
			float      density{ 1000.0f };
			uint16_t   layer{ 0 };
			uint8_t    broadphase{ 0 };
			bool       kinematic{ false };
			float      friction{ 0.6f };
			float      restitution{ 0.0f };
		};

		/**
		 * @brief 剛体破棄コマンド(DestroyBodyCmd)
		 */
		struct DestroyBodyCmd {
			Entity e;
		};
		/**
		 * @brief テレポートコマンド（強制ワープ）
		 */
		struct TeleportCmd {
			Entity e;
			bool   wake{ true };
			Mat34f worldTM;
		};

		/**
		 * @brief 速度設定
		 */
		struct SetLinearVelocityCmd { Entity e; Vec3f v; };
		/**
		 * @brief 角速度設定
		 */
		struct SetAngularVelocityCmd { Entity e; Vec3f w; };
		/**
		 * @brief インパルス付与 (atWorldPos 指定はオプション)
		 */
		struct AddImpulseCmd { Entity e; Vec3f impulse; Vec3f atWorldPos; bool useAtPos{ false }; };

		/**
		 * @brief キネマティック目標姿勢（運動学主体）
		 */
		struct SetKinematicTargetCmd { Entity e; Mat34f worldTM; };

		/**
		 * @brief 衝突マスクやレイヤ
		 */
		struct SetCollisionMaskCmd { Entity e; uint32_t mask; };
		/**
		 * @brief オブジェクトレイヤ（衝突グループ）変更
		 */
		struct SetObjectLayerCmd { Entity e; uint16_t layer; uint16_t broadphase; };

		/**
		 * @brief RayCast（結果はイベントで返す想定）
		 */
		struct RayCastCmd {
			uint32_t requestId;
			Vec3f    origin;
			Vec3f    dir;   // 正規化前提
			float    maxDist;
		};

		// プレイヤーキャラ生成コマンド
		struct CreateCharacterCmd {
			Entity e;
			ShapeHandle shape;   // カプセルなど
			Mat34f worldTM;      // 初期位置 + 回転
			uint16_t objectLayer;   // キャラ用 ObjectLayer
			float maxSlopeDeg = 45.0f;
		};

		// キャラの線形速度を設定
		struct SetCharacterVelocityCmd {
			Entity e;
			Vec3f  v;
		};

		// キャラの向きを設定（Y軸回転だけで良いなら yaw でもOK）
		struct SetCharacterRotationCmd {
			Entity e;
			Quatf  rot;
		};

		// 必要ならテレポートも
		struct TeleportCharacterCmd {
			Entity e;
			Mat34f worldTM;
		};

		/**
		 * @brief PhysicsCommand（コマンド総合型）
		 */
		using PhysicsCommand = std::variant<
			CreateBodyCmd, DestroyBodyCmd, TeleportCmd,
			SetLinearVelocityCmd, SetAngularVelocityCmd, AddImpulseCmd,
			SetKinematicTargetCmd, SetCollisionMaskCmd, SetObjectLayerCmd,
			RayCastCmd, CreateCharacterCmd, SetCharacterVelocityCmd,
			SetCharacterRotationCmd, TeleportCharacterCmd
		>;

		// ========= 形状記述子群 =========
		struct BoxDesc { Vec3f halfExtents; };
		struct SphereDesc { float radius; };
		struct CapsuleDesc { float halfHeight; float radius; };

		// 三角メッシュ（凸性チェック・BVHは Jolt 側）
		struct MeshDesc {
			std::vector<Vec3f> vertices;
			std::vector<uint32_t> indices; // 3*i の連続三角形
		};

		/**
		 * @brief 高さマップ
		 */
		struct HeightFieldDesc {
			int sizeX{ 0 }, sizeY{ 0 };                // サンプル数（格子点）
			std::vector<float> samples;            // sizeX*sizeY
			float scaleY{ 1.0f };                    // 高さスケール
			float cellSizeX{ 1.0f }, cellSizeY{ 1.0f }; // セル間隔
		};
		/**
		 * @brief 凸包
		 */
		struct ConvexHullDesc {
			const std::vector<Vec3f>& points;  // 凸包頂点候補（重複OK・Jolt側で整理）
			float maxConvexRadius = 0.05f; // シュリンク半径（狭間隔クエリのロバスト性向上）
			float hullTolerance = 0.005f;
		};
		/**
		 * @brief ShapeDesc（形状記述子総合型）
		 */
		using ShapeDesc = std::variant<
			BoxDesc, SphereDesc, CapsuleDesc, MeshDesc, HeightFieldDesc, ConvexHullDesc
		>;

		/**
		 * @brief 非一様スケールに対応したいときに使う
		 */
		struct ShapeScale {
			Vec3f s{ 1,1,1 }; // (1,1,1) ならスケール無し
			ShapeScale() = default;
			ShapeScale(Vec3f _s) :s(_s) {}
		};
		/**
		 * @brief 形状生成記述子(ShapeCreateDesc)
		 */
		struct ShapeCreateDesc {
			ShapeDesc   shape;
			ShapeScale  scale;     // オプション
			 //物理シェイプのローカルオフセット（ボディ原点からのシフト）
			Vec3f localOffset{ 0.0f, 0.0f, 0.0f };
			// 物理シェイプのローカル回転（ボディローカル）
			Quatf localRotation = Quatf::Identity(); // なければ {0,0,0,1} でもOK
			// 必要ならマテリアル等を追加
		};
	}
}

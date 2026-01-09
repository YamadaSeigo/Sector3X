/*****************************************************************//**
 * @file   PhysicsComponent.h
 * @brief 物理コンポーネントを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../Math/Vector.hpp"
#include "../Math/Quaternion.hpp"

#include "../Core/ECS/component.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "../Util/TypeChecker.hpp"

namespace SFW
{
	/**
	 * @brief JPH::BodyIDをプリミティブ型として扱うための特殊化
	 */
	template<> struct user_primitive<JPH::BodyID> : std::true_type {};

	namespace Physics
	{
		/**
		 * @brief 物理補間コンポーネント
		 */
		struct PhysicsInterpolation {
			union {
				struct {
					Math::Vec3f prevPos, currPos; // 位置
				};
				struct {
					float ppx, ppy, ppz, cpx, cpy, cpz; // 位置（x, y, z）
				};
			};

			uint32_t lastUpdatedFrame = 0; // 同期漏れ判定に使用

			union {
				struct {
					Math::Quatf prevRot, currRot; // 回転
				};
				struct {
					float prx, pry, prz, prw, crx, cry, crz, crw; // 回転（x, y, z, w）
				};
			};

			PhysicsInterpolation() :
				prevPos(0.0f, 0.0f, 0.0f), currPos(0.0f, 0.0f, 0.0f),
				prevRot(0.0f, 0.0f, 0.0f, 1.0f), currRot(0.0f, 0.0f, 0.0f, 1.0f) {
			}
			explicit PhysicsInterpolation(Vec3f pos, Quatf rot) :
				prevPos(pos), currPos(pos),
				prevRot(rot), currRot(rot) {
			}
			explicit PhysicsInterpolation(Vec3f ppos, Vec3f cpos, Quatf prot, Quatf crot) :
				prevPos(ppos), currPos(cpos),
				prevRot(prot), currRot(crot) {
			}
			explicit PhysicsInterpolation(float ppx, float ppy, float ppz, float cpx, float cpy, float cpz,
				float prx, float pry, float prz, float prw, float crx, float cry, float crz, float crw) :
				ppx(ppx), ppy(ppy), ppz(ppz), cpx(cpx), cpy(cpy), cpz(cpz),
				prx(prx), pry(pry), prz(prz), prw(prw), crx(crx), cry(cry), crz(crz), crw(crw) {
			}

			DEFINE_SOA(PhysicsInterpolation, ppx, ppy, ppz, cpx, cpy, cpz,
				lastUpdatedFrame, prx, pry, prz, prw, crx, cry, crz, crw)
		};
		/**
		 * @brief 物理ボディのタイプ
		 */
		enum class BodyType : uint8_t {
			Static = 1,
			Dynamic = 0,
		};

		/**
		 * @brief 物理ボディコンポーネント
		 */
		struct CPhyBody {
			static constexpr uint16_t invalidLayer = 0xFFFFu;

			JPH::BodyID body = JPH::BodyID((std::numeric_limits<uint32_t>::max)());   // 生成後にセット（読み取り用）
			uint16_t    layer = invalidLayer;  // 所属ワールド
			bool        kinematic{ false };
			BodyType     type{ BodyType::Dynamic }; // 1: 静的、0: 動的（キネマティック含む）

			DEFINE_SOA(CPhyBody, body, layer, kinematic, type)
		};
		/**
		 * @brief 形状寸法コンポーネント
		 */
		struct ShapeDims {
			enum Type : uint8_t {
				Box			= 0,
				Sphere		= 1,
				Capsule		= 2,
				Cylinder	= 3,
				Tapered		= 4,
				Mesh		= 5,
				CMHC		= 6,
			};

			// 代表寸法（例）：x,y,z = 幅/高さ/奥行 or 直径など
			Math::Vec3f dims;
			// 形状固有の値
			float r = 0.0f;           // 半径など
			float halfHeight = 0.0f;  // 半高さ

			// --- DecoratedShape 用のローカル変換 ---
			Math::Vec3f localOffset{ 0.0f, 0.0f, 0.0f };   // RotatedTranslated の position
			float localRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; //アライメントの都合で配列で持つ

			ShapeHandle handle{}; // 元の形状ハンドル（参照用）

			bool hasLocalTransform = false; // RotatedTranslated 等で飾られているか

			uint8_t type = Type::Box;
		};
	}
}

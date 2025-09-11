// PhysicsShapeManager.h
#pragma once

#include "IShapeResolver.h"
#include "../Util/ResouceManagerBase.hpp"
#include "PhysicsComponent.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

#include <variant>
#include <vector>
#include <cstdint>

namespace SectorFW
{
	namespace Physics
	{
		inline bool IsUniformScale(const Vec3f& s, float eps = 1e-6f) {
			return std::fabs(s.x - s.y) <= eps && std::fabs(s.y - s.z) <= eps;
		}

		// 非一様スケールが来た場合、max 要素で一様化する（安全寄り）
		inline Vec3f EnforceUniformScale(const Vec3f& s) {
			float u = (std::max)(s.x, (std::max)(s.y, s.z));
			return { u, u, u };
		}

		// ------------------ Key（重複排除用） ------------------
		struct ShapeKey {
			enum class Kind : uint8_t { Box, Sphere, Capsule, Mesh, HeightField, ConvexHull } kind{};
			// 汎用フィールド（使う所だけ使う）
			Vec3f he{};             // Box
			float r{ 0.f };           // Sphere/Capsule.r
			float hh{ 0.f };          // Capsule.halfHeight
			// Mesh summary
			size_t vhash{ 0 }, ihash{ 0 };
			uint32_t vcount{ 0 }, icount{ 0 };
			// HeightField summary
			int sizeX{ 0 }, sizeY{ 0 };
			size_t hfHash{ 0 };
			float scaleY{ 1.f }, cellX{ 1.f }, cellY{ 1.f };
			// Scale summary
			Vec3f scale{ 1,1,1 };
			// ConvexHull summary
			size_t chash{ 0 };
			uint32_t pcount{ 0 };
		};
		// ハッシュと比較（ビット等価）
		// 浮動小数は「ビット列」をハッシュするのが意図どおり（近似比較は避ける）
		static inline size_t HashFloatBits(float f) {
			static_assert(sizeof(float) == 4, "float must be 32-bit");
			uint32_t u; std::memcpy(&u, &f, 4);
			return std::hash<uint32_t>{}(u);
		}
		struct ShapeKeyHash {
			size_t operator()(ShapeKey const& k) const noexcept {
				size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(k.kind));
				auto mix = [&](size_t x) { h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
				mix(HashFloatBits(k.scale.x)); mix(HashFloatBits(k.scale.y)); mix(HashFloatBits(k.scale.z));
				switch (k.kind) {
				case ShapeKey::Kind::Box:
					mix(HashFloatBits(k.he.x)); mix(HashFloatBits(k.he.y)); mix(HashFloatBits(k.he.z));
					break;
				case ShapeKey::Kind::Sphere:
					mix(HashFloatBits(k.r));
					break;
				case ShapeKey::Kind::Capsule:
					mix(HashFloatBits(k.hh)); mix(HashFloatBits(k.r));
					break;
				case ShapeKey::Kind::Mesh:
					mix(k.vhash); mix(k.ihash); mix(k.vcount); mix(k.icount);
					break;
				case ShapeKey::Kind::HeightField:
					mix(std::hash<int>{}(k.sizeX)); mix(std::hash<int>{}(k.sizeY));
					mix(k.hfHash);
					mix(HashFloatBits(k.scaleY)); mix(HashFloatBits(k.cellX)); mix(HashFloatBits(k.cellY));
					break;
				}
				return h;
			}
		};
		struct ShapeKeyEq {
			bool operator()(ShapeKey const& a, ShapeKey const& b) const noexcept {
				if (a.kind != b.kind) return false;
				if (a.scale.x != b.scale.x || a.scale.y != b.scale.y || a.scale.z != b.scale.z) return false;
				switch (a.kind) {
				case ShapeKey::Kind::Box:
					return a.he.x == b.he.x && a.he.y == b.he.y && a.he.z == b.he.z;
				case ShapeKey::Kind::Sphere:
					return a.r == b.r;
				case ShapeKey::Kind::Capsule:
					return a.hh == b.hh && a.r == b.r;
				case ShapeKey::Kind::Mesh:
					return a.vhash == b.vhash && a.ihash == b.ihash && a.vcount == b.vcount && a.icount == b.icount;
				case ShapeKey::Kind::HeightField:
					return a.sizeX == b.sizeX && a.sizeY == b.sizeY && a.hfHash == b.hfHash
						&& a.scaleY == b.scaleY && a.cellX == b.cellX && a.cellY == b.cellY;
				}
				return false;
			}
		};

		// ================== PhysicsShapeManager ==================
		class PhysicsShapeManager
			: public ResourceManagerBase<PhysicsShapeManager, ShapeHandle, ShapeCreateDesc, JPH::RefConst<JPH::Shape>>,
			public IShapeResolver
		{
			using Base = ResourceManagerBase<PhysicsShapeManager, ShapeHandle, ShapeCreateDesc, JPH::RefConst<JPH::Shape>>;
		public:
			// ---- ResourceManagerBase から呼ばれる必須メソッド群 ----
			// 既存検出
			std::optional<ShapeHandle> FindExisting(const ShapeCreateDesc& desc) const {
				ShapeKey key = BuildKey(desc);
				std::shared_lock lk(cacheMutex_);
				auto it = keyToHandle_.find(key);
				if (it == keyToHandle_.end()) return std::nullopt;
				// IsValid も確認（世代が変わっている可能性に備える）
				const ShapeHandle h = it->second;
				if (!this->IsValid(h)) return std::nullopt;
				return h;
			}

			// 新規登録時にキーを記録
			void RegisterKey(const ShapeCreateDesc& desc, ShapeHandle h) {
				ShapeKey key = BuildKey(desc);
				{
					std::unique_lock lk(cacheMutex_);
					keyToHandle_.emplace(key, h);
					if (indexToKey_.size() <= h.index) indexToKey_.resize(h.index + 1);
					indexToKey_[h.index] = key;
				}
			}

			// 破棄時にキャッシュから掃除
			void RemoveFromCaches(uint32_t index) {
				std::unique_lock lk(cacheMutex_);
				if (index < indexToKey_.size()) {
					auto it = keyToHandle_.find(indexToKey_[index]);
					if (it != keyToHandle_.end() && it->second.index == index) {
						keyToHandle_.erase(it);
					}
					// Key は不要になったのでデフォルトに戻す（オプション）
					// indexToKey_[index] はそのままでも問題ないが、明示的に消すなら：
					// indexToKey_[index] = ShapeKey{};
				}
			}

			// 実体生成
			JPH::RefConst<JPH::Shape> CreateResource(const ShapeCreateDesc& desc, ShapeHandle /*h*/) {
				using namespace JPH;

				// Sphere/Capsule はスケールを“一様化”
				Vec3f scale = desc.scale.s;
				bool radial = std::holds_alternative<SphereDesc>(desc.shape) || std::holds_alternative<CapsuleDesc>(desc.shape);
				if (radial && !IsUniformScale(scale)) scale = EnforceUniformScale(scale);

				auto make_scaled = [&](RefConst<Shape> base)->RefConst<Shape> {
					if (scale.x == 1.f && scale.y == 1.f && scale.z == 1.f) return base;
					return RefConst<Shape>(new ScaledShape(base, Vec3(scale.x, scale.y, scale.z)));
					};

				return std::visit([&](auto&& d)->RefConst<Shape> {
					using T = std::decay_t<decltype(d)>;

					if constexpr (std::is_same_v<T, BoxDesc>) {
						RefConst<Shape> base = new BoxShape(Vec3(d.halfExtents.x, d.halfExtents.y, d.halfExtents.z));
						return make_scaled(base);
					}
					else if constexpr (std::is_same_v<T, SphereDesc>) {
						RefConst<Shape> base = new SphereShape(d.radius);
						return make_scaled(base); // （scale は一様化済み）
					}
					else if constexpr (std::is_same_v<T, CapsuleDesc>) {
						RefConst<Shape> base = new CapsuleShape(d.halfHeight, d.radius);
						return make_scaled(base); // （scale は一様化済み）
					}
					else if constexpr (std::is_same_v<T, MeshDesc>) {
						MeshShapeSettings st;
						st.mTriangleVertices.reserve(d.vertices.size());
						for (auto& v : d.vertices) st.mTriangleVertices.emplace_back(v.x, v.y, v.z);
						st.mIndexedTriangles.reserve(d.indices.size() / 3);
						for (size_t i = 0; i + 2 < d.indices.size(); i += 3)
							st.mIndexedTriangles.emplace_back(d.indices[i], d.indices[i + 1], d.indices[i + 2]);
						auto res = st.Create();
						if (res.HasError()) return RefConst<Shape>(new BoxShape(Vec3(0.5f, 0.5f, 0.5f)));
						return make_scaled(res.Get());
					}
					else if constexpr (std::is_same_v<T, HeightFieldDesc>) {
						// Joltの新APIは正方のみ: count x count
						if (d.sizeX != d.sizeY || d.sizeX <= 0) {
							// 正方でない場合のフォールバック（必要に応じて別実装へ）
							return JPH::RefConst<JPH::Shape>(new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f)));
						}
						const uint32_t count = static_cast<uint32_t>(d.sizeX);
						if (d.samples.size() != static_cast<size_t>(count) * static_cast<size_t>(count)) {
							// サンプル数不一致 → フォールバック
							return JPH::RefConst<JPH::Shape>(new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f)));
						}

						// offset と scale を設定
						const JPH::Vec3 offset(0.0f, 0.0f, 0.0f);                      // 必要ならここで地形の原点を調整
						const JPH::Vec3 scale(d.cellSizeX, d.scaleY, d.cellSizeY);    // x=グリッド間隔X, y=高さスケール, z=グリッド間隔Y

						// material は未使用なら nullptr / 既定リスト
						const uint8_t* materialIndices = nullptr;
						JPH::PhysicsMaterialList materialList; // 既定

						JPH::HeightFieldShapeSettings st(
							d.samples.data(), offset, scale, count, materialIndices, materialList
						);

						auto res = st.Create();
						if (res.HasError()) {
							return JPH::RefConst<JPH::Shape>(new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f)));
						}

						// HeightField は通常 ScaledShape でさらに包む必要はないが、設計統一でapply可
						// （ただし inScale と二重にならないよう注意。ここではスケールは HeightField 側に集約）
						return res.Get();
					}
					else if constexpr (std::is_same_v<T, ConvexHullDesc>) {
						ConvexHullShapeSettings st;
						st.mMaxConvexRadius = d.maxConvexRadius;
						st.mHullTolerance = d.hullTolerance;
						st.mPoints.reserve(d.points.size());
						for (auto& p : d.points) st.mPoints.emplace_back(p.x, p.y, p.z);

						auto res = st.Create();
						if (res.HasError()) return RefConst<Shape>(new BoxShape(Vec3(0.5f, 0.5f, 0.5f)));
						return make_scaled(res.Get());
					}
					else {
						return RefConst<Shape>(new BoxShape(Vec3(0.5f, 0.5f, 0.5f)));
					}
					}, desc.shape);
			}

			// 実体破棄（RefConst は参照カウント付き：クリアでOK）
			void DestroyResource(uint32_t index, uint64_t /*currentFrame*/) {
				this->slots[index].data = nullptr;
			}

			// ---- 追加ユーティリティ ----
			// 外部から Shape を取り出すヘルパ（薄い糖衣）
			JPH::RefConst<JPH::Shape> Resolve(ShapeHandle h) const {
				auto d = this->Get(h);
				return d.ref();
			}

			std::optional<ShapeDims> GetShapeDims(const JPH::Shape* s) const {
				switch (s->GetSubType()) {
				case JPH::EShapeSubType::Box: {
					auto bs = static_cast<const JPH::BoxShape*>(s);
					JPH::Vec3 he = bs->GetHalfExtent();
					return ShapeDims{ .dims = Math::Vec3f(he.GetX(),he.GetY(),he.GetZ()) * 2.0f, .type = ShapeDims::Type::Box };
				}
				case JPH::EShapeSubType::Sphere: {
					// SphereShape は半径が取れる
					// （AABB から直径=2r も計算可能）
					// ここでは AABB から直径を算出
					JPH::AABox b = s->GetLocalBounds();
					JPH::Vec3 d = b.GetSize();
					return ShapeDims{ .dims = Math::Vec3f(d.GetX(),d.GetY(),d.GetZ()), .r = 0.5f * d.GetX() , .type = ShapeDims::Type::Sphere }; // 等方
				}
				case JPH::EShapeSubType::Capsule: {
					auto cs = static_cast<const JPH::CapsuleShape*>(s);
					ShapeDims out;
					out.r = cs->GetRadius();
					out.halfHeight = cs->GetHalfHeightOfCylinder();
					out.dims = Math::Vec3f(2.0f * out.r, 2.0f * (out.halfHeight + out.r), 2.0f * out.r);
					out.type = ShapeDims::Type::Capsule;
					return out;
				}
				case JPH::EShapeSubType::Cylinder: {
					// CylinderShape: 半径/半高さ
					// （クラスリファレンスに半径・半高さとコンストラクタ定義あり）
					// AABB からも取得可
					JPH::AABox b = s->GetLocalBounds();
					auto s = b.GetSize();
					return ShapeDims{ .dims = Math::Vec3f(s.GetX(),s.GetY(),s.GetZ()), .type = ShapeDims::Type::Cylinder };
				}
				case JPH::EShapeSubType::TaperedCapsule:
				case JPH::EShapeSubType::TaperedCylinder: {
					// 上下で半径が異なるため、AABB で外接寸法を得るのが簡単
					JPH::AABox b = s->GetLocalBounds();
					auto s = b.GetSize();
					return ShapeDims{ .dims = Math::Vec3f(s.GetX(),s.GetY(),s.GetZ()), .type = ShapeDims::Type::Tapered };
				}
				default:
					// ConvexHull / Mesh / HeightField / Compound などは AABB 経由で外接寸法を得る
					JPH::AABox b = s->GetLocalBounds();
					auto s = b.GetSize();
					return ShapeDims{ .dims = Math::Vec3f(s.GetX(),s.GetY(),s.GetZ()), .type = ShapeDims::Type::CMHC };
				}
			}

		private:
			// Key の生成
			static ShapeKey BuildKey(const ShapeCreateDesc& d) {
				ShapeKey k{};
				// まずスケールを入れるが、Sphere/Capsule は“一様強制”する
				Vec3f scale = d.scale.s;

				std::visit([&](auto&& s) {
					using T = std::decay_t<decltype(s)>;
					if constexpr (std::is_same_v<T, SphereDesc> || std::is_same_v<T, CapsuleDesc>) {
						scale = IsUniformScale(scale) ? scale : EnforceUniformScale(scale);
					}
					}, d.shape);
				k.scale = scale;

				std::visit([&](auto&& s) {
					using T = std::decay_t<decltype(s)>;
					if constexpr (std::is_same_v<T, BoxDesc>) {
						k.kind = ShapeKey::Kind::Box;
						k.he = s.halfExtents;
					}
					else if constexpr (std::is_same_v<T, SphereDesc>) {
						k.kind = ShapeKey::Kind::Sphere;
						k.r = s.radius;
					}
					else if constexpr (std::is_same_v<T, CapsuleDesc>) {
						k.kind = ShapeKey::Kind::Capsule;
						k.hh = s.halfHeight; k.r = s.radius;
					}
					else if constexpr (std::is_same_v<T, MeshDesc>) {
						k.kind = ShapeKey::Kind::Mesh;
						k.vcount = (uint32_t)s.vertices.size();
						k.icount = (uint32_t)s.indices.size();
						if (!s.vertices.empty())
							k.vhash = HashBufferContent(s.vertices.data(), s.vertices.size() * sizeof(Vec3f));
						if (!s.indices.empty())
							k.ihash = HashBufferContent(s.indices.data(), s.indices.size() * sizeof(uint32_t));
					}
					else if constexpr (std::is_same_v<T, HeightFieldDesc>) {
						k.kind = ShapeKey::Kind::HeightField;
						k.sizeX = s.sizeX; k.sizeY = s.sizeY;
						k.scaleY = s.scaleY; k.cellX = s.cellSizeX; k.cellY = s.cellSizeY;
						if (!s.samples.empty())
							k.hfHash = HashBufferContent(s.samples.data(), s.samples.size() * sizeof(float));
					}
					else if constexpr (std::is_same_v<T, ConvexHullDesc>) {
						k.kind = ShapeKey::Kind::ConvexHull;
						k.pcount = (uint32_t)s.points.size();
						if (!s.points.empty())
							k.chash = HashBufferContent(s.points.data(), s.points.size() * sizeof(Vec3f));
					}
					}, d.shape);

				return k;
			}

		private:
			// 共有キャッシュ
			mutable std::shared_mutex cacheMutex_;
			std::unordered_map<ShapeKey, ShapeHandle, ShapeKeyHash, ShapeKeyEq> keyToHandle_;
			std::vector<ShapeKey> indexToKey_; // index -> key（RemoveFromCaches 用）
		};
	}
}

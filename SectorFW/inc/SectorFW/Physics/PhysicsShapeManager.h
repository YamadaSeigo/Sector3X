/*****************************************************************//**
 * @file   PhysicsShapeManager.h
 * @brief 物理形状の管理クラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once

#include "IShapeResolver.h"
#include "../Util/ResouceManagerBase.hpp"
#include "PhysicsComponent.h"
#include "PhysicsConvexHullLoader.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <variant>
#include <vector>
#include <cstdint>

#ifdef _DEBUG
#define CACHE_SHAPE_WIRE_DATA // ワイヤーフレーム用頂点/インデックスをキャッシュするかどうか
#endif

namespace SFW
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
			// 汎用フィールド（使う所だけ使う）
			Vec3f he{};             // Box
			float r{ 0.f };           // Sphere/Capsule.r
			float hh{ 0.f };          // Capsule.halfHeight
			// HeightField summary
			int sizeX{ 0 }, sizeY{ 0 };
			float scaleY{ 1.f }, cellX{ 1.f }, cellY{ 1.f };
			// Mesh summary
			uint32_t vcount{ 0 }, icount{ 0 };
			size_t vhash{ 0 }, ihash{ 0 };
			size_t hfHash{ 0 };
			// Scale summary
			Vec3f scale{ 1,1,1 };
			// ローカルオフセット
			Vec3f offset{ 0,0,0 };
			// ローカル回転（Quatf）
			Quatf rotation = Quatf::Identity();
			// ConvexHull summary
			size_t chash{ 0 };
			uint32_t pcount{ 0 };

			// ConvexCompoundFile summary
			size_t fileHash{ 0 };   // path のハッシュ

			enum class Kind : uint8_t {
				Box,
				Sphere,
				Capsule,
				Mesh,
				HeightField,
				ConvexHull,
				ConvexCompound,
				ConvexCompoundFile
			} kind{};
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

				// scale
				mix(HashFloatBits(k.scale.x));
				mix(HashFloatBits(k.scale.y));
				mix(HashFloatBits(k.scale.z));

				// offset
				mix(HashFloatBits(k.offset.x));
				mix(HashFloatBits(k.offset.y));
				mix(HashFloatBits(k.offset.z));

				// rotation (x,y,z,w)
				mix(HashFloatBits(k.rotation.x));
				mix(HashFloatBits(k.rotation.y));
				mix(HashFloatBits(k.rotation.z));
				mix(HashFloatBits(k.rotation.w));

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
				case ShapeKey::Kind::ConvexHull:
				case ShapeKey::Kind::ConvexCompound:
					mix(k.chash);
					mix(k.pcount);
					break;
				case ShapeKey::Kind::ConvexCompoundFile:
					mix(k.fileHash);
					break;
				}
				return h;
			}
		};
		struct ShapeKeyEq {
			bool operator()(ShapeKey const& a, ShapeKey const& b) const noexcept {
				if (a.kind != b.kind) return false;
				// scale
				if (a.scale.x != b.scale.x || a.scale.y != b.scale.y || a.scale.z != b.scale.z) return false;
				// offset
				if (a.offset.x != b.offset.x || a.offset.y != b.offset.y || a.offset.z != b.offset.z) return false;
				// rotation
				if (a.rotation.x != b.rotation.x ||
					a.rotation.y != b.rotation.y ||
					a.rotation.z != b.rotation.z ||
					a.rotation.w != b.rotation.w) return false;
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
				case ShapeKey::Kind::ConvexHull:
					return a.chash == b.chash && a.pcount == b.pcount;
				case ShapeKey::Kind::ConvexCompound:
					return a.chash == b.chash && a.pcount == b.pcount;
				case ShapeKey::Kind::ConvexCompoundFile:
					return a.fileHash == b.fileHash;
				}
				return false;
			}
		};

#ifdef CACHE_SHAPE_WIRE_DATA
		struct WireframeData {
			std::vector<Vec3f> vertices;
			std::vector<uint32_t> indices;
		};

		// triIndices: 3 要素で 1 三角形
		inline WireframeData BuildShapeWireframe(
			const std::vector<Vec3f>& verts,
			const std::vector<uint32_t>& triIndices)
		{
			WireframeData out;
			out.vertices = verts; // そのままコピー（もしくは参照を別管理でもOK）

			std::unordered_set<uint64_t> edgeSet;
			edgeSet.reserve(triIndices.size() * 2);

			auto add_edge = [&](uint32_t a, uint32_t b)
				{
					if (a == b) return;

					// 無向エッジとして扱うため (min,max) に正規化
					uint32_t i0 = (std::min)(a, b);
					uint32_t i1 = (std::max)(a, b);
					uint64_t key = (uint64_t(i0) << 32) | uint64_t(i1);

					if (edgeSet.insert(key).second)
					{
						// 新規エッジだけ LINELIST インデックスに追加
						out.indices.push_back(i0);
						out.indices.push_back(i1);
					}
				};

			const size_t triCount = triIndices.size() / 3;
			for (size_t t = 0; t < triCount; ++t)
			{
				uint32_t i0 = triIndices[t * 3 + 0];
				uint32_t i1 = triIndices[t * 3 + 1];
				uint32_t i2 = triIndices[t * 3 + 2];

				add_edge(i0, i1);
				add_edge(i1, i2);
				add_edge(i2, i0);
			}

			return out;
		}

		struct ShareWireframeData {
			const WireframeData& data;

			ShareWireframeData(const WireframeData& _data, std::shared_mutex& mutex)
				: data(_data), lk_(mutex){
			}
		private:
			std::shared_lock<std::shared_mutex> lk_;
		};
#endif

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

#ifdef CACHE_SHAPE_WIRE_DATA
				auto wit = wireDataCache_.find(index);
				if (wit != wireDataCache_.end()) {
					wireDataCache_.erase(wit);
				}
#endif
			}

			// 実体生成
			JPH::RefConst<JPH::Shape> CreateResource(const ShapeCreateDesc& desc, ShapeHandle h) {
				using namespace JPH;

				// ---- スケール決定 ----
				Vec3f scale = desc.scale.s;
				bool radial = std::holds_alternative<SphereDesc>(desc.shape) || std::holds_alternative<CapsuleDesc>(desc.shape);
				if (radial && !IsUniformScale(scale)) scale = EnforceUniformScale(scale);

				auto make_scaled = [&](RefConst<Shape> base)->RefConst<Shape> {
					if (scale.x == 1.f && scale.y == 1.f && scale.z == 1.f) return base;
					return RefConst<Shape>(new ScaledShape(base, Vec3(scale.x, scale.y, scale.z)));
					};

				// RotatedTranslatedShape を噛ませる
				const Vec3f& ofs = desc.localOffset;
				const Quatf& local_rot = desc.localRotation;

				auto make_rotated_translated = [&](RefConst<Shape> base)->RefConst<Shape> {
					// 何も変換がないならそのまま返してもOK（最初は常に wrap してもいい）
					bool has_offset =
						(ofs.x != 0.0f) || (ofs.y != 0.0f) || (ofs.z != 0.0f);

					bool has_rot =
						!(local_rot.x == 0.0f && local_rot.y == 0.0f &&
							local_rot.z == 0.0f && local_rot.w == 1.0f);

					if (!has_offset && !has_rot)
						return base;

					Vec3 pos(ofs.x, ofs.y, ofs.z);
					Quat rot(local_rot.x, local_rot.y, local_rot.z, local_rot.w);

					return RefConst<Shape>(new RotatedTranslatedShape(pos, rot, base));
					};

				return std::visit([&](auto&& d)->RefConst<Shape> {
					using T = std::decay_t<decltype(d)>;

					if constexpr (std::is_same_v<T, BoxDesc>) {
						RefConst<Shape> base = new BoxShape(Vec3(d.halfExtents.x, d.halfExtents.y, d.halfExtents.z));
						base = make_scaled(base);
						return make_rotated_translated(base);
					}
					else if constexpr (std::is_same_v<T, SphereDesc>) {
						RefConst<Shape> base = new SphereShape(d.radius);
						base = make_scaled(base);
						return make_rotated_translated(base);
					}
					else if constexpr (std::is_same_v<T, CapsuleDesc>) {
						RefConst<Shape> base = new CapsuleShape(d.halfHeight, d.radius);
						base = make_scaled(base);
						return make_rotated_translated(base);
					}
					else if constexpr (std::is_same_v<T, MeshDesc>) {
						MeshShapeSettings st;
						st.mTriangleVertices.reserve(d.vertices.size());
						for (auto& v : d.vertices) st.mTriangleVertices.emplace_back(v.x, v.y, v.z);
						st.mIndexedTriangles.reserve(d.indices.size() / 3);
						for (size_t i = 0; i + 2 < d.indices.size(); i += 3)
							st.mIndexedTriangles.emplace_back(d.indices[i], d.indices[i + 1], d.indices[i + 2]);

#ifdef CACHE_SHAPE_WIRE_DATA
						wireDataCache_.emplace(h.index,
							BuildShapeWireframe(d.vertices, d.indices));
#endif

						auto res = st.Create();
						if (res.HasError()) return make_rotated_translated(make_scaled(RefConst<Shape>(new BoxShape(Vec3(0.5f, 0.5f, 0.5f)))));
						RefConst<Shape> base = res.Get();
						return make_rotated_translated(base);
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
							RefConst<Shape> base = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
							base = make_scaled(base);
							return make_rotated_translated(base);
						}
						// HeightField は res.Get() をそのままオフセットだけ適用
						return make_rotated_translated(res.Get());
					}
					else if constexpr (std::is_same_v<T, ConvexHullDesc>) {
						ConvexHullShapeSettings st;
						st.mMaxConvexRadius = d.maxConvexRadius;
						st.mHullTolerance = d.hullTolerance;
						st.mPoints.reserve(d.points.size());
						for (auto& p : d.points) st.mPoints.emplace_back(p.x, p.y, p.z);

#ifdef CACHE_SHAPE_WIRE_DATA
						wireDataCache_.emplace(h.index,
							BuildShapeWireframe(d.points, d.indices));
#endif

						auto res = st.Create();
						if (res.HasError()) {
							RefConst<Shape> base = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
							base = make_scaled(base);
							return make_rotated_translated(base);
						}
						RefConst<Shape> base = res.Get();
						return make_rotated_translated(base);
					}
					else if constexpr (std::is_same_v<T, ConvexCompoundDesc> || std::is_same_v<T, ConvexCompoundFileDesc>) {
						using namespace SFW::Physics;

						// 1) バイナリを読み込んで hull 群を取得
						std::vector<VHACDHull> hulls;
						if constexpr (std::is_same_v<T, ConvexCompoundDesc>)
						{
							hulls = d.hulls;
							for (auto& hull : hulls)
							{
								auto flip_vec3z = [](float& x, float& y, float& z) { x = -x; };

								for (auto& p : hull.points)
								{
									p.x *= desc.scale.s.x;
									p.y *= desc.scale.s.y;
									p.z *= desc.scale.s.z;

									if (d.rhFlip)
										flip_vec3z(p.x, p.y, p.z);
								}
							}
						}
						else
						{
							bool debug = d.path == "generated/convex/RuinTower.chullbin";

							if (!LoadVHACDFile(d.path, hulls, desc.scale.s, d.rhFlip) || hulls.empty()) {
								// 読み込み失敗 → フォールバック Box
								RefConst<Shape> base = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
								base = make_scaled(base);

								LOG_WARNING("PhysicsShapeManager: Failed to load VHACD file: {%s}", d.path.c_str());

								return make_rotated_translated(base);
							}
						}

						// 2) hull が 1つだけなら普通の ConvexHull として処理
						if (hulls.size() == 1) {
							ConvexHullShapeSettings hst;
							hst.mMaxConvexRadius = d.maxConvexRadius;
							hst.mHullTolerance = d.hullTolerance;
							hst.mPoints.reserve(hulls[0].points.size());
							for (auto& p : hulls[0].points)
								hst.mPoints.emplace_back(p.x, p.y, p.z);

							auto res = hst.Create();
							if (res.HasError()) {
								RefConst<Shape> base = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
								base = make_scaled(base);
								return make_rotated_translated(base);
							}
							RefConst<Shape> base = res.Get();
							return make_rotated_translated(base);
						}

						// 3) hull が複数なら StaticCompoundShape としてまとめる
						StaticCompoundShapeSettings cs;

						for (auto& h : hulls) {
							if (h.points.empty()) continue;

							ConvexHullShapeSettings hst;
							hst.mMaxConvexRadius = d.maxConvexRadius;
							hst.mHullTolerance = d.hullTolerance;
							hst.mPoints.reserve(h.points.size());
							for (auto& p : h.points)
								hst.mPoints.emplace_back(p.x, p.y, p.z);

							auto res = hst.Create();
							if (res.HasError()) {
								// この hull はスキップ（もしくは全体フォールバックにするかは好み）
								continue;
							}

							RefConst<Shape> hullShape = res.Get();
							// 各 hull のローカルオフセットは Python 側で bake 済みと想定し、ここでは (0,0,0)
							cs.AddShape(Vec3::sZero(), Quat::sIdentity(), hullShape);
						}

#ifdef CACHE_SHAPE_WIRE_DATA
						std::vector<Vec3f> allVerts;
						std::vector<uint32_t> allIndices;
						for (auto& h : hulls) {
							auto wf = BuildShapeWireframe(h.points, h.indices); // インデックスは不要
							uint32_t baseIndex = static_cast<uint32_t>(allVerts.size());
							allVerts.insert(allVerts.end(), wf.vertices.begin(), wf.vertices.end());
							allIndices.reserve(allIndices.size() + wf.indices.size());
							// インデックスは頂点追加分だけオフセットして追加
							for (auto idx : wf.indices) {
								allIndices.push_back(baseIndex + idx);
							}
						}
						wireDataCache_.emplace(h.index,
							WireframeData{ allVerts, allIndices });
#endif

						auto compRes = cs.Create();
						if (compRes.HasError()) {
							RefConst<Shape> base = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
							base = make_scaled(base);
							return make_rotated_translated(base);
						}

						RefConst<Shape> compound = compRes.Get();
						return make_rotated_translated(compound);
					}
					else {
						RefConst<Shape> base = new BoxShape(Vec3(0.5f, 0.5f, 0.5f));
						base = make_scaled(base);
						return make_rotated_translated(base);
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


			std::optional<ShapeDims> GetShapeDims(const JPH::Shape* s, ShapeHandle h)
			{
				using namespace JPH;

				const Shape* shape = s;

				// DecoratedShape（特に RotatedTranslatedShape）のローカル変換
				Math::Vec3f localOffset{ 0.0f, 0.0f, 0.0f };
				Math::Quatf localRot = Math::Quatf::Identity();
				bool hasLocalTransform = false;

				// まず RotatedTranslatedShape をほどく（1段だけならこれで十分）
				if (shape->GetSubType() == EShapeSubType::RotatedTranslated)
				{
					auto rt = static_cast<const RotatedTranslatedShape*>(shape);

					Vec3 p = rt->GetPosition();   // 内側シェイプに適用された平行移動
					Quat q = rt->GetRotation();   // 内側シェイプに適用された回転

					localOffset = Math::Vec3f(p.GetX(), p.GetY(), p.GetZ());
					localRot = Math::Quatf(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
					hasLocalTransform = true;

					// 寸法は内側のシェイプから取る
					shape = rt->GetInnerShape();
				}

				ShapeDims out;

				switch (shape->GetSubType())
				{
				case EShapeSubType::Box:
				{
					auto bs = static_cast<const BoxShape*>(shape);
					Vec3 he = bs->GetHalfExtent();
					out.dims = Math::Vec3f(he.GetX(), he.GetY(), he.GetZ()) * 2.0f;
					out.type = ShapeDims::Type::Box;
					break;
				}
				case EShapeSubType::Sphere:
				{
					AABox b = shape->GetLocalBounds();
					Vec3 d = b.GetSize();
					out.dims = Math::Vec3f(d.GetX(), d.GetY(), d.GetZ());
					out.r = 0.5f * d.GetX(); // 等方
					out.type = ShapeDims::Type::Sphere;
					break;
				}
				case EShapeSubType::Capsule:
				{
					auto cs = static_cast<const CapsuleShape*>(shape);
					out.r = cs->GetRadius();
					out.halfHeight = cs->GetHalfHeightOfCylinder();
					out.dims = Math::Vec3f(
						2.0f * out.r,
						2.0f * (out.halfHeight + out.r),
						2.0f * out.r
					);
					out.type = ShapeDims::Type::Capsule;
					break;
				}
				case EShapeSubType::Cylinder:
				{
					AABox b = shape->GetLocalBounds();
					Vec3 d = b.GetSize();
					out.dims = Math::Vec3f(d.GetX(), d.GetY(), d.GetZ());
					out.type = ShapeDims::Type::Cylinder;
					break;
				}
				case EShapeSubType::TaperedCapsule:
				case EShapeSubType::TaperedCylinder:
				{
					AABox b = shape->GetLocalBounds();
					Vec3 d = b.GetSize();
					out.dims = Math::Vec3f(d.GetX(), d.GetY(), d.GetZ());
					out.type = ShapeDims::Type::Tapered;
					break;
				}
				default:
				{
					// ConvexHull / Mesh / HeightField / Compound など
					AABox b = shape->GetLocalBounds();
					Vec3 d = b.GetSize();
					out.dims = Math::Vec3f(d.GetX(), d.GetY(), d.GetZ());
					out.type = ShapeDims::Type::CMHC;
					out.handle = h;
					AddRef(h);
					break;
				}
				}

				// ここでローカル変換を反映
				out.localOffset = localOffset;
				std::memcpy(out.localRotation, localRot.v, sizeof(out.localRotation));
				out.hasLocalTransform = hasLocalTransform;

				return out;
			}
#ifdef CACHE_SHAPE_WIRE_DATA
			// ワイヤーフレームデータ取得
			std::optional<ShareWireframeData> GetShapeWireframeData(ShapeHandle h) const {
				std::shared_lock lk(cacheMutex_);
				auto it = wireDataCache_.find(h.index);
				if (it == wireDataCache_.end()) return std::nullopt;
				return ShareWireframeData(it->second, cacheMutex_);
			}
#endif

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
				k.offset = d.localOffset;
				k.rotation = d.localRotation;

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
					else if constexpr (std::is_same_v<T, ConvexCompoundDesc>) {
						k.kind = ShapeKey::Kind::ConvexCompound;
						// 複数 hull のハッシュをまとめる
						size_t combinedHash = 0;
						for (const auto& hull : s.hulls) {
							if (!hull.points.empty()) {
								size_t h = HashBufferContent(hull.points.data(), hull.points.size() * sizeof(Vec3f));
								// 簡易的にミックス
								combinedHash ^= h + 0x9e3779b97f4a7c15ull + (combinedHash << 6) + (combinedHash >> 2);

								k.pcount += static_cast<uint32_t>(hull.points.size());
							}
						}
						k.chash = combinedHash;
					}
					else if constexpr (std::is_same_v<T, ConvexCompoundFileDesc>) {
						k.kind = ShapeKey::Kind::ConvexCompoundFile;
						// ファイルパスのハッシュを取る
						k.fileHash = std::hash<std::string>{}(s.path);
					}
					}, d.shape);

				return k;
			}

		private:
			// 共有キャッシュ
			mutable std::shared_mutex cacheMutex_;
			std::unordered_map<ShapeKey, ShapeHandle, ShapeKeyHash, ShapeKeyEq> keyToHandle_;
			std::vector<ShapeKey> indexToKey_; // index -> key（RemoveFromCaches 用）
#ifdef CACHE_SHAPE_WIRE_DATA
			std::unordered_map<uint32_t, WireframeData> wireDataCache_;
#endif
		};
	}
}

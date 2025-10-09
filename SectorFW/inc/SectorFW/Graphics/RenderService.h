/*****************************************************************//**
 * @file   RenderService.h
 * @brief レンダーサービスを定義するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <vector>

#include "../external/MOC/MaskedOcclusionCulling.h"

#include "../Core/ECS/ServiceContext.hpp"
#include "RenderQueue.h"
#include "../Util/TypeChecker.hpp"

#include "../Math/Rectangle.hpp"
#include "OccluderToolkit.h"

namespace SectorFW
{
	namespace Graphics
	{
		using MOC = MaskedOcclusionCulling;

		// --- MOC 提出用の最小バッチ ---
		struct MocQuadBatch {
			SectorFW::Math::Vec4f clip[4];  // (x, y, z, w) だが MOC が使うのは x,y,w
			uint32_t indices[6];            // 0-1-2, 2-1-3 の2枚（CCW）
			int      numTriangles = 2;
			bool     valid = true;          // 近クリップ全面裏などなら false
		};

		/**
		 * @brief Systemが依存するレンダーサービスを管理するクラス
		 * @detail MOCのインスタンスを管理する
		 */
		struct RenderService : public ECS::IUpdateService
		{
			template<typename Backend, PointerType RTV, PointerType SRV, PointerType Buffer>
			friend class RenderGraph;
			/**
			 * @brief コンストラクタ
			 */
			explicit RenderService(MOC* moc) : queueMutex(std::make_unique<std::shared_mutex>()), moc(moc) {}

			/**
			 * @brief デストラクタ
			 * @detial MOCの解放
			 */
			~RenderService() {
				MOC::Destroy(moc);
			}
			/**
			 * @brief MOCの更新
			 */
			void Update(double deltaTime) override final
			{
				moc->ClearBuffer();
			}
			/**
			 * @brief RenderQueueのProducerSessionを取得する関数
			 * @param passName　パス名
			 * @return　RenderQueue::ProducerSession レンダークエリのプロデューサーセッション
			 */
			RenderQueue::ProducerSession GetProducerSession(const std::string& passName)
			{
				std::shared_lock lock(*queueMutex);

				auto it = queueIndex.find(passName);
				if (it == queueIndex.end()) {
					assert(false && "RenderQueue not found for pass name");
				}

				return renderQueues[it->second]->MakeProducer();
			}
			/**
			 * @brief RenderQueueのProducerSessionを取得する関数
			 * @param index インデックス
			 * @return RenderQueue::ProducerSession レンダークエリのプロデューサーセッション
			 */
			RenderQueue::ProducerSession GetProducerSession(size_t index)
			{
				std::shared_lock lock(*queueMutex);
				if (index >= renderQueues.size()) {
					assert(false && "RenderQueue index out of range");
				}
				return renderQueues[index]->MakeProducer();
			}
			/**
			 * @brief 指定した型のResouceManagerを取得する関数
			 * @return ResourceType* 指定した型のResouceManagerのポインタ(見つからない場合はnullptr)
			 */
			template<typename ResourceType>
			ResourceType* GetResourceManager() noexcept
			{
				auto it = resourceManagers.find(typeid(ResourceType));
				if (it == resourceManagers.end()) {
					assert(false && "Resource manager not found for type");
					return nullptr;
				}
				return static_cast<ResourceType*>(it->second);
			}

			/**
			 * @brief MOCで可視判定を行う関数
			 * @param ndc NDC空間での矩形（wmin付き）
			 * @return bool 可視ならtrue、不可視ならfalse
			 */
			auto IsVisibleInMOC(const Math::NdcRectWithW& ndc) const
			{
				return moc->TestRect(ndc.xmin, ndc.ymin, ndc.xmax, ndc.ymax, ndc.wmin);
			}
			/**
			 * @brief MOCにオクルーダーを提出する関数
			 * @param quad オクルーダーのクアッド情報
			 */
			void RenderingOccluderInMOC(MocQuadBatch& quad)
			{
				if (!quad.valid) return;

				// 可能なら Vec4f は standard-layout 前提
				MaskedOcclusionCulling::VertexLayout layout(
					/*stride=*/sizeof(SectorFW::Math::Vec4f),
					/*offsetY=*/offsetof(SectorFW::Math::Vec4f, y),
					/*offsetW=*/offsetof(SectorFW::Math::Vec4f, w)
				);

				struct Clip4 { float x, y, z, w; };
				Clip4 tmp[4];
				for (int i = 0; i < 4; ++i)
					tmp[i] = { quad.clip[i].x, quad.clip[i].y, quad.clip[i].z, quad.clip[i].w };

				moc->RenderTriangles(
					(float*)tmp,
					(unsigned*)&quad.indices[0],
					quad.numTriangles,
					nullptr,
					MOC::BACKFACE_NONE
				);
			}
			/**
			 * @brief MOCの近クリップ平面を取得する関数
			 * @return float 近クリップ平面の距離
			 */
			float GetNearClipPlane() const { return moc->GetNearClipPlane(); }
			/**
			 * @brief MOCの深度バッファを取得する関数
			 * @detial サイズが足りない場合はリサイズする
			 * @param buffer 深度バッファを格納するベクター
			 */
			void GetDepthBuffer(std::vector<float>& buffer) const
			{
				uint32_t width, height;
				moc->GetResolution(width, height);
				if (buffer.size() < width * height) buffer.resize(width * height);

				moc->ComputePixelDepthBuffer(buffer.data(), false);
			}
		private:
			template<typename ResourceType>
			void RegisterResourceManager(ResourceType* manager)
			{
				if (!manager) {
					assert(false && "Cannot register null resource manager");
					return;
				}
				if (resourceManagers.contains(typeid(ResourceType))) {
					assert(false && "Resource manager already registered for this type");
					return;
				}

				resourceManagers[typeid(ResourceType)] = manager;
			}
		private:
			std::unordered_map<std::string, size_t> queueIndex;
			std::vector<std::unique_ptr<RenderQueue>> renderQueues; // 全てのレンダークエリを保持する
			std::unique_ptr<std::shared_mutex> queueMutex;
			std::unordered_map<std::type_index, void*> resourceManagers;
			uint64_t currentFrame = 0; // 現在のフレーム番号

			MOC* moc = nullptr; // Masked Occlusion Culling のインスタンス
		public:
			STATIC_SERVICE_TAG
		};

		// AABBFrontFaceQuad を 2トライで提出できるバッチに変換
		inline MocQuadBatch ConvertAABBFrontFaceQuadToMoc(
			const AABBFrontFaceQuad& quadWS,
			const SectorFW::Math::Matrix<4, 4, float>& viewProj,
			float nearClipW /* = moc->GetNearClipPlane() と一致させるのが理想 */
		)
		{
			using Vec3 = SectorFW::Math::Vec3f;
			using Vec4 = SectorFW::Math::Vec4f;

			MocQuadBatch out{};

			// 1) world -> clip
			for (int i = 0; i < 4; ++i) {
				const Vec3& p = quadWS.v[i];
				const Vec4 wpos{ p.x, p.y, p.z, 1.0f };
				// 行列の掛け順：clip = viewProj * wpos （v*M派なら wpos * viewProj）
				out.clip[i] = wpos * viewProj;
			}

			// 2) 近クリップ整合（全頂点が near より奥側なら無効）
			//    MOC は depth = 1/w（逆深度）なので、near と合わせるなら w > near を可視空間とみなす。
			int countFront = 0;
			for (int i = 0; i < 4; ++i) if (out.clip[i].w > nearClipW) ++countFront;
			if (countFront == 0) {
				out.valid = false;
				return out;
			}

			// 3) スクリーン空間の巻き方向を安定させる（CCW想定）
			//    NDC の x,y を使って符号付き面積で CCW を判定し、CW なら対角を入れ替える。
			auto ndcXY = [&](int i) {
				const float invw = 1.0f / out.clip[i].w;
				return SectorFW::Math::Vec2f{ out.clip[i].x * invw, out.clip[i].y * invw };
				};
			const auto a = ndcXY(0), b = ndcXY(1), c = ndcXY(2), d = ndcXY(3);
			auto triArea2 = [](SectorFW::Math::Vec2f p, SectorFW::Math::Vec2f q, SectorFW::Math::Vec2f r) {
				// 2倍面積（z成分）: (q - p) × (r - p)
				return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
				};
			float area = triArea2(a, b, c) + triArea2(a, c, d);
			bool ccw = (area > 0.0f);
			// CCW でなければ 1 と 2 を入れ替える（0,1,2 / 2,1,3 -> 0,2,1 / 1,2,3）
			if (!ccw) {
				std::swap(out.clip[0], out.clip[2]);
			}

			// 4) インデックス（CCW）
			out.indices[0] = 0; out.indices[1] = 1; out.indices[2] = 2;
			out.indices[3] = 2; out.indices[4] = 3; out.indices[5] = 0;

			// 5) これで MOC::RenderTriangles にそのまま渡せます
			// moc->RenderTriangles((float*)&out.clip[0], sizeof(SectorFW::Math::Vec4f),
			//                      (unsigned*)&out.indices[0], out.numTriangles);

			return out;
		}
	}
}

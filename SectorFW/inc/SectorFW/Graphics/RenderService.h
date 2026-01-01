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

namespace SFW
{
	namespace Graphics
	{
		using MOC = MaskedOcclusionCulling;

		// --- MOC 提出用の最小バッチ ---
		struct MocQuadBatch {
			SFW::Math::Vec4f clip[4];  // (x, y, z, w) だが MOC が使うのは x,y,w
			uint32_t indices[6];            // 0-1-2, 2-1-3 の2枚（CCW）
			int      numTriangles = 2;
			bool     valid = true;          // 近クリップ全面裏などなら false
		};

		struct MocTriBatch {
			const float* clipVertices = nullptr; // (x, y, z, w) 配列
			const uint32_t* indices = nullptr;   // インデックス配列
			uint32_t      numTriangles = 0;
			bool          valid = true;          // 近クリップ全面裏などなら false
		};

		/**
		 * @brief Systemが依存するレンダーサービスを管理するクラス
		 * @details MOCのインスタンスを管理する
		 */
		struct RenderService : public ECS::IUpdateService
		{
			using UpdateFuncType = void(*)(RenderService*);
			using PreDrawFuncType = void(*)(RenderService*);

			template<typename Backend, PointerType RTV, PointerType DSV, PointerType SRV, PointerType Buffer, template <typename> class ViewHandle>
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
		private:
			/**
			 * @brief MOCの更新
			 */
			void PreUpdate(double deltaTime) override final
			{
				moc->ClearBuffer();

				produceSlot.exchange((produceSlot.load(std::memory_order_relaxed) + 1) % RENDER_BUFFER_COUNT, std::memory_order_acq_rel);

				if(updateFunc) updateFunc(this);
			}
			/**
			 * @brief カスタム関数の更新
			 * @details RenderGraphで呼び出される
			 */
			void CallPreDrawCustomFunc() noexcept
			{
				if (preDrawFunc) preDrawFunc(this);
			}

		public:
			/**
			 * @brief RenderQueueのProducerSessionを取得する関数
			 * @param groupName　グループ名
			 * @return　RenderQueue::ProducerSession レンダークエリのプロデューサーセッション
			 */
			RenderQueue::ProducerSession GetProducerSession(const std::string& groupName)
			{
				std::shared_lock lock(*queueMutex);

				auto it = queueIndex.find(groupName);
				if (it == queueIndex.end()) {
					assert(false && "RenderQueue not found for pass name");
				}

				return renderQueues[it->second]->MakeProducer();
			}
			/**
			 * @brief RenderQueueのProducerSessionExternalを取得する関数
			 * @param groupName　グループ名
			 * @param buf バッファ
			 * @return　RenderQueue::ProducerSession レンダークエリのプロデューサーセッション
			 */
			RenderQueue::ProducerSessionExternal GetProducerSession(const std::string& groupName,
				RenderQueue::ProducerSessionExternal::SmallBuf& buf)
			{
				std::shared_lock lock(*queueMutex);

				auto it = queueIndex.find(groupName);
				if (it == queueIndex.end()) {
					assert(false && "RenderQueue not found for pass name");
				}

				return renderQueues[it->second]->MakeProducer(buf);
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
			 * @brief MOCにオクルーダーを提出する関数
			 * @param tri オクルーダーの三角形情報
			 */
			void RenderingOccluderInMOC(MocTriBatch& tri)
			{
				if (!tri.valid) return;

				moc->RenderTriangles(
					tri.clipVertices,
					tri.indices,
					tri.numTriangles,
					nullptr,
					MOC::BACKFACE_CCW
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
			/**
			* @ brief 現在の生産スロットを取得する
			* @ return int 生産スロットのインデックス
			 */
			int GetProduceSlot() const noexcept { return produceSlot.load(std::memory_order_acquire); }
			/**
			* @ brief Updateカスタム関数の設定
			* @ param func カスタム関数
			 */
			void SetCustomUpdateFunction(UpdateFuncType func) noexcept {
				updateFunc = func;
			}
			/**
			* @ brief 描画前ドローカスタム関数の設定
			* @ param func カスタム関数
			 */
			void SetCustomPreDrawFunction(PreDrawFuncType func) noexcept {
				preDrawFunc = func;
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

			MOC* moc = nullptr; // Masked Occlusion Culling のインスタンス

			std::atomic<uint16_t> produceSlot{ 0 };

			UpdateFuncType updateFunc = nullptr;
			PreDrawFuncType preDrawFunc = nullptr;
		public:
			STATIC_SERVICE_TAG
			DEFINE_UPDATESERVICE_GROUP(GROUP_GRAPHICS)
		};

		// AABBFrontFaceQuad を 2トライで提出できるバッチに変換
		inline MocQuadBatch ConvertAABBFrontFaceQuadToMoc(
			const Math::Vec4f* clips,
			const SFW::Math::Matrix<4, 4, float>& viewProj,
			float nearClipW /* = moc->GetNearClipPlane() と一致させるのが理想 */
		)
		{
			using Vec3 = SFW::Math::Vec3f;
			using Vec4 = SFW::Math::Vec4f;

			MocQuadBatch out{};

			std::memcpy(out.clip, clips, sizeof(Vec4) * 4);

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
			auto ndcXY_stable = [&](int i) {
				// CCW判定用途なので「割り算が爆発しない」ことを優先
				const float w = (std::max)(out.clip[i].w, nearClipW); // near でクランプ
				const float invw = 1.0f / w;
				return SFW::Math::Vec2f{ out.clip[i].x * invw, out.clip[i].y * invw };
				};

			auto triArea2 = [](SFW::Math::Vec2f p, SFW::Math::Vec2f q, SFW::Math::Vec2f r) {
				// 2倍面積（z成分）: (q - p) × (r - p)
				return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
				};

			const auto a = ndcXY_stable(0), b = ndcXY_stable(1), c = ndcXY_stable(2), d = ndcXY_stable(3);
			float area = triArea2(a, b, c) + triArea2(a, c, d);
			bool ccw = (area > 0.0f);
			// CCW でなければ 1 と 2 を入れ替える（0,1,2 / 2,1,3 -> 0,2,1 / 1,2,3）
			if (!ccw) {
				std::swap(out.clip[0], out.clip[2]);
			}

			// 4) インデックス（CCW）
			out.indices[0] = 0; out.indices[1] = 2; out.indices[2] = 1;
			out.indices[3] = 2; out.indices[4] = 0; out.indices[5] = 3;

			return out;
		}
	}
}

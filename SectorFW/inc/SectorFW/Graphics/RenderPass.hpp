/*****************************************************************//**
 * @file   RenderPass.hpp
 * @brief レンダーパスを定義する構造体
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <functional>
#include <vector>

#include "RenderQueue.h"

namespace SFW
{
	namespace Graphics
	{
		using PassCustomFuncType = void(*)(uint64_t);

		/**
		 * @brief 描画するパスごとの設定を定義する構造体
		 */
		template<typename RTV, typename DSV, typename SRV, typename Buffer,
			typename RTVHandle = DefaultViewHandle<RTV>,
			typename DSVHandle = DefaultViewHandle<DSV>>
		struct RenderPass {

			// ハンドル型（スマートポインタ／生ポインタなど）
			using RTVHandleT = RTVHandle;
			using DSVHandleT = DSVHandle;

			static_assert(std::is_default_constructible_v<RTVHandleT>, "RTVHandle must be default constructible");
			static_assert(std::is_default_constructible_v<DSVHandleT>, "DSVHandle must be default constructible");

			std::vector<RTVHandleT> rtvs; // RenderTargetViewハンドル
			std::vector<RTV> rtvsRaw; // 生のRTVポインタ配列（内部処理用）
			DSVHandleT dsv = nullptr; // DepthStencilViewハンドル
			RenderQueue* queue;
			PrimitiveTopology topology = PrimitiveTopology::TriangleList; // プリミティブトポロジ
			std::optional<RasterizerStateID> rasterizerState = std::nullopt; // ラスタライザーステートID
			BlendStateID blendState = BlendStateID::Opaque; // ブレンドステートID
			DepthStencilStateID depthStencilState = DepthStencilStateID::Default; // 深度ステンシルステートID
			std::optional<std::vector<BindSlotBuffer>> cbvs; // 定数バッファハンドルのリスト
			std::optional<Viewport> viewport = std::nullopt; // ビューポートのオーバーライド
			std::optional<PSOHandle> psoOverride = std::nullopt; // PSOのオーバーライド
			std::vector<PassCustomFuncType> customExecute = {};	// カスタム描画関数（FullscreenQuadなど）

			uint32_t stencilRef = 0;

			// このパスが見る viewMask のビット
			uint16_t viewBit = 0;   // 例: 1<<0=ZPre, 1<<1=Opaque, 1<<2=ID...

			/**
			 * @brief デフォルトコンストラクタ
			 */
			RenderPass() = default;
			/**
			 * @brief コンストラクタ
			 * @param name パスの名前
			 * @param rtv RenderTargetViewハンドルのリスト
			 * @param dsv DepthStencilViewハンドル
			 * @param queue 描画キュー
			 * @param topology プリミティブトポロジ
			 * @param rasterizerState ラスタライザーステートID
			 * @param blendState ブレンドステートID
			 * @param depthStencilState 深度ステンシルステートID
			 * @param cbvs 定数バッファハンドルのリスト
			 * @param customExecute カスタム描画関数（FullscreenQuadなど）
			 */
			RenderPass(
				const std::vector<RTVHandleT>& rtvs,
				DSVHandleT dsv,
				RenderQueue* queue,
				PrimitiveTopology topology = PrimitiveTopology::TriangleList,
				std::optional<RasterizerStateID> rasterizerState = std::nullopt,
				BlendStateID blendState = BlendStateID::Opaque,
				DepthStencilStateID depthStencilState = DepthStencilStateID::Default,
				std::optional<std::vector<BindSlotBuffer>> cbvs = std::nullopt,
				std::optional<Viewport> viewport = std::nullopt,
				std::optional<PSOHandle> psoOverride = std::nullopt,
				const std::vector<PassCustomFuncType>& customExecute = {},
				uint32_t stencilRef = 0)
				: rtvs(rtvs)
				, dsv(dsv)
				, queue(queue)
				, topology(topology)
				, rasterizerState(rasterizerState)
				, blendState(blendState)
				, depthStencilState(depthStencilState)
				, cbvs(cbvs)
				, viewport(viewport)
				, psoOverride(psoOverride)
				, customExecute(customExecute)
				, stencilRef(stencilRef){

				rtvsRaw.resize(rtvs.size());
				for (size_t i = 0; i < rtvs.size(); ++i) {
					rtvsRaw[i] = rtvs[i].Get();
				}
			}

			/**
			 * @brief ムーブコンストラクタ
			 * @param other ムーブ元のRenderPass
			 * @return RenderPass&& ムーブされたRenderPass
			 */
			RenderPass(RenderPass&& other) noexcept
				: rtvs(std::move(other.rtvs))
				, dsv(other.dsv)
				, queue(std::move(other.queue))
				, topology(other.topology)
				, rasterizerState(other.rasterizerState)
				, cbvs(std::move(other.cbvs))
				, viewport(other.viewport)
				, psoOverride(other.psoOverride)
				, customExecute(std::move(other.customExecute))
				, stencilRef(other.stencilRef){
				other.dsv = nullptr; // 安全のためヌルクリア
				queue = other.queue;

				rtvsRaw.resize(rtvs.size());
				for (size_t i = 0; i < rtvs.size(); ++i) {
					rtvsRaw[i] = rtvs[i].Get();
				}
			}

			/**
			 * @brief ムーブ代入演算子
			 * @param other ムーブ元のRenderPass
			 * @return RenderPass& ムーブされたRenderPassの参照
			 */
			RenderPass& operator=(RenderPass&& other) noexcept {
				if (this != &other) {
					rtvs = std::move(other.rtvs);
					dsv = other.dsv;
					queue = other.queue;
					queue = std::move(other.queue);
					topology = other.topology;
					rasterizerState = other.rasterizerState;
					cbvs = std::move(other.cbvs);
					viewport = other.viewport;
					psoOverride = other.psoOverride;
					customExecute = other.customExecute;
					stencilRef = other.stencilRef;
					other.dsv = nullptr;

					rtvsRaw.resize(rtvs.size());
					for (size_t i = 0; i < rtvs.size(); ++i) {
						rtvsRaw[i] = rtvs[i].Get();
					}
				}
				return *this;
			}

			/**
			 * @brief コピー禁止
			 */
			RenderPass(const RenderPass&) = delete;
			RenderPass& operator=(const RenderPass&) = delete;
		};

		/**
		 * @brief レンダーパスの設定を定義する構造体
		 * @detial ラスタライズを指定しない場合PSOで指定したラスタライズを使用する
		 */
		template<typename RTV, typename DSV, template <typename> class ViewHandle = DefaultViewHandle>
		struct RenderPassDesc {

			// ハンドル型（スマートポインタ／生ポインタなど）
			using RTVHandleT = ViewHandle<std::remove_pointer_t<RTV>>;
			using DSVHandleT = ViewHandle<std::remove_pointer_t<DSV>>;

			static_assert(std::is_default_constructible_v<RTVHandleT>, "RTVHandle must be default constructible");
			static_assert(std::is_default_constructible_v<DSVHandleT>, "DSVHandle must be default constructible");

			std::vector<RTVHandleT> rtvs; // RenderTargetViewハンドル
			DSVHandleT dsv = nullptr; // DepthStencilViewハンドル
			PrimitiveTopology topology = PrimitiveTopology::TriangleList; // プリミティブトポロジ
			std::optional<RasterizerStateID> rasterizerState = std::nullopt; // ラスタライザーステートID
			BlendStateID blendState = BlendStateID::Opaque; // ブレンドステートID
			DepthStencilStateID depthStencilState = DepthStencilStateID::Default; // 深度ステンシルステートID
			std::optional<std::vector<BindSlotBuffer>> cbvs; // 定数バッファハンドルのリスト
			std::optional<Viewport> viewport = std::nullopt; // ビューポート設定
			std::optional<PSOHandle> psoOverride = std::nullopt; // PSOのオーバーライド
			std::vector<PassCustomFuncType> customExecute = {}; // FullscreenQuadなど
			uint32_t stencilRef = 0;
		};
	}
}
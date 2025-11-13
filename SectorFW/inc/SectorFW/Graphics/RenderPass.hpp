/*****************************************************************//**
 * @file   RenderPass.hpp
 * @brief レンダーパスを定義する構造体
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <functional>

#include "RenderQueue.h"

namespace SFW
{
	namespace Graphics
	{
		/**
		 * @brief 描画するパスごとの設定を定義する構造体
		 */
		template<typename RTV, typename SRV, typename Buffer>
		struct RenderPass {
			std::string name;
			std::vector<RTV> rtvs; // RenderTargetViewハンドル
			void* dsv = nullptr; // DepthStencilViewハンドル
			RenderQueue* queue;
			PrimitiveTopology topology = PrimitiveTopology::TriangleList; // プリミティブトポロジ
			std::optional<RasterizerStateID> rasterizerState = std::nullopt; // ラスタライザーステートID
			BlendStateID blendState = BlendStateID::Opaque; // ブレンドステートID
			DepthStencilStateID depthStencilState = DepthStencilStateID::Default; // 深度ステンシルステートID
			std::vector<BufferHandle> cbvs; // 定数バッファハンドルのリスト
			std::optional<PSOHandle> psoOverride = std::nullopt; // PSOのオーバーライド
			std::function<void(uint64_t)> customExecute; // FullscreenQuadなど

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
				const std::string& name,
				const std::vector<RTV>& rtvs,
				void* dsv,
				RenderQueue* queue,
				PrimitiveTopology topology = PrimitiveTopology::TriangleList,
				std::optional<RasterizerStateID> rasterizerState = std::nullopt,
				BlendStateID blendState = BlendStateID::Opaque,
				DepthStencilStateID depthStencilState = DepthStencilStateID::Default,
				const std::vector<BufferHandle>& cbvs = {},
				std::optional<PSOHandle> psoOverride = std::nullopt,
				std::function<void(uint64_t)> customExecute = nullptr)
				: name(name)
				, rtvs(rtvs)
				, dsv(dsv)
				, queue(queue)
				, topology(topology)
				, rasterizerState(rasterizerState)
				, blendState(blendState)
				, depthStencilState(depthStencilState)
				, cbvs(cbvs)
				, psoOverride(psoOverride)
				, customExecute(customExecute) {
			}

			/**
			 * @brief ムーブコンストラクタ
			 * @param other ムーブ元のRenderPass
			 * @return RenderPass&& ムーブされたRenderPass
			 */
			RenderPass(RenderPass&& other) noexcept
				: name(std::move(other.name))
				, rtvs(std::move(other.rtvs))
				, dsv(other.dsv)
				, queue(std::move(other.queue))
				, topology(other.topology)
				, rasterizerState(other.rasterizerState)
				, cbvs(std::move(other.cbvs))
				, psoOverride(other.psoOverride)
				, customExecute(std::move(other.customExecute)) {
				other.dsv = nullptr; // 安全のためヌルクリア
				queue = other.queue;
			}

			/**
			 * @brief ムーブ代入演算子
			 * @param other ムーブ元のRenderPass
			 * @return RenderPass& ムーブされたRenderPassの参照
			 */
			RenderPass& operator=(RenderPass&& other) noexcept {
				if (this != &other) {
					name = std::move(other.name);
					rtvs = std::move(other.rtvs);
					dsv = other.dsv;
					queue = other.queue;
					queue = std::move(other.queue);
					topology = other.topology;
					rasterizerState = other.rasterizerState;
					cbvs = std::move(other.cbvs);
					psoOverride = other.psoOverride;
					customExecute = std::move(other.customExecute);
					other.dsv = nullptr;
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
		template<typename RTV>
		struct RenderPassDesc {
			std::string name;
			std::vector<RTV> rtvs; // RenderTargetViewハンドル
			void* dsv = nullptr; // DepthStencilViewハンドル
			PrimitiveTopology topology = PrimitiveTopology::TriangleList; // プリミティブトポロジ
			std::optional<RasterizerStateID> rasterizerState = std::nullopt; // ラスタライザーステートID
			BlendStateID blendState = BlendStateID::Opaque; // ブレンドステートID
			DepthStencilStateID depthStencilState = DepthStencilStateID::Default; // 深度ステンシルステートID
			std::vector<BufferHandle> cbvs; // 定数バッファハンドルのリスト
			std::optional<PSOHandle> psoOverride = std::nullopt; // PSOのオーバーライド
			std::function<void(uint64_t)> customExecute; // FullscreenQuadなど
		};
	}
}
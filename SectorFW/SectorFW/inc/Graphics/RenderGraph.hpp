#pragma once

#include <functional>
#include <optional>

#include "RenderQueue.hpp"
#include "RenderService.h"

namespace SectorFW
{
	namespace Graphics
	{
		template<typename RTV, typename SRV, typename Buffer>
		struct RenderPass {
			std::string name;
			std::vector<RTV> rtvs; // RenderTargetViewハンドル
			void* dsv = nullptr; // DepthStencilViewハンドル
			RenderQueue queue;
			PrimitiveTopology topology = PrimitiveTopology::TriangleList; // プリミティブトポロジ
			std::optional<RasterizerStateID> rasterizerState = RasterizerStateID::SolidCullBack; // ラスタライザーステートID
			BlendStateID blendState = BlendStateID::Opaque; // ブレンドステートID
			DepthStencilStateID depthStencilState = DepthStencilStateID::Default; // 深度ステンシルステートID
			std::vector<BufferHandle> cbvs; // 定数バッファハンドルのリスト
			std::function<void()> customExecute; // FullscreenQuadなど

			// ムーブコンストラクタ
			RenderPass(RenderPass&& other) noexcept
				: name(std::move(other.name))
				, rtvs(std::move(other.rtvs))
				, dsv(other.dsv)
				, queue(std::move(other.queue))
				, topology(other.topology)
				, cbvs(std::move(other.cbvs))
				, customExecute(std::move(other.customExecute)) {
				other.dsv = nullptr; // 安全のためヌルクリア
			}

			// ムーブ代入演算子
			RenderPass& operator=(RenderPass&& other) noexcept {
				if (this != &other) {
					name = std::move(other.name);
					rtvs = std::move(other.rtvs);
					dsv = other.dsv;
					queue = std::move(other.queue);
					topology = other.topology;
					cbvs = std::move(other.cbvs);
					customExecute = std::move(other.customExecute);
					other.dsv = nullptr;
				}
				return *this;
			}

			// デフォルトコンストラクタ
			RenderPass() = default;

			// コピー禁止にしたい場合（必要に応じて）
			RenderPass(const RenderPass&) = delete;
			RenderPass& operator=(const RenderPass&) = delete;
		};

		template<typename Backend, PointerType RTV, PointerType SRV, PointerType Buffer>
		class RenderGraph {
		public:
			using PassType = RenderPass<RTV, SRV, Buffer>;

			RenderGraph(Backend& backend) : backend(backend) {
				// レンダーバックエンドでRenderServiceにリソースマネージャーを登録
				backend.AddResourceManagerToRenderService(*this);
			}

			void AddPass(const std::string& name,
				const std::vector<RTV>& rtvs,
				void* dsv,
				std::vector<BufferHandle>&& cbvs = {}) {
				PassType pass;
				pass.name = name;
				pass.rtvs = rtvs;
				pass.dsv = dsv;
				pass.cbvs = std::move(cbvs);
				passes.push_back(std::move(pass));

				std::unique_lock lock(*renderService.queueMutex);
				renderService.renderQueues.push_back(passes.back().queue);
				renderService.queueIndex[name] = renderService.renderQueues.size() - 1; // レンダーサービスにキューを登録
			}

			PassType& GetPass(const std::string& name) {
				for (auto& p : passes) {
					if (p.name == name) return p;
				}
				assert(false && "Pass not found");
				return passes[0];
			}

			void Execute() {
				//フレームのインクリメントとリソースの破棄処理
				backend.ProcessDeferredDeletes(++renderService.currentFrame);

				for (auto& pass : passes) {
					backend.SetPrimitiveTopology(pass.topology);

					bool useRasterizer = pass.rasterizerState.has_value();
					if (useRasterizer)
						backend.SetRasterizerState(*pass.rasterizerState);

					backend.SetBlendState(pass.blendState); // デフォルトのブレンドステートを使用

					backend.SetDepthStencilState(pass.depthStencilState);

					backend.SetRenderTargets(pass.rtvs, pass.dsv);

					backend.BindGlobalCBVs(pass.cbvs);

					std::vector<DrawCommand> cmds;
					pass.queue.Submit(cmds);
					backend.ExecuteDrawInstanced(cmds, !useRasterizer); // インスタンシング対応

					if (pass.customExecute) pass.customExecute();
				}
			}

			RenderService* GetRenderService() {
				return &renderService;
			}

			template<typename ResourceType>
			void RegisterResourceManager(ResourceType* manager) {
				renderService.RegisterResourceManager(manager);
			}

		private:
			Backend& backend;
			std::vector<PassType> passes;
			RenderService renderService; // レンダーサービスのインスタンス
		};
	}
}
#pragma once

#include <functional>

#include "RenderQueue.hpp"
#include "RenderService.h"

namespace SectorFW
{
	namespace Graphics
	{
		template<typename RTV, typename SRV>
		struct RenderPass {
			std::string name;
			std::vector<RTV> rtvs; // RenderTargetViewハンドル
			void* dsv = nullptr; // DepthStencilViewハンドル
			RenderQueue queue;
			std::function<void()> customExecute; // FullscreenQuadなど

			// ムーブコンストラクタ
			RenderPass(RenderPass&& other) noexcept
				: name(std::move(other.name))
				, rtvs(std::move(other.rtvs))
				, dsv(other.dsv)
				, queue(std::move(other.queue))
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

		template<typename Backend, PointerType RTV, PointerType SRV>
		class RenderGraph {
		public:
			using PassType = RenderPass<RTV, SRV>;

			RenderGraph(Backend& backend) : backend(backend) {
				// レンダーバックエンドでRenderServiceにリソースマネージャーを登録
				backend.AddResourceManagerToRenderService(*this);
			}

			void AddPass(const std::string& name,
				const std::vector<RTV>& rtvs,
				void* dsv) {
				RenderPass<RTV, SRV> pass;
				pass.name = name;
				pass.rtvs = rtvs;
				pass.dsv = dsv;
				passes.push_back(std::move(pass));

				std::unique_lock lock(*renderService.queueMutex);
				renderService.renderQueues.push_back(passes.back().queue);
				renderService.queueIndex[name] = renderService.renderQueues.size() - 1; // レンダーサービスにキューを登録
			}

			RenderPass<RTV, SRV>& GetPass(const std::string& name) {
				for (auto& p : passes) {
					if (p.name == name) return p;
				}
				assert(false && "Pass not found");
				return passes[0];
			}

			void Execute() {
				for (auto& pass : passes) {
					backend.SetRenderTargets(pass.rtvs, pass.dsv);

					std::vector<DrawCommand> cmds;
					pass.queue.Submit(cmds);
					backend.ExecuteDrawInstanced(cmds); // インスタンシング対応

					if (pass.customExecute) pass.customExecute();
				}
				//フレームのインクリメントとリソースの破棄処理
				backend.ProcessDeferredDeletes(++renderService.currentFrame);
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
			std::vector<RenderPass<RTV, SRV>> passes;
			RenderService renderService; // レンダーサービスのインスタンス
		};
	}
}
#pragma once

#include <functional>
#include <optional>

#include "RenderQueue.h"
#include "RenderService.h"

#include "Debug/ImGuiLayer.h"
#ifdef _ENABLE_IMGUI
#include "Debug/UIBus.h"
#endif

namespace SectorFW
{
	namespace Graphics
	{
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
			std::function<void()> customExecute; // FullscreenQuadなど

			// デフォルトコンストラクタ
			RenderPass() = default;

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
				std::function<void()> customExecute = nullptr)
				: name(name)
				, rtvs(rtvs)
				, dsv(dsv)
				, queue(queue)
				, topology(topology)
				, rasterizerState(rasterizerState)
				, blendState(blendState)
				, depthStencilState(depthStencilState)
				, cbvs(cbvs)
				, customExecute(customExecute) {
			}

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
				queue = other.queue;
			}

			// ムーブ代入演算子
			RenderPass& operator=(RenderPass&& other) noexcept {
				if (this != &other) {
					name = std::move(other.name);
					rtvs = std::move(other.rtvs);
					dsv = other.dsv;
					queue = other.queue;
					queue = std::move(other.queue);
					topology = other.topology;
					cbvs = std::move(other.cbvs);
					customExecute = std::move(other.customExecute);
					other.dsv = nullptr;
				}
				return *this;
			}

			// コピー禁止にしたい場合（必要に応じて）
			RenderPass(const RenderPass&) = delete;
			RenderPass& operator=(const RenderPass&) = delete;
		};

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
			uint32_t maxInstancesPerFrame = MAX_INSTANCES_PER_FRAME; // フレーム当たりの最大インスタンス数
			std::function<void()> customExecute; // FullscreenQuadなど
		};

		template<typename Backend, PointerType RTV, PointerType SRV, PointerType Buffer>
		class RenderGraph {
		public:
			using PassType = RenderPass<RTV, SRV, Buffer>;

			RenderGraph(Backend& backend) : backend(backend) {
				// レンダーバックエンドでRenderServiceにリソースマネージャーを登録
				backend.AddResourceManagerToRenderService(*this);
			}

			void AddPass(RenderPassDesc<RTV>& desc) {
				std::unique_lock lock(*renderService.queueMutex);

				renderService.renderQueues.emplace_back(std::make_unique<RenderQueue>(desc.maxInstancesPerFrame));
				renderService.queueIndex[desc.name] = renderService.renderQueues.size() - 1; // レンダーサービスにキューを登録

				passes.emplace_back(
					desc.name,
					desc.rtvs,
					desc.dsv,
					renderService.renderQueues.back().get(),
					desc.topology,
					desc.rasterizerState,
					desc.blendState,
					desc.depthStencilState,
					desc.cbvs,
					desc.customExecute);
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

#ifdef _ENABLE_IMGUI
				{
					auto g = Debug::BeginTreeWrite(); // lock & back buffer
					auto& frame = g.data();

					// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
					frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::RenderGraph, /*leaf=*/false, "RenderGraph" });
				} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif

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
					const InstanceData* instances = nullptr;
					uint32_t instCount = 0;

					pass.queue->Submit(cmds, instances, instCount);

#ifdef _ENABLE_IMGUI
					{
						auto g = Debug::BeginTreeWrite(); // lock & back buffer
						auto& frame = g.data();

						// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
						frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::Pass, /*leaf=*/false, "Pass : " + pass.name });
						frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::DrawCommand, /*leaf=*/true, "DrawCommand : " + std::to_string(cmds.size()) });
					} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif

					backend.BeginFrameUpload(instances, instCount);
					backend.ExecuteDrawIndexedInstanced(cmds, !useRasterizer); // インスタンシング対応

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
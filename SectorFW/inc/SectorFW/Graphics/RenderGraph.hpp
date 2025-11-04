/*****************************************************************//**
 * @file   RenderGraph.hpp
 * @brief レンダーグラフを定義するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <optional>

#include "RenderPass.hpp"
#include "RenderQueue.h"
#include "RenderService.h"

#include "Debug/ImGuiLayer.h"
#ifdef _ENABLE_IMGUI
#include "Debug/UIBus.h"
#endif

#ifndef NO_USE_PMR_RENDER_QUEUE
#include <memory_resource>
#endif
#include <memory>

namespace SFW
{
	namespace Graphics
	{
		/**
		 * @brief RenderPassごとに管理して描画を行うクラス
		 */
		template<typename Backend, PointerType RTV, PointerType SRV, PointerType Buffer>
		class RenderGraph {
		public:
			using PassType = RenderPass<RTV, SRV, Buffer>;
			static constexpr uint16_t kFlights = RENDER_BUFFER_COUNT; // フレームインフライト数

			/**
			 * @brief コンストラクタ
			 * @detail RenderGraphにResouceManagerの追加を行う
			 * @param backend レンダーバックエンド
			 */
			explicit RenderGraph(Backend& backend, MOC* moc) : backend(backend), renderService(moc) {
				// レンダーバックエンドでRenderServiceにリソースマネージャーを登録
				backend.AddResourceManagerToRenderService(*this);

				sharedInstanceArena = std::make_unique<SharedInstanceArena>();
			}

			/**
			 * @brief RenderPassの追加
			 * @param desc レンダーパスの詳細
			 */
			void AddPass(RenderPassDesc<RTV>& desc) {
				std::unique_lock lock(*renderService.queueMutex);

				renderService.renderQueues.emplace_back(std::make_unique<RenderQueue>(renderService.produceSlot, sharedInstanceArena.get(), desc.maxInstancesPerFrame));
				renderService.queueIndex[desc.name] = renderService.renderQueues.size() - 1; // レンダーサービスにキューを登録

				passes.push_back(std::make_unique<PassType>(
					desc.name,
					desc.rtvs,
					desc.dsv,
					renderService.renderQueues.back().get(),
					desc.topology,
					desc.rasterizerState,
					desc.blendState,
					desc.depthStencilState,
					desc.cbvs,
					desc.customExecute));

#ifndef NO_USE_PMR_RENDER_QUEUE
				// パスごとのランタイムを初期化（常駐アリーナを作っておく）
				PassRuntime rt{};
				rt.name = desc.name;
				rt.hint = std::max<size_t>(128 * 1024, desc.maxInstancesPerFrame * sizeof(DrawCommand) / 2);
				for (uint32_t i = 0; i < kFlights; ++i) {
					rt.perFlight[i].init(rt.hint);
				}
				runtimes.emplace_back(std::move(rt));
#endif //NO_USE_PMR_RENDER_QUEUE
			}
			/**
			 * @brief パスの取得
			 * @param name パスの名前
			 * @return PassType& パスの参照
			 */
			PassType& GetPass(const std::string& name) {
				for (auto& p : passes) {
					if (p->name == name) return p;
				}
				assert(false && "Pass not found");
				return *passes[0];
			}
			/**
			 * @brief 描画の実行
			 */
			void Execute() {
				//フレームのインクリメントとリソースの破棄処理
				backend.ProcessDeferredDeletes(++currentFrame);

#ifdef _ENABLE_IMGUI
				{
					auto g = Debug::BeginTreeWrite(); // lock & back buffer
					auto& frame = g.data();

					// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
					frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::RenderGraph, /*leaf=*/false, "RenderGraph" });
				} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif // _ENABLE_IMGUI

				uint16_t prevSlot = consumeSlot.exchange((consumeSlot.load(std::memory_order_relaxed) + 1) % RENDER_BUFFER_COUNT, std::memory_order_acq_rel);

				backend.BeginFrameUpload(sharedInstanceArena->Data(prevSlot), sharedInstanceArena->Size(prevSlot));
				sharedInstanceArena->ResetSlot(prevSlot);

				renderService.CallPreDrawCustomFunc(); // カスタム関数の更新

				for (auto& pass : passes) {
					backend.SetPrimitiveTopology(pass->topology);

					bool useRasterizer = pass->rasterizerState.has_value();
					if (useRasterizer)
						backend.SetRasterizerState(*pass->rasterizerState);

					backend.SetBlendState(pass->blendState); // デフォルトのブレンドステートを使用

					backend.SetDepthStencilState(pass->depthStencilState);

					backend.SetRenderTargets(pass->rtvs, pass->dsv);

					backend.BindGlobalCBVs(pass->cbvs);

#ifndef NO_USE_PMR_RENDER_QUEUE
					// 常駐アリーナ＆cmdsを取得（再構築しない）
					PassRuntime* rt = getRuntime(pass->name);
					const uint32_t flight = currentFrame % kFlights;
					auto& pf = rt->perFlight[flight];
					pf.release();              // メモリは解放せず、ポインタだけ巻き戻す
					auto& cmds = pf.cmds;      // 容量は保持。clear()のみ
					cmds.clear();
#else
					std::vector<DrawCommand> cmds;
#endif //NO_USE_PMR_RENDER_QUEUE


					pass->queue->Submit(prevSlot, cmds);

#ifdef _ENABLE_IMGUI
					{
						auto g = Debug::BeginTreeWrite(); // lock & back buffer
						auto& frame = g.data();

						// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
						frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::Pass, /*leaf=*/false, "Pass : " + pass->name });
						frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::DrawCommand, /*leaf=*/true, "DrawCommand : " + std::to_string(cmds.size()) });
					} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif // _ENABLE_IMGUI

					backend.ExecuteDrawIndexedInstanced(cmds, !useRasterizer); // インスタンシング対応

					if (pass->customExecute) pass->customExecute(currentFrame);

#ifndef NO_USE_PMR_RENDER_QUEUE
					// 使用量からヒント更新 & 必要時のみ拡張
					auto round_up = [](size_t x, size_t a) { return (x + (a - 1)) & ~(a - 1); };
					size_t used = rt->perFlight[currentFrame % kFlights].tracker.used;
					size_t next = round_up(static_cast<size_t>(used * 5 / 4), 64 * 1024); // 1.25x
					constexpr size_t kMin = 128 * 1024, kMax = 32ull * 1024 * 1024;
					next = (std::min)((std::max)(next, kMin), kMax);
					size_t prev = rt->hint;
					if (prev == 0 || next >= prev / 2) rt->hint = next; else rt->hint = prev / 2;

					// ヒントが現在の初期バッファより大きくなったら“まれに”拡張
					const uint32_t f0 = (currentFrame % kFlights);
					if (rt->needs_grow()) {
						for (uint32_t i = 0; i < kFlights; ++i) {
							rt->perFlight[i].maybe_grow(rt->hint);
						}
					}
#endif //NO_USE_PMR_RENDER_QUEUE
				}
			}
			/**
			 * @brief レンダーサービスの取得
			 * @return RenderService* レンダーサービスのポインタ
			 */
			RenderService* GetRenderService() {
				return &renderService;
			}
			/**
			 * @brief リソースマネージャーの登録
			 * @param manager リソースマネージャーのポインタ
			 */
			template<typename ResourceType>
			void RegisterResourceManager(ResourceType* manager) {
				renderService.RegisterResourceManager(manager);
			}

		private:
			Backend& backend;
			std::atomic<uint16_t> consumeSlot{ 1 }; // produceの方のスロットが0 + 1から始まるのでため1スタート
			std::unique_ptr<SharedInstanceArena> sharedInstanceArena; // フレーム共有インスタンスアリーナ
			std::vector<std::unique_ptr<PassType>> passes;
			RenderService renderService; // レンダーサービスのインスタンス
			uint64_t currentFrame = 0; // 現在のフレーム番号 RenderGraphの描画前に更新している

#ifndef NO_USE_PMR_RENDER_QUEUE
			struct PassRuntime {
				struct TrackingResource : std::pmr::memory_resource {
					std::pmr::memory_resource* upstream{};
					size_t used = 0;
					explicit TrackingResource(std::pmr::memory_resource* up = std::pmr::get_default_resource()) : upstream(up) {}
				private:
					void* do_allocate(size_t bytes, size_t align) override {
						used += ((bytes + align - 1) / align) * align;
						return upstream->allocate(bytes, align);
					}
					void do_deallocate(void* p, size_t bytes, size_t align) override {
						upstream->deallocate(p, bytes, align);
					}
					bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
						return this == &other;
					}
				};

				static constexpr size_t kStackBuf = 256 * 1024;
				struct PerFlight {
					// 初期バッファ（stack か heap のどちらかを使用）
					alignas(64) std::byte stack[kStackBuf] = {};
					std::unique_ptr<std::byte[]> heap{};
					size_t heapSize{ 0 };
					TrackingResource tracker{ std::pmr::get_default_resource() };
					// arena は「初期バッファ」を参照する。基本は再構築せず release() でリセット
					std::pmr::monotonic_buffer_resource* arena{ nullptr };
					// cmds は arena 常駐の pmr::vector
					std::pmr::vector<DrawCommand> cmds{ std::pmr::get_default_resource() };

					void init(size_t hint) {
						void* initialBuf = stack;
						size_t initialSize = kStackBuf;
						if (hint > kStackBuf) {
							heapSize = hint;
							heap = std::make_unique<std::byte[]>(heapSize);
							initialBuf = heap.get();
							initialSize = heapSize;
						}
						// arena を配置new（以後は release でリセット）
						arenaStorage = std::make_unique<std::pmr::monotonic_buffer_resource>(initialBuf, initialSize, &tracker);
						arena = arenaStorage.get();
						cmds = std::pmr::vector<DrawCommand>{ arena };
						tracker.used = 0;
					}

					void release() {
						tracker.used = 0;
						arena->release(); // 実メモリは保持。次の allocate はO(1)
						cmds.clear();
					}

					void maybe_grow(size_t newHint) {
						if (newHint <= kStackBuf || newHint <= heapSize) return;
						heapSize = newHint;
						heap = std::make_unique<std::byte[]>(heapSize);
						// arena を作り直す（成長時だけ・まれ）
						arenaStorage = std::make_unique<std::pmr::monotonic_buffer_resource>(heap.get(), heapSize, &tracker);
						arena = arenaStorage.get();
						cmds = std::pmr::vector<DrawCommand>{ arena };
						tracker.used = 0;
					}
				private:
					std::unique_ptr<std::pmr::monotonic_buffer_resource> arenaStorage{};
				};

				std::string name;
				size_t hint{ 0 };
				std::vector<PerFlight> perFlight = std::vector<PerFlight>(kFlights);

				bool needs_grow() const {
					for (uint32_t i = 0; i < kFlights; ++i) {
						// 直近 used が hint に近い or 超えたフライトがあるなら拡張候補
						if (perFlight[i].tracker.used > hint) return true;
					}
					return false;
				}
			};

			std::vector<PassRuntime> runtimes;

			PassRuntime* getRuntime(const std::string& name) {
				for (auto& r : runtimes) if (r.name == name) return &r;
				assert(false && "runtime not found");
				return &runtimes.front();
			}
#endif //NO_USE_PMR_RENDER_QUEUE
		};
	}
}
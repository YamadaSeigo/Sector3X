/*****************************************************************//**
 * @file   RenderGraph.hpp
 * @brief レンダーグラフを定義するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <optional>
#include <span>
#include <bit>

#include "RenderPass.hpp"
#include "RenderQueue.h"
#include "RenderService.h"

#include "../Debug/ImGuiLayer.h"
#ifdef _ENABLE_IMGUI
#include "../Debug/UIBus.h"
#endif

#define NO_USE_PMR_RENDER_QUEUE

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
		template<typename Backend, PointerType RTV, PointerType DSV, PointerType SRV, PointerType Buffer, template <typename> class ViewHandle>
		class RenderGraph {
		public:
			using PassType = RenderPass<RTV, DSV, SRV, Buffer, ViewHandle<std::remove_pointer_t<RTV>>, ViewHandle<std::remove_pointer_t<DSV>>>;
			static constexpr uint16_t kFlights = RENDER_BUFFER_COUNT; // フレームインフライト数

			struct PassGroup {
				std::string name;
				RenderQueue* queue = nullptr;          // グループ内共通キュー
				std::vector<PassType*> passes;         // このグループに属するパス
			};

			struct PassNode {
				uint32_t groupIndex;  // GroupA or GroupB …
				uint32_t passIndex;   // そのグループ内の何番目のPassか
			};

			struct PassViewRange {
				uint32_t offset; // indices[] の何番目から
				uint32_t count;  // 何個分
			};

			struct GroupState {
				std::vector<DrawCommand> cmds;          // Submit結果
				std::vector<uint32_t>    indices;       // 全パス共通の index バッファ
				std::vector<PassViewRange> ranges;      // [pass] → (offset,count)
			};

			/**
			 * @brief コンストラクタ
			 * @details RenderGraphにResouceManagerの追加を行う
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
			void AddPass(RenderPassDesc<RTV, DSV, ViewHandle>& desc) {
				std::unique_lock lock(*renderService.queueMutex);

				renderService.renderQueues.emplace_back(std::make_unique<RenderQueue>(renderService.produceSlot, sharedInstanceArena.get(), MAX_INSTANCES_PER_FRAME));
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
					desc.viewport,
					desc.psoOverride,
					desc.customExecute,
					desc.stencilRef));

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

			[[nodiscard]] PassGroup& AddPassGroup(const std::string& name, uint32_t maxInstancesPerFrame = MAX_INSTANCES_PER_FRAME)
			{
				// このグループ専用の RenderQueue を1本作る
				renderService.renderQueues.push_back(
					std::make_unique<RenderQueue>(renderService.produceSlot, sharedInstanceArena.get(), maxInstancesPerFrame)
				);
				renderService.queueIndex[name] = renderService.renderQueues.size() - 1; // レンダーサービスにキューを登録

				auto* rq = renderService.renderQueues.back().get();

				groups.push_back(PassGroup{ name, rq, {} });

				// groups と groupStates を同じサイズに揃える
				groupStates.resize(groups.size());

				return groups.back();
			}

			PassType& AddPassToGroup(
				PassGroup& group,
				const RenderPassDesc<RTV, DSV, ViewHandle>& desc,
				uint16_t viewBit       // このパス用のビット
			) {
				passes.push_back(std::make_unique<PassType>(
					desc.rtvs,
					desc.dsv,
					group.queue,              // 同じキューを共有
					desc.topology,
					desc.rasterizerState,
					desc.blendState,
					desc.depthStencilState,
					desc.cbvs,
					desc.viewport,
					desc.psoOverride,
					desc.customExecute,
					desc.stencilRef
				));

				auto* pass = passes.back().get();
				pass->viewBit = viewBit;
				group.passes.push_back(pass);
				return *pass;
			}

			void SetExecutionOrder(const std::vector<PassNode>& order) {
				executionOrder = order;
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

			bool BindPassGlobalResources(PassType* pass) {
				// パス固有の状態をセット
				backend.SetPrimitiveTopology(pass->topology);
				bool useRasterizer = pass->rasterizerState.has_value();
				if (useRasterizer)
					backend.SetRasterizerState(*pass->rasterizerState);
				backend.SetBlendState(pass->blendState);
				backend.SetDepthStencilState(pass->depthStencilState, pass->stencilRef);
				backend.SetRenderTargets(pass->rtvsRaw, pass->dsv.Get());

				if (pass->cbvs.has_value())
					backend.BindGlobalVSCBVs(pass->cbvs.value());

				if (pass->viewport.has_value())
					backend.SetViewport(pass->viewport.value());

				return useRasterizer;
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
					frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_RENDERGRAPH, /*leaf=*/false, "RenderGraph" });
				} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif // _ENABLE_IMGUI

				uint16_t prevSlot = consumeSlot.exchange((consumeSlot.load(std::memory_order_relaxed) + 1) % RENDER_BUFFER_COUNT, std::memory_order_acq_rel);

				backend.BeginFrameUpload(sharedInstanceArena->Data(prevSlot), sharedInstanceArena->Size(prevSlot));
				sharedInstanceArena->ResetSlot(prevSlot);

				renderService.CallPreDrawCustomFunc(prevSlot); // カスタム関数の更新


				// グループごとに Submit して cmds を埋める
				for (size_t gi = 0; gi < groups.size(); ++gi) {
					auto& g = groups[gi];
					auto& gs = groupStates[gi];

					// 中身だけ消す。capacity は保持される。
					gs.cmds.clear();
					gs.indices.clear();
					gs.ranges.clear();

					g.queue->Submit(prevSlot, gs.cmds);   // ここで1回だけ取り出す

#ifdef _ENABLE_IMGUI
					{
						auto guard = Debug::BeginTreeWrite(); // lock & back buffer
						auto& frame = guard.data();

						// 例えばプリオーダ＋depth 指定で平坦化したツリーを詰める
						frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_GROUP, /*leaf=*/false, "Group : " + g.name });
						frame.items.push_back({ /*id=*/frame.items.size(), /*depth=*/Debug::WorldTreeDepth::TREEDEPTH_DRAWCOMMAND, /*leaf=*/true, "DrawCommand : " + std::to_string(gs.cmds.size()) });
					} // guard のデストラクトで unlock。swap は UI スレッドで。
#endif // _ENABLE_IMGUI

					const size_t passCount = g.passes.size();
					gs.ranges.resize(passCount);

					if (gs.cmds.empty() || passCount == 0) continue;

					// --- 1st pass: 各パスがいくつ index を持つかカウント ---
					std::vector<uint32_t> counts(passCount, 0);

					for (uint32_t ci = 0; ci < (uint32_t)gs.cmds.size(); ++ci) {
						uint16_t m = gs.cmds[ci].viewMask;
						while (m > 0) {
							uint16_t p = std::countr_zero(m);

							m &= (m - 1);                        // 最下位の1ビットを落とす

							//passIndex がそのままbitならこれでOK
							counts[p]++;  // C++20 (MSVCなら _tzcnt_u32 など)
						}
					}

					// --- 2nd: prefix-sum でオフセット決定 ---
					uint32_t total = 0;
					for (size_t p = 0; p < passCount; ++p) {
						gs.ranges[p].offset = total;
						gs.ranges[p].count = counts[p];
						total += counts[p];
					}

					gs.indices.resize(total);       // ここだけ 1回の確保

					// カーソル（書き込み位置）
					std::vector<uint32_t> cursor(passCount);
					for (size_t p = 0; p < passCount; ++p)
						cursor[p] = gs.ranges[p].offset;

					// --- 3rd: 実際に index を突っ込む ---
					for (uint32_t ci = 0; ci < (uint32_t)gs.cmds.size(); ++ci) {
						uint16_t m = gs.cmds[ci].viewMask;
						while (m > 0) {
							uint16_t p = std::countr_zero(m);

							m &= (m - 1);                        // 最下位の1ビットを落とす

							gs.indices[cursor[p]++] = ci;
						}
					}
				}

				for (const auto& node : executionOrder) {
					auto& g = groups[node.groupIndex];
					auto& gs = groupStates[node.groupIndex];

					auto* pass = g.passes[node.passIndex];
					const auto& r = gs.ranges[node.passIndex];

					if (r.count == 0) {
						if (!pass->customExecute.empty()) {
							BindPassGlobalResources(pass);

							for (auto& func : pass->customExecute)
								func(currentFrame);
						}

						continue;
					}

					bool useRasterizer = BindPassGlobalResources(pass);

					// このパス用の indexView
					auto* idxBegin = gs.indices.data() + r.offset;
					auto* idxEnd = idxBegin + r.count;

					// 共通cmds + このパスの indexビューで描画
					backend.ExecuteDrawIndexedInstanced(gs.cmds, std::span<const uint32_t>(idxBegin, idxEnd), pass->psoOverride, !useRasterizer);

					for (auto& func : pass->customExecute)
						func(currentFrame);
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

			std::vector<std::unique_ptr<PassType>> passes;	//実体
			std::vector<PassGroup> groups;                 // グループ

			std::vector<PassNode> executionOrder;

			std::vector<GroupState> groupStates;

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
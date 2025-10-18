/*****************************************************************//**
 * @file   RenderQueue.h
 * @brief レンダリングコマンドキューを定義するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include "../external/concurrentqueue/concurrentqueue.h"

#include <atomic>
#include <vector>
#include <thread>
#include <barrier>
#include <optional>
#include <array>

 //#define NO_USE_PMR_RENDER_QUEUE

#ifndef NO_USE_PMR_RENDER_QUEUE
#include <memory_resource>
#endif

#include "RenderTypes.h"

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief フレーム当たりの最大インスタンス数
		 */
		static inline constexpr uint32_t MAX_INSTANCES_PER_FRAME = 65536;
		/**
		 * @brief パス当たりの最大インスタンスインデックス数
		 */
		static inline constexpr uint32_t MAX_INSTANCE_INDICES_PER_PASS = 1024 * 1024;

		//========================================================================
		/**
		 * @brief 描画コマンドをキューから取り出す際のバッチサイズ
		 */
		static inline constexpr size_t DRAWCOMMAND_TMPBUF_SIZE = 4096 * 4;
		//========================================================================

		/**
		 * @brief 描画コマンドの発行。管理、ソート、バッチングを行うクラス
		 */
		class RenderQueue {
			/**
			 * @brief 描画コマンドのソートコンテキスト
			 */
			class SortContext {
#ifndef NO_USE_PMR_RENDER_QUEUE
				// PMR 有効時は DrawCommand コンテナ型を pmr::vector に切替
				using DrawCmdVec = std::pmr::vector<DrawCommand>;
				using IndexVec = std::pmr::vector<uint32_t>;
				using KeyVec = std::pmr::vector<uint64_t>;
				using ByteVec = std::pmr::vector<uint8_t>;
#else
				using DrawCmdVec = std::vector<DrawCommand>;
				using IndexVec = std::vector<uint32_t>;
				using KeyVec = std::vector<uint64_t>;
				using ByteVec = std::vector<uint8_t>;
#endif //NO_USE_PMR_RENDER_QUEUE

			public:
				explicit SortContext(
#ifndef NO_USE_PMR_RENDER_QUEUE
					std::pmr::memory_resource* mr = std::pmr::get_default_resource(),
#endif //NO_USE_PMR_RENDER_QUEUE
					int threadCount = std::thread::hardware_concurrency())
#ifndef NO_USE_PMR_RENDER_QUEUE
					: tempBuffer(mr)
					, indexBuf(mr)
					, keysBuf(mr)
					, visited_(mr)
					, histPool(mr)
					, offPool(mr)
					, threadCount(threadCount), mr_(mr)
#else
					: threadCount(threadCount)
#endif //NO_USE_PMR_RENDER_QUEUE
				{
				}

				// PMR/通常どちらのベクタでも動くようにテンプレート化
				template<class VecT>
				void Sort(VecT& cmds) {
					const size_t N = cmds.size();

					if (N < 500000)
					{
						// ---- 間接ソート：index だけを動かし、最後に一括適用 ----
						IndirectSortStd(cmds);
					}
					/*else if (N < 500000) {
						EnsureTempBuffer(N);
						RadixSortSingle(cmds, tempBuffer);
					}*/
					else {
						EnsureTempBuffer(N);
						SortContext::RadixSortMulti(cmds, (VecT&)tempBuffer, threadCount);
					}
				}

			private:
				DrawCmdVec tempBuffer;
				IndexVec indexBuf;    // 再利用
				KeyVec   keysBuf;     // 再利用（sortKey を抽出）
				IndexVec tmpIdxBuf;
				ByteVec  visited_;    // ApplyPermutation 用 “訪問フラグ” を再利用

				// Radix(MT) 用の大域ワーク（threadCount * BUCKETS）
				// ※スレッド生成は据え置き。まずは確保回数の削減を優先
				static constexpr int RADIX_BITS = 8;
				static constexpr int RADIX_BUCKETS = 1 << RADIX_BITS;
#ifndef NO_USE_PMR_RENDER_QUEUE
				std::pmr::vector<size_t> histPool;
				std::pmr::vector<size_t> offPool;
#else
				std::vector<size_t> histPool;
				std::vector<size_t> offPool;
#endif

				int threadCount;
#ifndef NO_USE_PMR_RENDER_QUEUE
				std::pmr::memory_resource* mr_ = nullptr;
#endif //NO_USE_PMR_RENDER_QUEUE
				void EnsureTempBuffer(size_t requiredSize) {
					if (tempBuffer.size() < requiredSize)
						tempBuffer.resize(requiredSize);
				}

				void EnsureScratch(size_t N) {
					if (indexBuf.capacity() < N) indexBuf.reserve(N);
					if (keysBuf.capacity() < N) keysBuf.reserve(N);
					if (tmpIdxBuf.capacity() < N) tmpIdxBuf.reserve(N);
					if (visited_.capacity() < N) visited_.reserve(N);
					const size_t need = static_cast<size_t>(threadCount) * RADIX_BUCKETS;
					if (histPool.size() < need) histPool.resize(need);
					if (offPool.size() < need) offPool.resize(need);
				}

				// ======（新規）比較ソート用：間接ソート + 一括適用 ======
				template<class VecT>
				void IndirectSortStd(VecT& cmds) {
					const size_t N = cmds.size();
					if (N <= 1) [[unlikely]] return;

					EnsureScratch(N);
					if (indexBuf.size() < N) indexBuf.resize(N);
					if (keysBuf.size() < N) keysBuf.resize(N);

					for (uint32_t i = 0; i < N; ++i) {
						indexBuf[i] = i;
						keysBuf[i] = cmds[i].sortKey;
					}

					if (N >= 32768) {
						constexpr int TOP_BITS = 12;
						constexpr uint32_t B = 1u << TOP_BITS; // 4096

						// ここは「固定サイズ」なので thread_local の配列で確保ゼロに
						thread_local std::array<uint32_t, B> count{};
						thread_local std::array<uint32_t, B> offset{};

						// クリア
						count.fill(0);
						offset.fill(0);

						auto bucketId = [](uint64_t k) noexcept {
							return uint32_t(k >> (64 - TOP_BITS));
							};

						for (size_t i = 0; i < N; ++i) ++count[bucketId(keysBuf[indexBuf[i]])];
						for (uint32_t b = 1; b < B; ++b) offset[b] = offset[b - 1] + count[b - 1];

						// ← ここで pmr と同型の一時バッファを使用
						auto& tmpIdx = tmpIdxBuf;
						tmpIdx.resize(N);

						for (size_t i = 0; i < N; ++i)
							tmpIdx[offset[bucketId(keysBuf[indexBuf[i]])]++] = indexBuf[i];

						indexBuf.swap(tmpIdx); // 同じ IndexVec 同士なので OK

						// バケット内を keys で比較ソート
						uint32_t start = 0;
						for (uint32_t b = 0; b < B; ++b) {
							uint32_t len = count[b];
							if (len > 1) {
								std::sort(indexBuf.begin() + start, indexBuf.begin() + start + len,
									[k = keysBuf.data()](uint32_t a, uint32_t b) noexcept { return k[a] < k[b]; });
							}
							start += len;
						}
					}
					else {
						std::sort(indexBuf.begin(), indexBuf.begin() + N,
							[k = keysBuf.data()](uint32_t a, uint32_t b) noexcept { return k[a] < k[b]; });
					}

					ApplyPermutationInPlace(cmds, indexBuf);
				}

				template<class VecTLocal, class IndexContainer>
				void ApplyPermutationInPlace(VecTLocal& cmds, IndexContainer& idx) {
					const size_t N = cmds.size();
					visited_.assign(N, 0);
					uint8_t* visited = visited_.data();

					for (size_t i = 0; i < N; ++i) {
						if (visited[i] || idx[i] == i) continue;
						size_t cur = i;
						auto tmp = std::move(cmds[cur]);
						while (!visited[cur]) {
							visited[cur] = 1;
							size_t nxt = idx[cur];
							if (nxt == i) { cmds[cur] = std::move(tmp); break; }
							cmds[cur] = std::move(cmds[nxt]);
							cur = nxt;
						}
					}
				}

				// Single-threaded Radix Sort
				template<class VecT>
				void RadixSortSingle(VecT& cmds, VecT& temp) {
					constexpr int BITS = 8;
					constexpr int BUCKETS = 1 << BITS;
					constexpr int PASSES = 64 / BITS;
					for (int pass = 0; pass < PASSES; ++pass) {
						int shift = pass * BITS;
						std::array<size_t, BUCKETS> count = {};
						for (auto& cmd : cmds)
							++count[(cmd.sortKey >> shift) & (BUCKETS - 1)];

						std::array<size_t, BUCKETS> offset = {};
						size_t sum = 0;
						for (int i = 0; i < BUCKETS; ++i) {
							offset[i] = sum;
							sum += count[i];
						}

						for (auto& cmd : cmds) {
							size_t idx = (cmd.sortKey >> shift) & (BUCKETS - 1);
							temp[offset[idx]++] = std::move(cmd);
						}

						cmds.swap(temp);
					}
				}

				// Multi-threaded Radix Sort
				template<class VecT>
				void RadixSortMulti(VecT& cmds, VecT& temp, int threadCount) {
					static constexpr int BITS = RADIX_BITS;
					static constexpr int BUCKETS = RADIX_BUCKETS;
					static constexpr int PASSES = 64 / BITS;

					const size_t N = cmds.size();
					VecT* in = &cmds;
					VecT* out = &temp;

					// --- 再確保ゼロのプール利用 ---
					// histPool/offPool は呼び出し側 EnsureScratch により容量確保済み想定
					auto& histPoolRef = const_cast<SortContext*>(this)->histPool;
					auto& offPoolRef = const_cast<SortContext*>(this)->offPool;

					for (int pass = 0; pass < PASSES; ++pass) {
						int shift = pass * BITS;
						size_t chunkSize = (N + threadCount - 1) / threadCount;

						// Histogram phase
						std::vector<std::thread> workers;
						for (int t = 0; t < threadCount; ++t) {
							size_t start = t * chunkSize;
							size_t end = (std::min)(start + chunkSize, N);
							workers.emplace_back([=, &in, &histPoolRef]() {
								size_t* hist = &histPoolRef[t * BUCKETS];
								std::fill_n(hist, BUCKETS, size_t(0));
								for (size_t i = start; i < end; ++i) {
									uint64_t key = ((*in)[i].sortKey >> shift) & (BUCKETS - 1);
									++hist[key];
								}
								});
						}
						for (auto& w : workers) w.join();
						workers.clear();

						// Global offset
						std::array<size_t, BUCKETS> globalOffset = {};
						size_t running = 0;
						for (int b = 0; b < BUCKETS; ++b) {
							globalOffset[b] = running;
							size_t sum_b = 0;
							for (int t = 0; t < threadCount; ++t)
								sum_b += histPoolRef[t * BUCKETS + b];
							running += sum_b;
						}

						for (int t = 0; t < threadCount; ++t) {
							size_t accum = 0;
							for (int b = 0; b < BUCKETS; ++b) {
								offPoolRef[t * BUCKETS + b] = globalOffset[b] + accum;
								accum += histPoolRef[t * BUCKETS + b];
							}
						}

						// Scatter phase
						for (int t = 0; t < threadCount; ++t) {
							size_t start = t * chunkSize;
							size_t end = (std::min)(start + chunkSize, N);
							workers.emplace_back([=, &in, &out, &offPoolRef]() mutable {
								size_t* localOff = &offPoolRef[t * BUCKETS];
								for (size_t i = start; i < end; ++i) {
									uint64_t key = ((*in)[i].sortKey >> shift) & (BUCKETS - 1);
									(*out)[localOff[key]++] = std::move((*in)[i]);
								}
								});
						}
						for (auto& w : workers) w.join();

						std::swap(in, out);
					}

					if (in != &cmds)
						cmds = std::move(*in);
				}
			};

		public:
			struct alignas(16) InstancePool
			{
				Math::Matrix<3,4,float> world;
				InstancePool& operator=(const InstanceData& data) noexcept {
					memcpy(&world, &data, sizeof(decltype(world)));
					return *this;
				}
				InstancePool& operator=(InstanceData&& data) noexcept {
					memcpy(&world, &data, sizeof(decltype(world)));
					return *this;
				}
			};

			/**
			 * @brief 生産者セッション-ユーザーに渡す用（thread_localを使わない）
			 */
			class ProducerSession {
			public:
				/**
				 * @brief コンストラクタ
				 * @param owner このセッションが属する RenderQueue への参照
				 */
				explicit ProducerSession(RenderQueue& owner) noexcept
					: rq(&owner) {
				}
				/**
				 * @brief コピー禁止
				 */
				ProducerSession(const ProducerSession&) = delete;
				ProducerSession& operator=(const ProducerSession&) = delete;
				/**
				 * @brief ムーブは可
				 */
				ProducerSession(ProducerSession&& other) noexcept
					: rq(other.rq), boundQueue(other.boundQueue), token(std::move(other.token)), buf(std::move(other.buf)) {
					other.rq = nullptr;
					other.boundQueue = nullptr;
					other.token.reset();
					other.buf.clear();
				}
				ProducerSession& operator=(ProducerSession&& other) noexcept {
					if (this != &other) {
						FlushAll();
						rq = other.rq;
						boundQueue = other.boundQueue;
						token = std::move(other.token);
						buf = std::move(other.buf);
						other.rq = nullptr;
						other.boundQueue = nullptr;
						other.token.reset();
						other.buf.clear();
					}
					return *this;
				}

				~ProducerSession() { FlushAll(); }

				// まとめ用固定長バッファ（ヒープなし）
				static constexpr size_t kChunk = 128;
				struct SmallBuf {
					DrawCommand data[kChunk];
					size_t size = 0;
					void push_back(const DrawCommand& c) noexcept { data[size++] = c; }
					void push_back(DrawCommand&& c) noexcept { data[size++] = std::move(c); }
					bool full() const noexcept { return size >= kChunk; }
					void clear() noexcept { size = 0; }
				};
				/**
				 * @brief DrawCommand を 1 件プールへ書き込み
				 * @param cmd 書き込む DrawCommand インスタンス
				 */
				void Push(const DrawCommand& cmd) {
					RebindIfNeeded();
					buf.push_back(cmd);
					if (buf.full()) flushChunk();
				}
				/**
				 * @brief DrawCommand を 1 件プールへ書き込み（ムーブ版）
				 * @param cmd 書き込む DrawCommand インスタンス
				 */
				void Push(DrawCommand&& cmd) {
					RebindIfNeeded();
					buf.push_back(std::move(cmd));
					if (buf.full()) flushChunk();
				}
				/**
				 * @brief インスタンスを 1 件プールへ書き込み、Index を返す
				 */
				[[nodiscard]] InstanceIndex AllocInstance(const InstanceData& inst);
				/**
				 * @brief インスタンスを 1 件プールへ書き込み、Index を返す（ムーブ版）
				 */
				[[nodiscard]] InstanceIndex AllocInstance(InstanceData&& inst);

				/**
				 * @brief インスタンスを 1 件プールへ書き込み、Index を返す
				 */
				[[nodiscard]] InstanceIndex AllocInstance(const InstancePool& inst);
				/**
				 * @brief インスタンスを 1 件プールへ書き込み、Index を返す（ムーブ版）
				 */
				[[nodiscard]] InstanceIndex AllocInstance(InstancePool&& inst);
				/**
				 * @brief 次に割り当てられるインスタンスインデックス
				 * @detail 実際にフレーム当たりのインスタンス数が増える
				 */
				[[nodiscard]] InstanceIndex NextInstanceIndex();
				/**
				 * @brief　インスタンスプールを 1 件分 memset で埋める
				 * @param index 対象のインスタンスインデックス
				 * @param inst 埋めるデータ
				 */
				void MemsetInstancePool(InstanceIndex index, const InstancePool& inst) noexcept;

				/**
				 * @brief SoA で DrawCommand を一括投入するための受け口
				 */
				struct DrawCommandSOA {
					const uint32_t* mesh = nullptr;
					const uint32_t* material = nullptr;
					const uint32_t* pso = nullptr;
					const uint32_t* instIx = nullptr;   // 省略時は baseInstance から連番でも可
					const uint64_t* sortKey = nullptr;   // null の場合は PushSOA 内で生成
					size_t count = 0;
				};

				/**
				 * @brief SoA から AoS に詰め替えてキューへ一括投入
				 */
				inline void PushSOA(const DrawCommandSOA& soa) {
					if (soa.count == 0) return;
					RebindIfNeeded();

					static_assert(kChunk >= 1, "kChunk must be positive");
					DrawCommand tmp[kChunk];
					size_t i = 0;
					while (i < soa.count) {
						const size_t n = (std::min)(kChunk, soa.count - i);
						for (size_t j = 0; j < n; ++j) {
							const size_t k = i + j;
							DrawCommand& c = tmp[j];
							c.mesh = soa.mesh ? soa.mesh[k] : 0u;
							c.material = soa.material ? soa.material[k] : 0u;
							c.pso = soa.pso ? soa.pso[k] : 0u;
							c.instanceIndex = soa.instIx ? soa.instIx[k] : 0u;
							if (soa.sortKey) c.sortKey = soa.sortKey[k];
							else             c.sortKey = Graphics::MakeSortKey(c.pso, c.material, c.mesh);
						}
						boundQueue->enqueue_bulk(*token, tmp, n);
						i += n;
					}
				}
				/**
				 * @brief 全バッファをキューへフラッシュ
				 */
				void FlushAll() {
					// “現在バインドされているキュー” に吐く（フレーム切替後でも取りこぼさない）
					if (boundQueue && token && buf.size) {
						boundQueue->enqueue_bulk(*token, buf.data, buf.size);
						buf.clear();
					}
				};
			private:
				RenderQueue* rq = nullptr;
				moodycamel::ConcurrentQueue<DrawCommand>* boundQueue = nullptr;
				std::optional<moodycamel::ProducerToken> token; // インライン保持（ヒープなし）
				SmallBuf buf;

				void flushChunk() {
					boundQueue->enqueue_bulk(*token, buf.data, buf.size);
					buf.clear();
				}

				void RebindIfNeeded() {
					auto* cur = &rq->CurQ(); // 現在の生産キュー
					if (boundQueue != cur) [[likely]] {
						// 旧キューへ残りを吐き出してからバインド切替（安全）
						if (boundQueue && token && buf.size) {
							boundQueue->enqueue_bulk(*token, buf.data, buf.size);
							buf.clear();
						}
						token.reset();
						token.emplace(*cur);
						boundQueue = cur;
					}
				}
			};

		public:
			/**
			 * @brief コンストラクタ
			 * @param maxInstancesPerFrame フレーム当たりの最大インスタンス数（1～MAX_INSTANCES_PER_FRAME）
			 */
			RenderQueue(uint32_t maxInstancesPerFrame = MAX_INSTANCES_PER_FRAME) :
				maxInstancesPerFrame(maxInstancesPerFrame) {
				assert(maxInstancesPerFrame > 0 && maxInstancesPerFrame <= MAX_INSTANCES_PER_FRAME);

				for (int i = 0; i < RENDER_BUFFER_COUNT; ++i) {
					queues[i] = std::make_unique<moodycamel::ConcurrentQueue<DrawCommand>>();
					instancePools[i] = std::unique_ptr<InstancePool[]>(new InstancePool[maxInstancesPerFrame]);
					instWritePos[i].store(0, std::memory_order_relaxed);
				}
			}

			/**
			 * @brief ムーブコンストラクタ
			 * @param other ムーブ元
			 */
			RenderQueue(RenderQueue&& other) noexcept
				: maxInstancesPerFrame(other.maxInstancesPerFrame),
				current(other.current.load()), sortContext(std::move(other.sortContext)) {
				assert(maxInstancesPerFrame > 0 && maxInstancesPerFrame <= MAX_INSTANCES_PER_FRAME);

				for (int i = 0; i < RENDER_BUFFER_COUNT; ++i) {
					queues[i] = std::move(other.queues[i]);
					instancePools[i] = std::move(other.instancePools[i]);
					instWritePos[i].store(other.instWritePos[i].load());
				}
			}

			~RenderQueue() {
				for (auto& t : ctoken) t.reset(); // ConsumerToken 明示破棄
			}

			/**
			 * @brief ムーブ代入演算子
			 */
			RenderQueue& operator=(RenderQueue&& other) noexcept {
				if (this != &other) {
					current.store(other.current.load());
					sortContext = std::move(other.sortContext);
					for (int i = 0; i < RENDER_BUFFER_COUNT; ++i)
						queues[i] = std::move(other.queues[i]);
				}
				return *this;
			}
			/**
			 * @brief 各ワーカーはフレーム / タスク開始時にこれで ProducerSession を取得
			 * @return ProducerSession 生産者セッション
			 */
			ProducerSession MakeProducer() { return ProducerSession{ *this }; }
			/**
			 * @brief Submit は「全ワーカーが FlushAll 済み」バリアの後に呼ぶ
			 * @param out DrawCommand コンテナ（呼び出し側で確保して渡す）
			 * @param outInstances インスタンスプールの生配列（次フレーム用に確保済み）
			 * @param outCount インスタンスプールの使用数
			 */
			void Submit(std::vector<DrawCommand>& out,
				const InstancePool*& outInstances, uint32_t& outCount) {
				// “現在の生産キュー” を次のフレームへ先に切り替える
				const int prev = current.exchange(
					(current.load(std::memory_order_relaxed) + 1) % RENDER_BUFFER_COUNT,
					std::memory_order_acq_rel);

				auto& q = *queues[prev];
				if (!ctoken[prev]) ctoken[prev].emplace(q); // 初回だけ生成して再利用

				//概算サイズで out を一発確保
				if (auto approx = q.size_approx(); approx > 0) {
					// 既存要素があればその分も見越しておく
					out.reserve(out.size() + approx);
				}

				auto pTmp = tmp.data();

				size_t n;
				while ((n = q.try_dequeue_bulk(*ctoken[prev], pTmp, DRAWCOMMAND_TMPBUF_SIZE)) != 0) {
					const auto old = out.size();
					out.resize(old + n);                 // 先にサイズだけ伸ばす（再確保は reserve 済みで起きない前提）
					if constexpr (std::is_trivially_copyable_v<DrawCommand>) {
						std::memcpy(out.data() + old, pTmp, n * sizeof(DrawCommand));
					}
					else {
						std::move(pTmp, pTmp + n, out.begin() + old);
					}
				}
				sortContext.Sort(out);

				// --- インスタンスプールの生配列と使用数を返す ---
				outInstances = instancePools[prev].get();
				outCount = instWritePos[prev].load(std::memory_order_acquire);

				// 次フレーム用に prev 側の write pos をリセット
				instWritePos[prev].store(0, std::memory_order_release);
			}
#ifndef NO_USE_PMR_RENDER_QUEUE
			/**
			 * @brief PMR 版 Submit（pmr::vector を直接受けられる）
			 * @param out DrawCommand コンテナ（呼び出し側で確保して渡す）
			 * @param outInstances インスタンスプールの生配列（次フレーム用に確保済み）
			 * @param outCount インスタンスプールの使用数
			 */
			void Submit(std::pmr::vector<DrawCommand>& out,
				const InstancePool*& outInstances, uint32_t& outCount) {
				const int prev = current.exchange(
					(current.load(std::memory_order_relaxed) + 1) % RENDER_BUFFER_COUNT,
					std::memory_order_acq_rel);

				auto& q = *queues[prev];
				if (!ctoken[prev]) ctoken[prev].emplace(q);

				// 概算で一撃 reserve（pmr アリーナから確保）
				if (auto approx = q.size_approx(); approx > 0) {
					out.reserve(out.size() + approx);
				}

				auto pTmp = tmp.data();
				size_t n;
				while ((n = q.try_dequeue_bulk(*ctoken[prev], pTmp, DRAWCOMMAND_TMPBUF_SIZE)) != 0) {
					const auto old = out.size();
					out.resize(old + n);
					if constexpr (std::is_trivially_copyable_v<DrawCommand>) {
						std::memcpy(out.data() + old, pTmp, n * sizeof(DrawCommand));
					}
					else {
						std::move(pTmp, pTmp + n, out.begin() + old);
					}
				}
				sortContext.Sort(out);

				outInstances = instancePools[prev].get();
				outCount = instWritePos[prev].load(std::memory_order_acquire);
				instWritePos[prev].store(0, std::memory_order_release);
			}
#endif

			/**
			 * @brief 現在フレーム側の Instance プールアクセス
			 * @return (InstanceData*, 書き込み位置へのポインタ)
			 */
			inline std::pair<InstancePool*, std::atomic<uint32_t>*>
				GetCurrentInstancePoolAccess() noexcept {
				const int cur = current.load(std::memory_order_acquire);
				return { instancePools[cur].get(), &instWritePos[cur] };
			}

			/**
			 * @brief 最大インスタンス数の公開 Getter
			 * @return uint32_t 最大インスタンス数
			 */
			inline uint32_t MaxInstancesPerFrame() const noexcept { return maxInstancesPerFrame; }

		private:
			// 現在の生産キュー
			moodycamel::ConcurrentQueue<DrawCommand>& CurQ() noexcept {
				return *queues[current.load(std::memory_order_acquire)];
			}
		private:
			const uint32_t maxInstancesPerFrame;

			std::unique_ptr<moodycamel::ConcurrentQueue<DrawCommand>> queues[RENDER_BUFFER_COUNT];
			std::optional<moodycamel::ConsumerToken> ctoken[RENDER_BUFFER_COUNT];
			std::atomic<int> current = 0;

			// フレーム別インスタンスプール（固定長配列）
			std::unique_ptr<InstancePool[]> instancePools[RENDER_BUFFER_COUNT];
			std::atomic<uint32_t> instWritePos[RENDER_BUFFER_COUNT];

			// 取り込み用一時バッファ：std::array でヒープ確保ゼロ化
			std::array<DrawCommand, DRAWCOMMAND_TMPBUF_SIZE> tmp{};

			// PMR時は tempBuffer も同資源で確保されるようにコンストラクト
#ifndef NO_USE_PMR_RENDER_QUEUE
			SortContext sortContext{ std::pmr::get_default_resource() };
#else
			SortContext sortContext;
#endif
		};
	}
}
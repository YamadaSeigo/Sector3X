#pragma once

#include "../external/concurrentqueue/concurrentqueue.h"

#include <atomic>
#include <vector>
#include <thread>
#include <barrier>

#include "RenderTypes.h"

namespace SectorFW
{
	namespace Graphics
	{
		//フレーム当たりの最大インスタンス数
		static inline constexpr uint32_t MAX_INSTANCES_PER_FRAME = 65536 * 2;
		static inline constexpr uint32_t MAX_INDICES_PER_PASS = 1024 * 1024;

		class RenderQueue {
			class SortContext {
			public:
				explicit SortContext(int threadCount = std::thread::hardware_concurrency())
					: threadCount(threadCount) {
				}

				void Sort(std::vector<DrawCommand>& cmds) {
					const size_t N = cmds.size();

					if (N < 500000)
					{
						std::sort(cmds.begin(), cmds.end(), [](const auto& a, const auto& b) {
							return a.sortKey < b.sortKey;
							});
					}
					/*else if (N < 500000) {
						EnsureTempBuffer(N);
						RadixSortSingle(cmds, tempBuffer);
					}*/
					else {
						EnsureTempBuffer(N);
						RadixSortMulti(cmds, tempBuffer, threadCount);
					}
				}

			private:
				std::vector<DrawCommand> tempBuffer;
				int threadCount;

				void EnsureTempBuffer(size_t requiredSize) {
					if (tempBuffer.size() < requiredSize)
						tempBuffer.resize(requiredSize);
				}

				// Single-threaded Radix Sort
				static void RadixSortSingle(std::vector<DrawCommand>& cmds, std::vector<DrawCommand>& temp) {
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
				static void RadixSortMulti(std::vector<DrawCommand>& cmds, std::vector<DrawCommand>& temp, int threadCount) {
					static constexpr int BITS = 8;
					static constexpr int BUCKETS = 1 << BITS;
					static constexpr int PASSES = 64 / BITS;

					const size_t N = cmds.size();
					std::vector<DrawCommand>* in = &cmds;
					std::vector<DrawCommand>* out = &temp;

					std::vector<std::vector<size_t>> localHistograms(threadCount, std::vector<size_t>(BUCKETS));
					std::vector<std::vector<size_t>> localOffsets(threadCount, std::vector<size_t>(BUCKETS));

					for (int pass = 0; pass < PASSES; ++pass) {
						int shift = pass * BITS;
						size_t chunkSize = (N + threadCount - 1) / threadCount;

						// Histogram phase
						std::vector<std::thread> workers;
						for (int t = 0; t < threadCount; ++t) {
							size_t start = t * chunkSize;
							size_t end = (std::min)(start + chunkSize, N);
							workers.emplace_back([=, &in, &localHistograms]() {
								auto& hist = localHistograms[t];
								std::fill(hist.begin(), hist.end(), 0);
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
						for (int b = 1; b < BUCKETS; ++b) {
							for (int t = 0; t < threadCount; ++t)
								globalOffset[b] += localHistograms[t][b - 1];
							globalOffset[b] += globalOffset[b - 1];
						}

						for (int b = 0; b < BUCKETS; ++b) {
							size_t offset = globalOffset[b];
							for (int t = 0; t < threadCount; ++t) {
								localOffsets[t][b] = offset;
								offset += localHistograms[t][b];
							}
						}

						// Scatter phase
						for (int t = 0; t < threadCount; ++t) {
							size_t start = t * chunkSize;
							size_t end = (std::min)(start + chunkSize, N);
							workers.emplace_back([=, &in, &out, &localOffsets]() mutable {
								auto localOff = localOffsets[t];
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
			// ---------- 生産者セッション（thread_localを使わない） ----------
			class ProducerSession {
			public:
				explicit ProducerSession(RenderQueue& owner) noexcept
					: rq(&owner) {
				}

				// コピー禁止
				ProducerSession(const ProducerSession&) = delete;

				ProducerSession& operator=(const ProducerSession&) = delete;
				// ムーブは可
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

				void Push(const DrawCommand& cmd) {
					RebindIfNeeded();
					buf.push_back(cmd);
					if (buf.full()) flushChunk();
				}
				void Push(DrawCommand&& cmd) {
					RebindIfNeeded();
					buf.push_back(std::move(cmd));
					if (buf.full()) flushChunk();
				}

				// インスタンスを 1 件プールへ書き込み、Index を返す
				[[nodiscard]] InstanceIndex AllocInstance(const InstanceData& inst) {
					RebindIfNeeded();
					// 現在のフレームスロット
					const int slot = rq->current.load(std::memory_order_acquire);
					auto& pos = rq->instWritePos[slot];
					uint32_t idx = pos.fetch_add(1, std::memory_order_acq_rel);
					// 簡易チェック（必要なら LOG + clamp / 失敗扱いにする）
					if (idx >= MAX_INSTANCES_PER_FRAME) {
						// 飽和させるか、エラー処理
						idx = MAX_INSTANCES_PER_FRAME - 1;
					}
					rq->instancePools[slot][idx] = inst;
					return InstanceIndex{ idx };
				}

				void FlushAll() {
					// “現在バインドされているキュー” に吐く（フレーム切替後でも取りこぼさない）
					if (boundQueue && token && buf.size) {
						boundQueue->enqueue_bulk(*token, buf.data, buf.size);
						buf.clear();
					}
				}

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
					if (boundQueue != cur) {
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
			RenderQueue() {
				for (int i = 0; i < RENDER_QUEUE_BUFFER_COUNT; ++i) {
					queues[i] = std::make_unique<moodycamel::ConcurrentQueue<DrawCommand>>();
					instancePools[i] = std::unique_ptr<InstanceData[]>(new InstanceData[MAX_INSTANCES_PER_FRAME]);
					instWritePos[i].store(0, std::memory_order_relaxed);
				}
			}

			// ムーブコンストラクタ
			RenderQueue(RenderQueue&& other) noexcept
				: current(other.current.load()), sortContext(std::move(other.sortContext)) {
				for (int i = 0; i < RENDER_QUEUE_BUFFER_COUNT; ++i) {
					queues[i] = std::move(other.queues[i]);
					instancePools[i] = std::move(other.instancePools[i]);
					instWritePos[i].store(other.instWritePos[i].load());
				}
			}

			~RenderQueue() {
				for (auto& t : ctoken) t.reset(); // ConsumerToken 明示破棄
			}

			// ムーブ代入演算子
			RenderQueue& operator=(RenderQueue&& other) noexcept {
				if (this != &other) {
					current.store(other.current.load());
					sortContext = std::move(other.sortContext);
					for (int i = 0; i < RENDER_QUEUE_BUFFER_COUNT; ++i)
						queues[i] = std::move(other.queues[i]);
				}
				return *this;
			}

			// 各ワーカーはフレーム/タスク開始時にこれで ProducerSession を取得
			ProducerSession MakeProducer() { return ProducerSession{ *this }; }

			// Submit は「全ワーカーが FlushAll 済み」バリアの後に呼ぶ
			void Submit(std::vector<DrawCommand>& out,
				const InstanceData*& outInstances, uint32_t& outCount) {
				// “現在の生産キュー” を次のフレームへ先に切り替える
				const int prev = current.exchange(
					(current.load(std::memory_order_relaxed) + 1) % RENDER_QUEUE_BUFFER_COUNT,
					std::memory_order_acq_rel);

				auto& q = *queues[prev];
				if (!ctoken[prev]) ctoken[prev].emplace(q); // 初回だけ生成して再利用

				DrawCommand tmp[1024];
				size_t n;
				while ((n = q.try_dequeue_bulk(*ctoken[prev], tmp, std::size(tmp))) != 0) {
					out.insert(out.end(), tmp, tmp + n);
				}
				sortContext.Sort(out);

				// --- インスタンスプールの生配列と使用数を返す ---
				outInstances = instancePools[prev].get();
				outCount = instWritePos[prev].load(std::memory_order_acquire);

				// 次フレーム用に prev 側の write pos をリセット
				instWritePos[prev].store(0, std::memory_order_release);
			}
		private:
			// 現在の生産キュー
			moodycamel::ConcurrentQueue<DrawCommand>& CurQ() noexcept {
				return *queues[current.load(std::memory_order_acquire)];
			}
		private:
			std::unique_ptr<moodycamel::ConcurrentQueue<DrawCommand>> queues[RENDER_QUEUE_BUFFER_COUNT];
			std::optional<moodycamel::ConsumerToken> ctoken[RENDER_QUEUE_BUFFER_COUNT];
			std::atomic<int> current = 0;

			// フレーム別インスタンスプール（固定長配列）
			std::unique_ptr<InstanceData[]> instancePools[RENDER_QUEUE_BUFFER_COUNT];
			std::atomic<uint32_t> instWritePos[RENDER_QUEUE_BUFFER_COUNT];

			SortContext sortContext;
		};
	}
}
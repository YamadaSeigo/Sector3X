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
		class RenderQueue {

            class SortContext {
            public:
                explicit SortContext(int threadCount = std::thread::hardware_concurrency())
                    : threadCount(threadCount) {
                }

                void Sort(std::vector<DrawCommand>& cmds) {
                    const size_t N = cmds.size();

                    if (N < 4096) {
                        std::sort(cmds.begin(), cmds.end(), [](const auto& a, const auto& b) {
                            return a.sortKey < b.sortKey;
                            });
                    }
                    else if (N < 20000) {
                        EnsureTempBuffer(N);
                        RadixSortSingle(cmds, tempBuffer);
                    }
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
                    constexpr int BITS = 8;
                    constexpr int BUCKETS = 1 << BITS;
                    constexpr int PASSES = 64 / BITS;

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
            RenderQueue() {
                for (int i = 0; i < RENDER_QUEUE_BUFFER_COUNT; ++i)
                    queues[i] = std::make_unique<moodycamel::ConcurrentQueue<DrawCommand>>();
            }

            // ムーブコンストラクタ
            RenderQueue(RenderQueue&& other) noexcept
                : current(other.current.load()), sortContext(std::move(other.sortContext)) {
                for (int i = 0; i < RENDER_QUEUE_BUFFER_COUNT; ++i)
                    queues[i] = std::move(other.queues[i]);
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

			void Push(const DrawCommand& cmd) { queues[current]->enqueue(cmd); }
			void Push(DrawCommand&& cmd) { queues[current]->enqueue(std::move(cmd)); }

			void Submit(std::vector<DrawCommand>& out) {
				int idx = current;
				current = (current + 1) % RENDER_QUEUE_BUFFER_COUNT;

				DrawCommand cmd;
				while (queues[idx]->try_dequeue(cmd)) out.push_back(cmd);

				sortContext.Sort(out);
			}
		private:
			std::unique_ptr<moodycamel::ConcurrentQueue<DrawCommand>> queues[RENDER_QUEUE_BUFFER_COUNT];
			std::atomic<int> current = 0;
			SortContext sortContext;
		};
	}
}
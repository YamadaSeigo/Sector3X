// SimpleThreadPool.h
#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace SFW
{
    struct IThreadExecutor {
        virtual ~IThreadExecutor() = default;
        virtual void Submit(std::function<void()> job) = 0;
        virtual size_t Concurrency() const = 0;
    };

    class SimpleThreadPool final : public IThreadExecutor {
    public:
        //RenderThraedのためがスレッドを1つ使っているので-1する
        explicit SimpleThreadPool(size_t n = std::thread::hardware_concurrency() - 1)
            : stop_(false)
            , busy_(0)
        {
            if (n == 0) n = 1;

            LOG_INFO("SimpleThreadPoolService: starting with {%d} threads", n);
            workers_.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                workers_.emplace_back([this] {
                    tls_inPool_ = true;
                    for (;;) {
                        std::function<void()> job;
                        {
                            std::unique_lock lk(m_);
                            cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
                            if (stop_ && q_.empty())
                                return;
                            job = std::move(q_.front());
                            q_.pop();
                        }

                        busy_.fetch_add(1, std::memory_order_relaxed);
                        ++tls_depth_;
                        job();
                        --tls_depth_;
                        busy_.fetch_sub(1, std::memory_order_relaxed);
                    }
                    });
            }
        }

        ~SimpleThreadPool() override {
            {
                std::lock_guard lk(m_);
                stop_ = true;
            }
            cv_.notify_all();
            for (auto& w : workers_)
                w.join();
        }

        void Submit(std::function<void()> job) override {
            // プール内からのネストした Submit で、
            // かつ全ワーカーがビジーなら、その場で実行
            if (tls_inPool_
                && tls_depth_ > 0
                && busy_.load(std::memory_order_relaxed) >= static_cast<int>(workers_.size())) {

                ++tls_depth_;
                job();
                --tls_depth_;
                return;
            }

            {
                std::lock_guard lk(m_);
                q_.push(std::move(job));
            }
            cv_.notify_one();
        }

        size_t Concurrency() const override { return workers_.size(); }

    private:
        std::mutex m_;
        std::condition_variable cv_;
        std::queue<std::function<void()>> q_;
        std::vector<std::thread> workers_;
        bool stop_;

        std::atomic<int> busy_;

        inline static thread_local int  tls_depth_ = 0;
        inline static thread_local bool tls_inPool_ = false;

    public:
        STATIC_SERVICE_TAG
    };

    class ThreadCountDownLatch {
    public:
        explicit ThreadCountDownLatch(int count) : count_(count) {}
        void CountDown() {
            std::lock_guard lk(m_);
            if (--count_ == 0) cv_.notify_all();
        }
        void Wait() {
            std::unique_lock lk(m_);
            cv_.wait(lk, [&] { return count_ == 0; });
        }
    private:
        std::mutex m_;
        std::condition_variable cv_;
        int count_;
    };

    class ThreadCountDownLatchExternalSync {
    public:
        explicit ThreadCountDownLatchExternalSync(std::mutex& mutex, std::condition_variable& cv, int count) 
            : m_(mutex), cv_(cv), count_(count) {}
        void CountDown() {
            std::lock_guard lk(m_);
            if (--count_ == 0) cv_.notify_all();
        }
        void Wait() {
            std::unique_lock lk(m_);
            cv_.wait(lk, [&] { return count_ == 0; });
        }
    private:
        std::mutex& m_;
        std::condition_variable& cv_;
        int count_;
    };
}

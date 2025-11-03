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
    struct IExecutor {
        virtual ~IExecutor() = default;
        virtual void Submit(std::function<void()> job) = 0;
        virtual size_t Concurrency() const = 0;
    };

    class SimpleThreadPoolService final : public IExecutor {
    public:
        explicit SimpleThreadPoolService(size_t n = std::thread::hardware_concurrency())
            : stop_(false)
        {
            if (n == 0) n = 1;
            workers_.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                workers_.emplace_back([this] {
                    for (;;) {
                        std::function<void()> job;
                        {
                            std::unique_lock lk(m_);
                            cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
                            if (stop_ && q_.empty()) return;
                            job = std::move(q_.front()); q_.pop();
                        }
                        job();
                    }
                    });
            }
        }
        ~SimpleThreadPoolService() override {
            {
                std::lock_guard lk(m_);
                stop_ = true;
            }
            cv_.notify_all();
            for (auto& w : workers_) w.join();
        }
        void Submit(std::function<void()> job) override {
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
    public:
        STATIC_SERVICE_TAG
    };

    class CountDownLatch {
    public:
        explicit CountDownLatch(int count) : count_(count) {}
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
}

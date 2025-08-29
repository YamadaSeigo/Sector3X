#pragma once
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <string>

namespace SectorFW
{
    namespace GUI
    {
        // --- 軽量 Latest 値（trivially copyable 用）
        template <class T>
        struct Latest {
            std::atomic<T> v{};
            void publish(T x) noexcept { v.store(x, std::memory_order_release); }
            T    consume() const noexcept { return v.load(std::memory_order_acquire); }
        };
        // --- 文字列など非trivialはロックで
        template <>
        struct Latest<std::string> {
            void publish(std::string s) { std::lock_guard<std::mutex> lk(m); v = std::move(s); }
            std::string consume() const { std::lock_guard<std::mutex> lk(m); return v; }
        private:
            mutable std::mutex m;
            std::string v;
        };

        // --- 小さな MPMC 風キュー（UI側で毎フレ drain するだけなのでロックでOK）
        template <class T>
        class UiQueue {
        public:
            void push(T x) { std::lock_guard<std::mutex> lk(m_); q_.push_back(std::move(x)); }
            std::vector<T> drain() {
                std::lock_guard<std::mutex> lk(m_);
                std::vector<T> out; out.reserve(q_.size());
                while (!q_.empty()) { out.push_back(std::move(q_.front())); q_.pop_front(); }
                return out;
            }
        private:
            std::mutex m_;
            std::deque<T> q_;
        };

        // --- 必要ならスナップショット（二重バッファ）
        struct Telemetry {
            float cpu = 0.f, gpu = 0.f;
            std::vector<float> frameTimes;
        };
        // UiSnapshot: RAII 版
        class UiSnapshot {
        public:
            class WriteGuard {
            public:
                explicit WriteGuard(UiSnapshot& s)
                    : snap_(s), lock_(s.m_) {
                }          // ここで lock
                Telemetry& data() noexcept { return snap_.back_; }
                // コピー禁止・ムーブOK
                WriteGuard(const WriteGuard&) = delete;
                WriteGuard& operator=(const WriteGuard&) = delete;
                WriteGuard(WriteGuard&&) = default;
                WriteGuard& operator=(WriteGuard&&) = default;
            private:
                UiSnapshot& snap_;
                std::unique_lock<std::mutex> lock_;     // ここがスコープで自動 unlock
            };

            WriteGuard beginWrite() { return WriteGuard(*this); }
            void swap() {                                  // UIスレッド側
                std::lock_guard<std::mutex> lk(m_);
                using std::swap;
                swap(front_, back_);
            }
            const Telemetry& read() const { return front_; }

        private:
            mutable std::mutex m_;
            Telemetry front_{}, back_{};
        };

        // --- バス本体（ここだけをグローバルにする）
        struct UIBus {
            std::atomic<bool> alive{ false }; // 生存フラグ
            Latest<float>      cpuLoad, gpuLoad;
            Latest<std::string> status;
            UiQueue<std::string> logQ;
            UiSnapshot          snap;
        };

        // --- ライフサイクル（起動時/終了時に呼ぶ）
        inline void StartUIBus();
        inline void StopUIBus();

        // --- 公開API（どこからでも呼べる“関数”だけを露出）
        inline void PublishCpu(float v);
        inline void PublishGpu(float v);
        inline void PublishStatus(std::string s);
        inline void PushLog(std::string s);
        inline UiSnapshot::WriteGuard BeginTelemetryWrite();
    }
}

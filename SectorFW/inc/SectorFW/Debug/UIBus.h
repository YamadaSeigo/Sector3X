/*****************************************************************//**
 * @file   UIBus.h
 * @brief UIバスの定義
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <string>

namespace SectorFW
{
	namespace Debug
	{
		/**
		 * @brief 軽量 Latest 値（trivially copyable 用）
		 */
		template <class T>
		struct Latest {
			std::atomic<T> v{};
			void publish(T x) noexcept { v.store(x, std::memory_order_release); }
			T    consume() const noexcept { return v.load(std::memory_order_acquire); }
		};
		/**
		 * @brief 文字列など非trivialはロックで
		 */
		template <>
		struct Latest<std::string> {
			void publish(std::string s) { std::lock_guard<std::mutex> lk(m); v = std::move(s); }
			std::string consume() const { std::lock_guard<std::mutex> lk(m); return v; }
		private:
			mutable std::mutex m;
			std::string v;
		};
		/**
		 * @brief 小さな MPMC 風キュー（UI側で毎フレ drain するだけなのでロックでOK）
		 */
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

		/**
		 * @brief 必要ならスナップショット（二重バッファ）
		 */
		struct Telemetry {
			float cpu = 0.f, gpu = 0.f;
			std::vector<float> frameTimes;
		};
		/**
		 * @brief UiSnapshot: RAII 版
		 */
		class UiSnapshot {
		public:
			/**
			 * @brief 書き込みガード（RAII でロック/アンロック）
			 */
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
			/**
			 * @brief 書き込み開始
			 * @return WriteGuard 書き込みガード
			 */
			WriteGuard beginWrite() { return WriteGuard(*this); }
			/**
			 * @brief フロント/バックを入れ替え
			 */
			void swap() {                                  // UIスレッド側
				std::lock_guard<std::mutex> lk(m_);
				using std::swap;
				swap(front_, back_);
			}
			/**
			 * @brief フロントを読む
			 * @return const Telemetry& フロントの参照
			 */
			const Telemetry& read() const { return front_; }
		private:
			mutable std::mutex m_;
			Telemetry front_{}, back_{};
		};

		/**
		 * @brief Tree snapshot types(UIBus.h)
		 */
		struct TreeItem {
			uint64_t id;       // 安定したID（ImGuiの開閉状態が保持される）
			uint32_t depth;    // 0 = root, 1 = child, ...
			bool     leaf;     // 子なしなら true
			std::string label; // 表示テキスト
		};
		/**
		 * @brief ツリーフレーム（前順＋depth 付きアイテム群）
		 */
		struct TreeFrame {
			std::vector<TreeItem> items; // 前順（プリオーダ）で depth 付き
		};
		/**
		 * @brief 既存 UiSnapshot をそのまま流用して TreeFrame 用を追加
		 */
		class UiTreeSnapshot {
		public:
			/**
			 * @brief 書き込みガード（RAII でロック/アンロック）
			 */
			class WriteGuard {
			public:
				explicit WriteGuard(UiTreeSnapshot& s)
					: snap_(s), lock_(s.m_) {
				}
				TreeFrame& data() noexcept { return snap_.back_; }
				WriteGuard(const WriteGuard&) = delete;
				WriteGuard& operator=(const WriteGuard&) = delete;
				WriteGuard(WriteGuard&&) = default;
				WriteGuard& operator=(WriteGuard&&) = default;
			private:
				UiTreeSnapshot& snap_;
				std::unique_lock<std::mutex> lock_;
			};
			/**
			 * @brief 書き込み開始
			 * @return WriteGuard 書き込みガード
			 */
			WriteGuard beginWrite() { return WriteGuard(*this); }
			/**
			 * @brief フロント/バックを入れ替え
			 */
			void swap() {
				std::lock_guard<std::mutex> lk(m_);
				using std::swap; swap(front_, back_);
			}
			/**
			 * @brief フロントを読む
			 * @return const TreeFrame& フロントの参照
			 */
			const TreeFrame& read() const { return front_; }
		private:
			mutable std::mutex m_;
			TreeFrame front_{}, back_{};
		};

		/**
		 * @brief バス本体（ここだけをグローバルにする）
		 */
		struct UIBus {
			std::atomic<bool> alive{ false }; // 生存フラグ
			Latest<float>      cpuLoad, gpuLoad;
			Latest<std::string> status;
			UiQueue<std::string> logQ;
			UiSnapshot          snap;
			UiTreeSnapshot       tree;
		};
		/**
		 * @brief グローバルUIバスの取得
		 */
		UIBus& GetUIBus();

		/**
		 * @brief ライフサイクル（起動時 / 終了時に呼ぶ）
		 */
		void StartUIBus();
		/**
		 * @brief ライフサイクル（起動時 / 終了時に呼ぶ）
		 */
		void StopUIBus();
		/**
		 * @brief CPUの計測情報を発行。公開API（どこからでも呼べる“関数”だけを露出）
		 */
		void PublishCpu(float v);
		/**
		 * @brief GPUの計測情報を発行。公開API（どこからでも呼べる“関数”だけを露出）
		 */
		void PublishGpu(float v);
		/**
		 * @brief ステータス文字列を発行。公開API（どこからでも呼べる“関数”だけを露出）
		 */
		void PublishStatus(std::string s);
		/**
		 * @brief ログ文字列を発行。公開API（どこからでも呼べる“関数”だけを露出）
		 */
		void PushLog(std::string s);
		/**
		 * @brief テレメトリ書き込み開始（RAIIでロック/アンロック）
		 */
		UiSnapshot::WriteGuard BeginTelemetryWrite();
		/**
		 * @brief ツリー書き込み開始（RAIIでロック/アンロック）
		 */
		UiTreeSnapshot::WriteGuard BeginTreeWrite();
		/**
		 * @brief ワールドのデバッグツリーの深さ
		 */
		enum WorldTreeDepth : uint32_t {
			World = 0,
			Level = 1,
			LevelNode = 2,
			System = 3,
			RenderGraph = 0,
			Pass = 1,
			DrawCommand = 2,
			DepthMax
		};
	}
}

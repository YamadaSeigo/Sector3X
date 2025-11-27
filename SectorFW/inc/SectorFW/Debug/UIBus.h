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
#include <functional>

namespace SFW
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

		// ================================
		// デバッグコントロール用の定義
		// ================================
		enum class DebugControlKind {
			DC_SLIDERFLOAT,
			DC_SLIDERINT,
			DC_CHECKBOX,
			DC_MAX
		};

		struct DebugControl {
			DebugControlKind kind{};
			std::string category = "Default";
			std::string      label;

			// 現在値（UI側で保持）
			float f_value = 0.f, f_min = 0.f, f_max = 1.f, f_speed = 0.1f;
			int   i_value = 0, i_min = 0, i_max = 100;
			bool  b_value = false;

			// 値変更時に呼ばれるコールバック
			std::function<void(float)> onChangeF;
			std::function<void(int)>   onChangeI;
			std::function<void(bool)>  onChangeB;
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

			UiQueue<DebugControl> debugControlRegisterQ; // 登録キュー
			std::vector<DebugControl> debugControls;     // UIスレッド側で保持
		};
		/**
		 * @brief グローバルUIバスの取得
		 */
		UIBus& GetUIBus();

		/**
		 * @brief ImGui の SliderFloat + コールバック.
		 * @param label ラベル名
		 * @param initialValue 初期値
		 * @param minValue　上限
		 * @param maxValue　下限
		 * @param speed　スライダーの速度
		 */
		void RegisterDebugSliderFloat(
			const std::string& category,
			const std::string& label,
			float initialValue,
			float minValue,
			float maxValue,
			float speed,
			std::function<void(float)> onChange);

		/**
		 * @brief 変数ポインタを直接バインドする簡易版.
		 * @param label ラベル名
		 * @param target 設定する変数のポインタ
		 * @param minValue　上限
		 * @param maxValue　下限
		 * @param speed　スライダーの速度
		 */
		inline void BindDebugSliderFloat(
			const std::string& category,
			const std::string& label,
			float* target,
			float minValue,
			float maxValue,
			float speed = 0.1f)
		{
			if (!target) return;
			RegisterDebugSliderFloat(
				category,
				label,
				*target,
				minValue,
				maxValue,
				speed,
				[target](float v) { *target = v; } // ここで代入
			);
		}

#ifdef _ENABLE_IMGUI

#define REGISTER_DEBUG_SLIDER_FLOAT(category, label, initialValue, minValue, maxValue, speed, onChange) \
	SFW::Debug::RegisterDebugSliderFloat(category, label, initialValue, minValue, maxValue, speed, onChange)

#define BIND_DEBUG_SLIDER_FLOAT(category, label, target, minValue, maxValue, speed) \
	SFW::Debug::BindDebugSliderFloat(category, label, target, minValue, maxValue, speed)

#else //! _ENABLE_IMGUI

#define REGISTER_DEBUG_SLIDER_FLOAT(category, label, initialValue, minValue, maxValue, speed, onChange) 
#define BIND_DEBUG_SLIDER_FLOAT(category, label, target, minValue, maxValue, speed)

#endif // _ENABLE_IMGUI

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
			TREEDEPTH_WORLD = 0,
			TREEDEPTH_LEVEL = 1,
			TREEDEPTH_LEVELNODE = 2,
			TREEDEPTH_SYSTEM = 3,
			TREEDEPTH_RENDERGRAPH = 0,
			TREEDEPTH_GROUP = 1,
			TREEDEPTH_DRAWCOMMAND = 2,
			TREEDEPTH_MAX
		};
	}
}

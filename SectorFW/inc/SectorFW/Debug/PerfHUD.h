/*****************************************************************//**
 * @file   PerfHUD.h
 * @brief パフォーマンスHUDを表示するクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include "../external/imgui/imgui.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace SFW
{
	namespace Debug
	{
		/**
		 * @brief 固定サイズのローリングバッファ
		 */
		struct RollingBuffer {
			// バッファ本体
			std::vector<float> data;
			// 現在の書き込み位置
			size_t head = 0;
			// 最後に計算した最小値と最大値
			float  lastMin = 0.f, lastMax = 1.f;
			/**
			 * @brief バッファを初期化する
			 * @param cap バッファの容量
			 */
			void init(size_t cap) { data.assign(cap, 0.f); head = 0; }
			/**
			 * @brief バッファのサイズを取得する
			 * @return size_t バッファのサイズ
			 */
			size_t size()  const { return data.size(); }
			/**
			 * @brief バッファが空かどうかを確認する
			 * @return bool 空の場合はtrue、そうでない場合はfalse
			 */
			bool   empty() const { return data.empty(); }
			/**
			 * @brief バッファに値を追加する
			 * @param v 追加する値
			 */
			void push(float v) {
				if (data.empty()) return;
				data[head] = v;
				head = (head + 1) % data.size();
			}
			/**
			 * @brief バッファの内容を線形配列に変換する
			 * @param out 出力先の配列
			 */
			void toLinear(std::vector<float>& out) const {
				out.resize(data.size());
				const size_t N = data.size();
				const size_t tail = head;
				const size_t first = N - tail;
				std::copy(data.begin() + tail, data.end(), out.begin());
				std::copy(data.begin(), data.begin() + tail, out.begin() + first);
			}
			/**
			 * @brief バッファの内容を自動的にスケーリングする
			 */
			void autoscale() {
				if (data.empty()) return;
				float mn = FLT_MAX, mx = -FLT_MAX;
				for (float v : data) { mn = std::min(mn, v); mx = std::max(mx, v); }
				if (!(mn < mx)) { mn = 0.f; mx = 1.f; }
				lastMin = mn; lastMax = mx;
			}
			/**
			 * @brief バッファの平均値を計算する
			 * @return float 平均値
			 */
			float average() const {
				if (data.empty()) return 0.f;
				float sum = 0.f;
				for (float v : data) sum += v;
				return sum / data.size();
			}
		};
		/**
		 * @brief パフォーマンスHUDを表示するクラス
		 */
		struct PerfHUD {
			/**
			 * @brief HUDを初期化する
			 * @param historySamples 履歴サンプル数
			 */
			void Init(size_t historySamples = 600);
			/**
			 * @brief ImGui::NewFrame() 後に毎フレ呼ぶ
			 * @param frameBudgetSec フレーム予算(秒)
			 * @param overlayTopRight 右上にオーバーレイ表示する場合true
			 */
			void TickAndDraw(float frameBudgetSec = 1.0f / 60.0f, bool overlayTopRight = true);
		private:
			RollingBuffer cpuBuf_, gpuBuf_, frameMsBuf_;
			float cpuEMA_ = 0.f, gpuEMA_ = 0.f;
			bool  inited_ = false;

			void DrawDonutGauge(const char* label, float value01, float sizePx,
				ImU32 colBg, ImU32 colFill, ImU32 colText);
			void DrawSparkline(const char* label, RollingBuffer& buf, const ImVec2& size,
				float scaleMin = 0.f, float scaleMax = 1.f, const char* fmt = "%.0f%%",
				float scaleMul = 100.f);
		};
	}
}

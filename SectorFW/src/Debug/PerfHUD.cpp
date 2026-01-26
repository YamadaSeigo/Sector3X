#include "Debug/PerfHUD.h"
#include "Debug/UIBus.h"
#include "../external/imgui/imgui_internal.h"

namespace SFW
{
	namespace Debug
	{
		static float EMA(float prev, float x, float alpha) {
			return prev + alpha * (x - prev);
		}

		void PerfHUD::Init(size_t historySamples) {
			cpuBuf_.init(historySamples);
			gpuBuf_.init(historySamples);
			frameMsBuf_.init(historySamples);

			logicMsBuf_.init(historySamples);
			renderMsBuf_.init(historySamples);
			gpuMsBuf_.init(historySamples);
			criticalMsBuf_.init(historySamples);

			cpuEMA_ = 0.f; gpuEMA_ = 0.f;
			inited_ = true;
		}

		// PathArcToReverse を使わず、ストロークでドーナツを描く
		void PerfHUD::DrawDonutGauge(const char* label, float value01, float sizePx,
			ImU32 colBg, ImU32 colFill, ImU32 colText) {
			value01 = std::clamp(value01, 0.f, 1.f);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImVec2 p = ImGui::GetCursorScreenPos();
			ImVec2 center = ImVec2(p.x + sizePx * 0.5f, p.y + sizePx * 0.5f);

			const float thickness = sizePx * 0.16f;
			const float radius = sizePx * 0.5f;

			// 背景リング（全周）
			dl->AddCircle(center, radius, colBg, 0, thickness);

			// 値アーク（開始角 -135° から 360° 掃引の一部）
			const float start = -IM_PI * 0.75f;   // -135°
			const float span = IM_PI * 2.0f;    // 360°
			const float a0 = start;
			const float a1 = start + span * value01;

			dl->PathClear();
			dl->PathArcTo(center, radius, a0, a1, 0); // 逆方向にしたい場合は (a1, a0)
			dl->PathStroke(colFill, 0, thickness);

			// 中央テキスト（%）
			char buf[64];
			ImFormatString(buf, IM_ARRAYSIZE(buf), "%.0f%%", value01 * 100.f);
			ImVec2 ts = ImGui::CalcTextSize(buf);
			dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), colText, buf);

			// ラベル
			ImVec2 ls = ImGui::CalcTextSize(label);
			dl->AddText(ImVec2(center.x - ls.x * 0.5f, p.y + sizePx + 4.f), colText, label);

			ImGui::Dummy(ImVec2(sizePx, sizePx + ls.y + 6.f));
		}

		void PerfHUD::DrawSparkline(const char* label, RollingBuffer& buf, const ImVec2& size,
			float scaleMin, float scaleMax, const char* fmt, float scaleMul) {
			std::vector<float> tmp;
			buf.toLinear(tmp);
			ImGui::TextUnformatted(label);
			ImGui::PushItemWidth(size.x);
			ImGui::PlotLines("##spark", tmp.data(), (int)tmp.size(), 0, nullptr,
				scaleMin, scaleMax, size);
			ImGui::PopItemWidth();
			float latest = tmp.empty() ? 0.f : tmp.back();
			ImGui::SameLine();
			ImGui::Text(fmt, latest * scaleMul);
		}

		void PerfHUD::DrawStackedFrameBar(float logicMs, float renderMs, float gpuMs, float budgetMs, float barWidth, float barHeight)
		{
			// 目盛りは budget を基準（超えたら右に溢れる描き方も可能だが、ここはclamp）
			auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
			float l01 = clamp01(logicMs / budgetMs);
			float r01 = clamp01(renderMs / budgetMs);
			float g01 = clamp01(gpuMs / budgetMs);

			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImVec2 p0 = ImGui::GetCursorScreenPos();
			ImVec2 p1 = ImVec2(p0.x + barWidth, p0.y + barHeight);

			// 背景
			dl->AddRectFilled(p0, p1, IM_COL32(50, 50, 60, 180), 4.0f);

			// 横方向に「積み上げ」に見せる（並べる）方式
			// ※本当のクリティカルパスは max(logic, render, gpu) なので、下の表示で強調する
			float x = p0.x;

			auto seg = [&](float w01, ImU32 col) {
				float w = barWidth * w01;
				if (w <= 0.0f) return;
				dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x + w, p1.y), col, 4.0f);
				x += w;
				};

			seg(l01, IM_COL32(90, 180, 255, 220));  // Logic
			seg(r01, IM_COL32(120, 255, 140, 220)); // Render
			seg(g01, IM_COL32(255, 140, 90, 220));  // GPU

			// 枠
			dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 60), 4.0f);

			// ツールチップ
			ImGui::InvisibleButton("##stackbar", ImVec2(barWidth, barHeight));
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::Text("Logic : %.2f ms", logicMs);
				ImGui::Text("Render: %.2f ms", renderMs);
				ImGui::Text("GPU   : %.2f ms", gpuMs);
				ImGui::Text("Budget: %.2f ms", budgetMs);
				ImGui::EndTooltip();
			}
		}

		void PerfHUD::TickAndDraw(float frameBudgetSec, bool overlayTopRight) {
			if (!inited_) Init();

			// 0..1 値取得
			float cpu = GetUIBus().cpuLoad.consume();
			float gpu = GetUIBus().gpuLoad.consume();

			// EMA で見やすく
			cpuEMA_ = EMA(cpuEMA_, cpu, 0.25f);
			gpuEMA_ = EMA(gpuEMA_, gpu, 0.25f);

			// フレーム時間（ms）
			float dt = ImGui::GetIO().DeltaTime;
			float ms = dt * 1000.f;

			cpuBuf_.push(cpuEMA_);
			gpuBuf_.push(gpuEMA_);
			frameMsBuf_.push(ms);


			float logicMs = GetUIBus().logicMs.consume();
			float renderMs = GetUIBus().renderMs.consume();
			float gpuMs = GetUIBus().gpuFrameMs.consume();

			logicMsBuf_.push(logicMs);
			renderMsBuf_.push(renderMs);
			gpuMsBuf_.push(gpuMs);

			// 「このフレームのボトルネック（クリティカル）」は max を採用
			float criticalMs = std::max(logicMs, std::max(renderMs, gpuMs));
			criticalMsBuf_.push(criticalMs);

			// 配置（右上オーバーレイ）
			ImGuiViewport* vp = ImGui::GetMainViewport();
			if (overlayTopRight) {
				ImVec2 pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x - 8.f, vp->WorkPos.y + 8.f);
				ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.f, 0.f));
				ImGui::SetNextWindowBgAlpha(0.35f);
			}
			ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoNav;

			if (ImGui::Begin("Perf", nullptr, wf)) {
				// ドーナツゲージ（CPU/GPU）
				ImGui::BeginGroup();

				float budgetMs = frameBudgetSec * 1000.0f;

				// 見た目の要：積み上げバー（1本で直感的に）
				ImGui::TextUnformatted("Frame Breakdown (this frame)");

				// ボトルネック判定（max の要素名）
				const char* bottleneck = "Logic";
				if (renderMs >= logicMs && renderMs >= gpuMs) bottleneck = "Render";
				else if (gpuMs >= logicMs && gpuMs >= renderMs) bottleneck = "GPU";

				if (ImGui::BeginTable("crit", 2, ImGuiTableFlags_SizingFixedFit))
				{
					ImGui::TableSetupColumn("Left");                          // 自動
					ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthFixed, 220.0f); // 固定

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Critical: %6.2f ms", criticalMs); // 数字も桁固定

					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Bottleneck: %-6s", bottleneck);   // 文字も固定（例: "Render"まで想定）

					ImGui::EndTable();
				}

				DrawStackedFrameBar(logicMs, renderMs, gpuMs, budgetMs, 260.0f, 16.0f);

				ImVec2 spSize = ImVec2(220.f, 48.f);

				// フレーム時間（ms）
				frameMsBuf_.autoscale();
				float minMs = std::min(0.f, frameMsBuf_.lastMin);
				float maxMs = std::max(33.3f, frameMsBuf_.lastMax);
				std::vector<float> tmp;
				frameMsBuf_.toLinear(tmp);
				ImGui::Text("Frame (10s)");
				ImGui::PlotLines("##framems", tmp.data(), (int)tmp.size(), 0, nullptr,
					minMs, maxMs, ImVec2(spSize.x, spSize.y));
				ImGui::SameLine();
				ImGui::Text("%.1f ms \n(budget %.1f)",
					tmp.empty() ? 0.f : tmp.back(), frameBudgetSec * 1000.f);

				// 履歴も見たい場合（10秒スパーク）
				ImVec2 sp2 = ImVec2(220.f, 40.f);
				ImGui::PushID("logicMs");
				DrawSparkline("Logic ms (10s)", logicMsBuf_, sp2, 0.f, budgetMs, "%.2f ms", 1.f);
				ImGui::PopID();
				ImGui::PushID("renderMs");
				DrawSparkline("Render ms (10s)", renderMsBuf_, sp2, 0.f, budgetMs, "%.2f ms", 1.f);
				ImGui::PopID();
				ImGui::PushID("gpuMs");
				DrawSparkline("GPU ms (10s)", gpuMsBuf_, sp2, 0.f, budgetMs, "%.2f ms", 1.f);
				ImGui::PopID();

				// スパークライン（直近10秒）
				ImGui::Separator();

				ImGui::Text("Usage Rates");

				float S = 96.f * ImGui::GetIO().FontGlobalScale;
				auto colText = ImGui::GetColorU32(ImGuiCol_Text);
				DrawDonutGauge("CPU", cpuEMA_,
					S,
					IM_COL32(60, 60, 70, 180),
					IM_COL32(80, 180, 255, 230),
					colText);
				ImGui::SameLine();
				DrawDonutGauge("GPU3D", gpuEMA_,
					S,
					IM_COL32(60, 60, 70, 180),
					IM_COL32(255, 120, 80, 230),
					colText);
				ImGui::EndGroup();

				// スパークライン（直近10秒）
				ImGui::Separator();

				ImGui::PushID("sparksCPU");
				DrawSparkline("CPU (10s)", cpuBuf_, spSize, 0.f, 1.f, "%.0f%%", 100.f);
				ImGui::PopID();
				ImGui::PushID("sparksGPU");
				DrawSparkline("GPU (10s)", gpuBuf_, spSize, 0.f, 1.f, "%.0f%%", 100.f);
				ImGui::PopID();
			}
			ImGui::End();
		}
	}
}
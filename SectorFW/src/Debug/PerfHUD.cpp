#include "Debug/PerfHUD.h"
#include "Debug/UIBus.h"
#include "../external/imgui/imgui_internal.h"

namespace SectorFW
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

			// 値アーク（開始角 -135° から 270° 掃引の一部）
			const float start = -IM_PI * 0.75f;   // -135°
			const float span = IM_PI * 1.5f;    // 270°
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
				float S = 96.f * ImGui::GetIO().FontGlobalScale;
				auto colText = ImGui::GetColorU32(ImGuiCol_Text);
				DrawDonutGauge("CPU", cpuEMA_,
					S,
					IM_COL32(60, 60, 70, 180),
					IM_COL32(80, 180, 255, 230),
					colText);
				ImGui::SameLine();
				DrawDonutGauge("GPU", gpuEMA_,
					S,
					IM_COL32(60, 60, 70, 180),
					IM_COL32(255, 120, 80, 230),
					colText);
				ImGui::EndGroup();

				// スパークライン（直近10秒）
				ImGui::Separator();
				ImVec2 spSize = ImVec2(220.f, 48.f);

				ImGui::PushID("sparksCPU");
				DrawSparkline("CPU (10s)", cpuBuf_, spSize, 0.f, 1.f, "%.0f%%", 100.f);
				ImGui::PopID();
				ImGui::PushID("sparksGPU");
				DrawSparkline("GPU (10s)", gpuBuf_, spSize, 0.f, 1.f, "%.0f%%", 100.f);
				ImGui::PopID();

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
			}
			ImGui::End();
		}
	}
}
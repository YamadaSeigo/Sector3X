/*****************************************************************//**
 * @file   GPUTimerD3D11.h
 * @brief  Direct3D11用のGPUタイマーを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm> // std::max

namespace SFW
{
	namespace Debug
	{
		/**
		 * @brief Direct3D11用のGPUタイマー
		 *
		 * - タイムスタンプクエリをリングバッファで管理
		 * - 未解決（GPU がまだ終わっていない）クエリは再利用しない
		 *   → QUERY_BEGIN_ABANDONING_PREVIOUS_RESULTS / QUERY_END_ABANDONING_PREVIOUS_RESULTS を回避
		 * - 空きスロットがない場合、そのフレームの計測はスキップする
		 */
		class GpuTimerD3D11 {
		public:
			/**
			 * @brief 初期化
			 * @param dev      デバイス
			 * @param history  履歴数（同時 in-flight させる最大フレーム数目安）
			 */
			void init(ID3D11Device* dev, int history = 3)
			{
				history_ = (std::max)(1, history);
				rings_.clear();
				rings_.resize(history_);

				for (auto& r : rings_) {
					D3D11_QUERY_DESC q{};
					// 周波数＆Disjoint 情報
					q.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
					q.MiscFlags = 0;
					dev->CreateQuery(&q, r.disjoint.ReleaseAndGetAddressOf());

					// 開始タイムスタンプ
					q.Query = D3D11_QUERY_TIMESTAMP;
					dev->CreateQuery(&q, r.begin.ReleaseAndGetAddressOf());

					// 終了タイムスタンプ
					dev->CreateQuery(&q, r.end.ReleaseAndGetAddressOf());

					r.inFlight = false;
					r.hasResult = false;
					r.lastSec = -1.0;
				}

				writeIndex_ = 0;
				readIndex_ = 0;
				activeIndex_ = -1;
			}

			/**
			 * @brief フレーム先頭（描画直前）
			 * @param ctx デバイスコンテキスト
			 *
			 * 空いているリングスロット（inFlight == false）のみを使用する。
			 * 全スロットが inFlight の場合は計測をスキップする。
			 */
			void begin(ID3D11DeviceContext* ctx)
			{
				if (rings_.empty()) {
					activeIndex_ = -1;
					return;
				}

				// 現在の writeIndex_ から空きスロットを探す
				int start = writeIndex_;
				int found = -1;
				for (int i = 0; i < history_; ++i) {
					int idx = (start + i) % history_;
					if (!rings_[idx].inFlight) {
						found = idx;
						break;
					}
				}

				// 空きがない → このフレームは計測しない
				if (found < 0) {
					activeIndex_ = -1;
					return;
				}

				activeIndex_ = found;
				auto& r = rings_[activeIndex_];

				// 新しい計測開始
				r.inFlight = true;
				r.hasResult = false;
				r.lastSec = -1.0;

				ctx->Begin(r.disjoint.Get());
				ctx->End(r.begin.Get());
			}

			/**
			 * @brief フレーム最後（Present前）
			 * @param ctx デバイスコンテキスト
			 *
			 * begin() に成功したときのみ End を発行する。
			 */
			void end(ID3D11DeviceContext* ctx)
			{
				if (activeIndex_ < 0 || rings_.empty())
					return;

				auto& r = rings_[activeIndex_];

				ctx->End(r.end.Get());
				ctx->End(r.disjoint.Get());

				// 次回探索のベース位置を進めておく
				writeIndex_ = (activeIndex_ + 1) % history_;

				// このフレームの計測は完了（結果は GPU 側で後から取れる）
				activeIndex_ = -1;
			}

			/**
			 * @brief 1フレ遅れで取得（ノンブロッキング）
			 * @param ctx デバイスコンテキスト
			 * @return 秒（新しい結果が取得できなかった場合は < 0）
			 *
			 * - 一番古い未解決のリング（readIndex_ 起点）から順に解決を試みる
			 * - 解決できたものは必ず GetData(S_OK) 済みにして inFlight = false にする
			 * - これにより「未取得結果を残したまま Query を再利用する」ことを避ける
			 */
			double tryResolve(ID3D11DeviceContext* ctx)
			{
				if (rings_.empty())
					return -1.0;

				// 一度に1つだけ解決して返す（古いものから）
				for (int i = 0; i < history_; ++i) {
					int idx = (readIndex_ + i) % history_;
					auto& r = rings_[idx];

					if (!r.inFlight)
						continue; // このスロットは既に解決済み or 未使用

					D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
					HRESULT hr = ctx->GetData(r.disjoint.Get(), &dj, sizeof(dj), 0);

					if (hr == S_FALSE) {
						// まだ GPU が終わっていない → 次のフレームで再チャレンジ
						continue;
					}
					if (FAILED(hr)) {
						// 何らかのエラー → このスロットは捨てる
						r.inFlight = false;
						r.hasResult = false;
						r.lastSec = -1.0;
						readIndex_ = (idx + 1) % history_;
						continue;
					}

					// 周波数情報取得成功。Disjoint = true の場合は計測無効
					if (dj.Disjoint) {
						r.inFlight = false;
						r.hasResult = false;
						r.lastSec = -1.0;
						readIndex_ = (idx + 1) % history_;
						continue;
					}

					// タイムスタンプ2つを取得
					UINT64 t0 = 0, t1 = 0;
					hr = ctx->GetData(r.begin.Get(), &t0, sizeof(t0), 0);
					if (hr != S_OK) {
						// begin/end どちらかがまだなら、もう少し待つ
						if (hr == S_FALSE) continue;

						// それ以外のエラー → このスロットは捨てる
						r.inFlight = false;
						r.hasResult = false;
						r.lastSec = -1.0;
						readIndex_ = (idx + 1) % history_;
						continue;
					}

					hr = ctx->GetData(r.end.Get(), &t1, sizeof(t1), 0);
					if (hr != S_OK) {
						if (hr == S_FALSE) continue;

						r.inFlight = false;
						r.hasResult = false;
						r.lastSec = -1.0;
						readIndex_ = (idx + 1) % history_;
						continue;
					}

					// 正常に両方のタイムスタンプが取れた → 計測完了
					const double sec = double(t1 - t0) / double(dj.Frequency);

					r.inFlight = false;
					r.hasResult = true;
					r.lastSec = sec;

					// 次の解決対象位置を進める
					readIndex_ = (idx + 1) % history_;

					return sec;
				}

				// 今回は新しい結果を得られなかった
				return -1.0;
			}

		private:
			struct Ring {
				Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
				Microsoft::WRL::ComPtr<ID3D11Query> begin;
				Microsoft::WRL::ComPtr<ID3D11Query> end;

				bool   inFlight = false; ///< GPU に投げて未解決なら true
				bool   hasResult = false;
				double lastSec = -1.0;   ///< 直近の結果（今は外には返していないが保険で保持）
			};

			std::vector<Ring> rings_;
			int history_ = 0;

			int writeIndex_ = 0;  ///< 次に begin で探し始める位置
			int readIndex_ = 0;  ///< 次に tryResolve する起点
			int activeIndex_ = -1; ///< 直近の begin/end 対象（そのフレーム）
		};
	}
}

/*****************************************************************//**
 * @file   GPUTimerD3D11.h
 * @brief Direct3D11用のGPUタイマーを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

namespace SFW
{
	namespace Debug
	{
		/**
		 * @brief Direct3D11用のGPUタイマー
		 */
		class GpuTimerD3D11 {
		public:
			/**
			 * @brief 初期化
			 * @param dev デバイス
			 * @param history 履歴数（デフォルト3）
			 */
			void init(ID3D11Device* dev, int history = 3) {
				history_ = history;
				rings_.resize(history_);
				for (auto& r : rings_) {
					D3D11_QUERY_DESC q{};
					q.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
					dev->CreateQuery(&q, r.disjoint.ReleaseAndGetAddressOf());
					q.Query = D3D11_QUERY_TIMESTAMP;
					dev->CreateQuery(&q, r.begin.ReleaseAndGetAddressOf());
					dev->CreateQuery(&q, r.end.ReleaseAndGetAddressOf());
				}
			}
			/**
			 * @brief フレーム先頭（描画直前）
			 * @param ctx デバイスコンテキスト
			 */
			void begin(ID3D11DeviceContext* ctx) {
				auto& r = rings_[cur_];
				ctx->Begin(r.disjoint.Get());
				ctx->End(r.begin.Get());
			}
			/**
			 * @brief フレーム最後（Present前）
			 * @param ctx デバイスコンテキスト
			 */
			void end(ID3D11DeviceContext* ctx) {
				auto& r = rings_[cur_];
				ctx->End(r.end.Get());
				ctx->End(r.disjoint.Get());
				cur_ = (cur_ + 1) % history_;
			}
			/**
			 * @brief 1フレ遅れで取得（ノンブロッキング）
			 * @param ctx デバイスコンテキスト
			 * @return 秒（取得不可は <0）
			 */
			double tryResolve(ID3D11DeviceContext* ctx) {
				auto& r = rings_[read_];
				if (!r.disjoint) return -1.0;

				D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
				if (ctx->GetData(r.disjoint.Get(), &dj, sizeof(dj), 0) != S_OK) return -1.0;
				if (dj.Disjoint) { read_ = (read_ + 1) % history_; return -1.0; }

				UINT64 t0 = 0, t1 = 0;
				if (ctx->GetData(r.begin.Get(), &t0, sizeof(t0), 0) != S_OK) return -1.0;
				if (ctx->GetData(r.end.Get(), &t1, sizeof(t1), 0) != S_OK) return -1.0;

				read_ = (read_ + 1) % history_;
				return double(t1 - t0) / double(dj.Frequency); // 秒
			}
		private:
			struct Ring {
				Microsoft::WRL::ComPtr<ID3D11Query> disjoint, begin, end;
			};
			std::vector<Ring> rings_;
			int cur_ = 0, read_ = 0, history_ = 3;
		};
	}
}

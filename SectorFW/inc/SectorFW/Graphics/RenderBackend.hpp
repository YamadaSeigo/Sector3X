/*****************************************************************//**
 * @file   RenderBackend.hpp
 * @brief レンダリングバックエンドの基底クラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <vector>
#include "RenderTypes.h"

#include "RenderGraph.hpp"

namespace SFW
{
	namespace Graphics
	{
		/**
		 * @brief レンダリングバックエンドの基底クラス。CRTPを使用して派生クラスで実装を強制する。
		 */
		template<typename Derived, PointerType RTV, PointerType DSV, PointerType SRV, PointerType Buffer, template<typename> class ViewHandle>
		class RenderBackendBase {
		public:
			/**
			 * @brief RenderGraphにリソースマネージャーを追加する関数
			 * @param graph レンダーグラフの参照
			 */
			void AddResourceManagerToRenderService(RenderGraph<Derived, RTV, DSV, SRV, Buffer, ViewHandle>& graph) {
				static_cast<Derived*>(this)->AddResourceManagerToRenderServiceImpl(graph);
			}
			/**
			 * @brief プリミティブのトポロジーを設定する関数
			 * @param topology　プリミティブのトポロジー
			 */
			void SetPrimitiveTopology(PrimitiveTopology topology) const {
				static_cast<const Derived*>(this)->SetPrimitiveTopologyImpl(topology);
			}
			/**
			 * @brief ラスタライザーステートを設定する関数
			 * @param state ラスタライザーステートID
			 */
			void SetRasterizerState(RasterizerStateID state) const {
				static_cast<const Derived*>(this)->SetRasterizerStateImpl(state);
			}
			/**
			 * @brief ブレンドステートを設定する関数
			 * @param state ブレンドステートID
			 */
			void SetBlendState(BlendStateID state) const {
				static_cast<const Derived*>(this)->SetBlendStateImpl(state);
			}
			/**
			 * @brief デプスステンシルステートを設定する関数
			 * @param state デプスステンシルステートID
			 */
			void SetDepthStencilState(DepthStencilStateID state, uint32_t stencilRef = 0) const {
				static_cast<const Derived*>(this)->SetDepthStencilStateImpl(state, stencilRef);
			}
			/**
			 * @brief レンダーターゲットとデプスステンシルビューを設定する関数
			 * @param rtvs レンダーターゲットビューの配列
			 * @param dsv デプスステンシルビュー
			 */
			void SetRenderTargets(const std::vector<RTV>& rtvs, void* dsv) const {
				static_cast<const Derived*>(this)->SetRenderTargetsImpl(rtvs, dsv);
			}
			/**
			 * @brief シェーダーリソースビューをバインドする関数
			 * @param srvs シェーダーリソースビューの配列
			 * @param startSlot バインド開始スロット
			 */
			void BindPSSRVs(const std::vector<SRV>& srvs, uint32_t startSlot = 0) const {
				static_cast<const Derived*>(this)->BindPSSRVsImpl(srvs, startSlot);
			}
			/**
			 * @brief 定数バッファビューをバインドする関数
			 * @param cbvs 定数バッファビューの配列
			 * @param startSlot バインド開始スロット
			 */
			void BindVSCBVs(const std::vector<Buffer>& cbvs, uint32_t startSlot = 0) const {
				static_cast<const Derived*>(this)->BindVSCBVsImpl(cbvs, startSlot);
			}
			/**
			 * @brief ピクセルシェーダー用定数バッファビューをバインドする関数
			 * @param cbvs 定数バッファビューの配列
			 * @param startSlot バインド開始スロット
			 */
			void BindPSCBVs(const std::vector<Buffer>& cbvs, uint32_t startSlot = 0) const {
				static_cast<const Derived*>(this)->BindPSCBVsImpl(cbvs, startSlot);
			}
			/**
			 * @brief グローバル定数バッファビューをバインドする関数
			 * @param cbvs 定数バッファビューの配列
			 */
			void BindGlobalVSCBVs(const std::vector<BindSlotBuffer>& cbvs) const {
				static_cast<const Derived*>(this)->BindGlobalVSCBVsImpl(cbvs);
			}
			/**
			 * @brief ビューポートを設定する関数
			 * @param vp ビューポート情報
			 */
			void SetViewport(const Viewport& vp) const {
				static_cast<const Derived*>(this)->SetViewportImpl(vp);
			}
			/**
			 * @brief フレームごとのインスタンスデータをアップロードする関数
			 * @param framePool インスタンスデータの配列
			 * @param instCount インスタンスデータの数
			 */
			void BeginFrameUpload(const SharedInstanceArena::InstancePool* framePool, uint32_t instCount) {
				static_cast<Derived*>(this)->BeginFrameUploadImpl(framePool, instCount);
			}
			/**
			 * @brief インデックス付きインスタンスドローを実行する関数
			 * @param cmds インデックス付きインスタンスドローコマンドの配列
			 * @param usePSORastarizer ラスタライザーステートをPSOから設定するかどうか
			 */
			template<typename VecT>
			void ExecuteDrawIndexedInstanced(const VecT& cmds, std::optional<PSOHandle> psoOverride = std::nullopt, bool usePSORastarizer = true) {
				static_cast<Derived*>(this)->ExecuteDrawIndexedInstancedImpl(cmds, psoOverride, usePSORastarizer);
			}

			template<typename VecT>
			void ExecuteDrawIndexedInstanced(const VecT& cmds, std::span<const uint32_t> indices, std::optional<PSOHandle> psoOverride = std::nullopt, bool usePSORastarizer = true) {
				static_cast<Derived*>(this)->ExecuteDrawIndexedInstancedImpl(cmds, indices, psoOverride, usePSORastarizer);
			}
			/**
			 * @brief フレーム終了時に遅延削除を処理する関数
			 * @param currentFrame 現在のフレーム番号
			 */
			void ProcessDeferredDeletes(uint64_t currentFrame) {
				static_cast<Derived*>(this)->ProcessDeferredDeletesImpl(currentFrame);
			}
			/**
			 * @brief バッファデータを更新する関数
			 * @param buffer 更新するバッファ
			 * @param data 更新データへのポインタ
			 * @param size 更新データのサイズ（バイト単位）
			 */
			void UpdateBufferData(Buffer buffer, const void* data, size_t size) {
				static_cast<Derived*>(this)->UpdateBufferDataImpl(buffer, data, size);
			}
		};
	}
}
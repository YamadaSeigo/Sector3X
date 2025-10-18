/*****************************************************************//**
 * @file   DX11RenderBackend.h
 * @brief DirectX 11レンダリングバックエンドの定義
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once

#include <wrl/client.h>

#include "../RenderBackend.hpp"

#include "DX11MeshManager.h"
#include "DX11MaterialManager.h"
#include "DX11ShaderManager.h"
#include "DX11PSOManager.h"
#include "DX11TextureManager.h"
#include "DX11BufferManager.h"
#include "DX11SamplerManager.h"
#include "DX11ModelAssetManager.h"

#include <cassert>

namespace SectorFW
{
	namespace Graphics
	{
		/**
		 * @brief DirectX 11のプリミティブトポロジー変換用ルックアップテーブル
		 * @param t プリミティブトポロジー
		 * @return D3D_PRIMITIVE_TOPOLOGY DirectX 11のプリミティブトポロジー
		 */
		inline D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology t) {
			return D3DTopologyLUT[static_cast<size_t>(t)];
		}
		/**
		 * @brief DirectX 11レンダリングバックエンドクラス.
		 */
		class DX11Backend : public RenderBackendBase<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*, ID3D11Buffer*> {
		public:
			/**
			 * @brief ひと描画命令当たりのインスタンスデータの最大数
			 */
			static inline constexpr uint32_t MAX_DRAW_CALL_INSTANCES_NUM = 1024 * 4;
			/**
			 * @brief コンストラクタ
			 * @param device ID3D11Deviceのポインタ
			 * @param context ID3D11DeviceContextのポインタ
			 * @param meshMgr メッシュマネージャー
			 * @param matMgr マテリアルマネージャー
			 * @param shaderMgr シェーダーマネージャー
			 * @param psoMgr PSOマネージャー
			 * @param textureMgr テクスチャマネージャー
			 * @param cbMgr コンスタントバッファマネージャー
			 * @param samplerMgr サンプラーマネージャー
			 * @param modelAssetMgr モデルアセットマネージャー
			 */
			explicit DX11Backend(ID3D11Device* device, ID3D11DeviceContext* context,
				DX11MeshManager* meshMgr, DX11MaterialManager* matMgr,
				DX11ShaderManager* shaderMgr, DX11PSOManager* psoMgr,
				DX11TextureManager* textureMgr, DX11BufferManager* cbMgr,
				DX11SamplerManager* samplerMgr, DX11ModelAssetManager* modelAssetMgr);
			/**
			 * @brief RendderGraphにリソースマネージャーを追加する関数
			 * @param graph レンダーグラフ
			 */
			void AddResourceManagerToRenderServiceImpl(
				RenderGraph<DX11Backend, ID3D11RenderTargetView*, ID3D11ShaderResourceView*, ID3D11Buffer*>& graph);
			/**
			 * @brief プリミティブトポロジーを設定する関数
			 * @param topology プリミティブトポロジー
			 */
			void SetPrimitiveTopologyImpl(PrimitiveTopology topology) {
				context->IASetPrimitiveTopology(ToD3DTopology(topology));
			}
			/**
			 * @brief ブレンドステートを設定する関数
			 * @param state ブレンドステートID
			 */
			void SetBlendStateImpl(BlendStateID state);
			/**
			 * @brief ラスタライザーステートを設定する関数
			 * @param state ラスタライザーステートID
			 */
			void SetRasterizerStateImpl(RasterizerStateID state);
			/**
			 * @brief デプスステンシルステートを設定する関数
			 * @param state デプスステンシルステートID
			 */
			void SetDepthStencilStateImpl(DepthStencilStateID state) {
				context->OMSetDepthStencilState(depthStencilStates[(size_t)state].Get(), 0);
			}
			/**
			 * @brief レンダーターゲットとデプスステンシルビューを設定する関数
			 * @param rtvs レンダーターゲットビューの配列
			 * @param dsv デプスステンシルビュー
			 */
			void SetRenderTargetsImpl(const std::vector<ID3D11RenderTargetView*>& rtvs, void* dsv) {
				context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), (ID3D11DepthStencilView*)dsv);
			}
			/**
			 * @brief シェーダリソースビューをバインドする関数
			 * @param srvs シェーダリソースビューの配列
			 * @param startSlot 開始スロット
			 */
			void BindSRVsImpl(const std::vector<ID3D11ShaderResourceView*>& srvs, uint32_t startSlot = 0) {
				context->PSSetShaderResources(startSlot, (UINT)srvs.size(), srvs.data());
			}
			/**
			 * @brief コンスタントバッファをバインドする関数
			 * @param cbvs コンスタントバッファの配列
			 * @param startSlot 開始スロット
			 */
			void BindCBVsImpl(const std::vector<ID3D11Buffer*>& cbvs, uint32_t startSlot = 0) {
				context->VSSetConstantBuffers(startSlot, (UINT)cbvs.size(), cbvs.data());
			}
			/**
			 * @brief 共通のコンスタントバッファをバインドする関数
			 * @param cbvs コンスタントバッファの配列
			 */
			void BindGlobalCBVsImpl(const std::vector<BufferHandle>& cbvs) {
				std::vector<ID3D11Buffer*> buffers;
				buffers.reserve(cbvs.size());
				for (const auto& cb : cbvs) {
					auto d = cbManager->Get(cb);
					buffers.push_back(d.ref().buffer.Get());
				}
				context->VSSetConstantBuffers(0, (UINT)buffers.size(), buffers.data());
			}
			/**
			 * @brief インスタンスデータをフレーム前にアップロードする関数
			 * @param framePool インスタンスデータの配列
			 * @param instCount インスタンスデータの数
			 */
			void BeginFrameUploadImpl(const RenderQueue::InstancePool* framePool, uint32_t instCount)
			{
				D3D11_MAPPED_SUBRESOURCE m{};
				context->Map(m_instanceSB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
				memcpy(m.pData, framePool, instCount * sizeof(decltype(*framePool)));
				context->Unmap(m_instanceSB.Get(), 0);

				context->VSSetShaderResources(0, 1, m_instanceSRV.GetAddressOf()); // t0 バインド
			}
			/**
			 * @brief インスタンスドローを実行する関数の実装
			 * @param cmds インスタンスドローコマンドの配列
			 * @param usePSORasterizer PSOのラスタライザーステートを使用するかどうか
			 */
			template<typename VecT>
			void ExecuteDrawIndexedInstancedImpl(const VecT& cmds, bool usePSORasterizer)
			{
				struct DrawBatch {
					uint32_t mesh;
					uint32_t material;
					uint32_t pso;
					uint32_t base;
					uint32_t instanceCount; // instances[idx]
				};

				BeginIndexStream();

				size_t i = 0;
				size_t cmdCount = cmds.size();
				std::vector<DrawBatch> batches;
				while (i < cmdCount) {
					auto currentPSO = cmds[i].pso;
					auto currentMat = cmds[i].material;

					uint32_t currentMesh = cmds[i].mesh;
					uint32_t instanceCount = 0;

					const uint32_t base = m_idxHead;        // このドローの index 先頭

					// 1) 同PSO/Mat/Mesh を束ねつつ、index を SRV に“直接”書く
					auto* dst = reinterpret_cast<uint32_t*>(m_idxMapped) + m_idxHead;

					// 同じPSO + Material + Meshをまとめる
					while (i < cmdCount &&
						cmds[i].pso == currentPSO &&
						cmds[i].material == currentMat &&
						cmds[i].mesh == currentMesh &&
						instanceCount < MAX_DRAW_CALL_INSTANCES_NUM) {
						dst[instanceCount++] = cmds[i].instanceIndex.index; // ← 直接書く = instances[idx];
						++i;
					}
					m_idxHead += instanceCount;

					batches.emplace_back(currentMesh, currentMat, currentPSO, base, instanceCount);
				}

				EndIndexStream();

				context->VSSetConstantBuffers(1, 1, m_perDrawCB.GetAddressOf()); // b1
				for (const auto& b : batches) {
					// PerDraw CB に base と count を設定
					struct { uint32_t base, count, pad0, pad1; }
					perDraw{ b.base, b.instanceCount, 0, 0 };
					D3D11_MAPPED_SUBRESOURCE m{};
					context->Map(m_perDrawCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
					memcpy(m.pData, &perDraw, sizeof(perDraw));
					context->Unmap(m_perDrawCB.Get(), 0);

					DrawInstanced(b.mesh, b.material, b.pso, b.instanceCount, usePSORasterizer);
				}
			}

			/**
			 * @brief 保留中の削除を処理する関数の実装
			 * @param currentFrame 現在のフレーム番号
			 */
			void ProcessDeferredDeletesImpl(uint64_t currentFrame);

		private:
			void DrawInstanced(uint32_t meshIdx, uint32_t matIdx, uint32_t psoIdx, uint32_t count, bool usePSORasterizer);
			void BindMeshVertexStreamsForPSO(uint32_t meshIdx, uint32_t psoIdx);
			void BindMeshVertexStreamsFromOverrides(uint32_t meshIdx, uint32_t psoIdx);

			HRESULT CreateInstanceBuffer();
			HRESULT CreateRasterizerStates();
			HRESULT CreateBlendStates();
			HRESULT CreateDepthStencilStates();

			//void UpdateInstanceBuffer(const void* pInstancesData, size_t dataSize);

			void BeginIndexStream()
			{
				D3D11_MAPPED_SUBRESOURCE m{};
				context->Map(m_instIndexSB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
				m_idxMapped = static_cast<std::byte*>(m.pData);
				m_idxHead = 0;
			}

			void EndIndexStream()
			{
				context->Unmap(m_instIndexSB.Get(), 0);
				m_idxMapped = nullptr;

				context->VSSetShaderResources(1, 1, m_instIndexSRV.GetAddressOf()); // t1 を VS にセット
			}

		private:
			ID3D11Device* device;
			ID3D11DeviceContext* context;
			DX11MeshManager* meshManager;
			DX11MaterialManager* materialManager;
			DX11ShaderManager* shaderManager;
			DX11PSOManager* psoManager;
			DX11TextureManager* textureManager;
			DX11BufferManager* cbManager;
			DX11SamplerManager* samplerManager;
			DX11ModelAssetManager* modelAssetManager;

			// フレーム行列用 StructuredBuffer (SRV)
			ComPtr<ID3D11Buffer>            m_instanceSB;
			ComPtr<ID3D11ShaderResourceView> m_instanceSRV;

			// インデックス列用 StructuredBuffer (SRV)  ※uint（4B/要素）
			ComPtr<ID3D11Buffer>            m_instIndexSB;
			ComPtr<ID3D11ShaderResourceView> m_instIndexSRV;

			// PerDraw 定数バッファ（gIndexBase, gIndexCount）
			ComPtr<ID3D11Buffer>            m_perDrawCB;

			// CPU書き込み用ポインタ（Mapした間だけ有効）
			std::byte* m_idxMapped = nullptr;
			uint32_t    m_idxHead = 0;  // 書き込みカーソル

			ComPtr<ID3D11RasterizerState> rasterizerStates[(size_t)RasterizerStateID::MAX_COUNT];
			ComPtr<ID3D11BlendState> blendStates[(size_t)BlendStateID::MAX_COUNT];
			ComPtr<ID3D11DepthStencilState> depthStencilStates[(size_t)DepthStencilStateID::MAX_COUNT];
		};
	}
}

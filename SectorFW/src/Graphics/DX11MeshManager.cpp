#include "Graphics/DX11/DX11MeshManager.h"

#include "Debug/logger.h"

#include <numbers>
#include <dxgiformat.h>

// half への変換（R16G16_FLOAT 用）
#include <DirectXPackedVector.h>

#ifdef USE_MESHOPTIMIZER
#ifdef _DEBUG
#include <meshoptimizer/MDdx64/include/meshoptimizer.h>
#else
#include <meshoptimizer/MDx64/include/meshoptimizer.h>
#endif//_DEBUG
#endif//USE_MESHOPTIMIZER

namespace SFW
{
	namespace Graphics::DX11
	{
		MeshData MeshManager::CreateResource(const MeshCreateDesc& desc, MeshHandle h)
		{
			MeshData mesh{};
			// Vertex Buffer
			// 互換API：単一VBパス（既存の呼び出し維持）
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.ByteWidth = (UINT)desc.vSize;
			vbDesc.Usage = desc.vUsage;
			if (desc.vUsage == D3D11_USAGE_DYNAMIC) {
				vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			}
			else if (desc.vUsage == D3D11_USAGE_STAGING) {
				vbDesc.CPUAccessFlags = desc.cpuAccessFlags; // CPU アクセスフラグを指定
			}
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			HRESULT hr;

			if (desc.vertices != nullptr) {
				D3D11_SUBRESOURCE_DATA vbData = {};
				vbData.pSysMem = desc.vertices;
				hr = device->CreateBuffer(&vbDesc, &vbData, &mesh.vbs[0]);
				mesh.usedSlots.set(0);
			}
			else if (vbDesc.Usage == D3D11_USAGE_IMMUTABLE) {
				LOG_ERROR("Immutable vertex buffer must have initial data.");
				assert(false && "Immutable vertex buffer must have initial data.");
				return {};
			}
			else {
				hr = device->CreateBuffer(&vbDesc, nullptr, &mesh.vbs[0]);
			}
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create vertex buffer: {}", hr);
				assert(false && "Failed to create vertex buffer");
				return {};
			}

			// Index Buffer
			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.ByteWidth = (UINT)desc.iSize;
			ibDesc.Usage = desc.iUsage;
			if (desc.iUsage == D3D11_USAGE_DYNAMIC) {
				ibDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			}
			else if (desc.iUsage == D3D11_USAGE_STAGING) {
				ibDesc.CPUAccessFlags = desc.cpuAccessFlags; // CPU アクセスフラグを指定
			}
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			if (desc.indices != nullptr) {
				D3D11_SUBRESOURCE_DATA ibData = {};
				ibData.pSysMem = desc.indices;
				hr = device->CreateBuffer(&ibDesc, &ibData, &mesh.ib);
			}
			else if (ibDesc.Usage == D3D11_USAGE_IMMUTABLE) {
				LOG_ERROR("Immutable index buffer must have initial data.");
				assert(false && "Immutable index buffer must have initial data.");
				return {};
			}
			else {
				hr = device->CreateBuffer(&ibDesc, nullptr, &mesh.ib);
			}

			if (FAILED(hr)) {
				LOG_ERROR("Failed to create index buffer: {}", hr);
				assert(false && "Failed to create index buffer");
				return {};
			}

			mesh.indexCount = (uint32_t)(desc.iSize / sizeof(uint32_t));
			mesh.stride = (uint32_t)desc.stride;
			mesh.path = desc.sourcePath;

			return mesh;
		}

		// ========= 変換ユーティリティ（ローカル関数） =========
		static inline uint8_t ToSnorm8(float v) {
			// [-1,1] → [-128,127] を two's complement で格納（DXGI SNORM は -128→-1.0 と解釈）
			float clamped = (std::max)(-1.0f, (std::min)(1.0f, v));
			int iv = (int)std::round(clamped * 127.0f);
			if (iv < -128) iv = -128;
			if (iv > 127) iv = 127;
			return static_cast<uint8_t>(iv & 0xFF);

		}
		static inline uint32_t PackSnorm8x4(float x, float y, float z, float w) {
			return  (uint32_t)ToSnorm8(x)
				| ((uint32_t)ToSnorm8(y) << 8)
				| ((uint32_t)ToSnorm8(z) << 16)
				| ((uint32_t)ToSnorm8(w) << 24);

		}

		static inline uint32_t PackHalf2(float u, float v) {
			DirectX::PackedVector::XMHALF2 h2;
			XMStoreHalf2(&h2, DirectX::XMVectorSet(u, v, 0, 0));
			// XMHALF2 は 2×16bit。メモリを 32bit として見なす
			uint32_t out;
			std::memcpy(&out, &h2, sizeof(uint32_t));
			return out;

		}

		// VB 作成ショートハンド
		bool MakeVB(ID3D11Device* device, Microsoft::WRL::ComPtr<ID3D11Buffer>& vb,
			UINT byteWidth, UINT stride, const void* pData) {
			if (byteWidth == 0 || pData == nullptr) return true;
			D3D11_BUFFER_DESC bd{};
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.Usage = D3D11_USAGE_IMMUTABLE;
			bd.ByteWidth = byteWidth;
			D3D11_SUBRESOURCE_DATA sd{ pData };
			return SUCCEEDED(device->CreateBuffer(&bd, &sd, vb.GetAddressOf()));

		}

		bool MeshManager::CreateFromGLTF_SoA_R8Snorm(
			const std::wstring& pathW,
			const std::vector<Math::Vec3f>& positions,
			const std::vector<Math::Vec3f>& normals,
			const std::vector<Math::Vec4f>& tangents,
			const std::vector<Math::Vec2f>& tex0,
			const std::vector<std::array<uint8_t, 4>>& skinIdx,
			const std::vector<std::array<uint8_t, 4>>& skinWgt,
			const std::vector<uint32_t>& indices,
			MeshData& out,
			NormalWCustomFunc customFunc)
		{
			out = {};
			out.path = pathW;

			const size_t vtxCount = positions.size();
			if (vtxCount == 0 || indices.empty()) return false;

			// ====== ストリームデータを準備 ======
			// slot0: POSITION (float3)
			// そのまま

			// slot1: NORMAL/TANGENT（R8G8B8A8_SNORM × 2 を同じ slot に詰める）
			std::vector<uint32_t> packedTan;
			std::vector<uint32_t> packedNrm;
			packedTan.reserve(vtxCount);
			packedNrm.reserve(vtxCount);
			if (!tangents.empty()) {
				for (auto& t : tangents) {
					packedTan.push_back(PackSnorm8x4(t.x, t.y, t.z, t.w)); // w=handedness

				}

			}
			if (!normals.empty()) {
				if (customFunc != nullptr)
				{
					std::vector<float> wList = customFunc(positions);
					if (wList.size() != normals.size()) {
						LOG_ERROR("カスタムw成分のリストと法線リストのサイズが一致しません。");
					}

					for (auto i = 0; i < normals.size();++i) {
						const auto& n = normals[i];
						packedNrm.push_back(PackSnorm8x4(n.x, n.y, n.z, wList[i])); // w はカスタム関数で取得
					}
				}
				else
				{
					for (auto& n : normals) {
						packedNrm.push_back(PackSnorm8x4(n.x, n.y, n.z, 0.0f)); // w は未使用
					}
				}
			}

			// slot2: TEXCOORD0（R16G16_FLOAT）
			std::vector<uint32_t> packedUV;
			if (!tex0.empty()) {
				packedUV.reserve(vtxCount);
				for (auto& uv : tex0) {
					packedUV.push_back(PackHalf2(uv.x, uv.y));
				}
			}

			// slot3: SKIN（indices: R8G8B8A8_UINT, weights: R8G8B8A8_UNORM）
			std::vector<uint32_t> skinIdxU32;
			std::vector<uint32_t> skinWgtU32;
			if (!skinIdx.empty() && !skinWgt.empty()) {
				skinIdxU32.reserve(vtxCount);
				skinWgtU32.reserve(vtxCount);
				for (size_t i = 0; i < vtxCount; ++i) {
					const auto& si = skinIdx[i];
					const auto& sw = skinWgt[i]; // 0..255 を想定（既に合計255近辺に正規化済み推奨）
					uint32_t idx = (uint32_t)si[0] | ((uint32_t)si[1] << 8) | ((uint32_t)si[2] << 16) | ((uint32_t)si[3] << 24);
					uint32_t wgt = (uint32_t)sw[0] | ((uint32_t)sw[1] << 8) | ((uint32_t)sw[2] << 16) | ((uint32_t)sw[3] << 24);
					skinIdxU32.push_back(idx);
					skinWgtU32.push_back(wgt);
				}
			}

			// ====== VB/IB を作成 ======
			// slot0: POSITION
			if (!MakeVB(device, out.vbs[0], (UINT)(positions.size() * sizeof(Math::Vec3f)), sizeof(Math::Vec3f), positions.data())) return false;
			out.strides[0] = sizeof(Math::Vec3f); out.offsets[0] = 0; out.usedSlots.set(0);
			out.attribMap.emplace("POSITION0", MeshData::AttribBinding{ 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 });

			// slot1: Tangent と Normal を“同一 slot”に詰める（InputLayout で append offset 指定）
			// ここでは簡単化のため「別々の VB」にします（同一slotにまとめたい場合は 8byte/頂点の連結バッファを用意）
			if (!packedTan.empty()) {
				if (!MakeVB(device, out.vbs[1], (UINT)(packedTan.size() * sizeof(uint32_t)), /*stride*/4, packedTan.data())) return false;
				out.strides[1] = 4; out.offsets[1] = 0; out.usedSlots.set(1);
				out.attribMap.emplace("TANGENT0", MeshData::AttribBinding{ 1, DXGI_FORMAT_R8G8B8A8_SNORM, 0 });

			}
			if (!packedNrm.empty()) {
				// Normal を slot=1 の“別オフセット”で同VBに入れたい場合は、連結した1本のバッファを作る。
					// ここでは別 VB（slot=1 のままにしたいなら TANGENT を slot=1, NORMAL を slot=5 などに分けてもOK）。
				if (!MakeVB(device, out.vbs[5], (UINT)(packedNrm.size() * sizeof(uint32_t)), 4, packedNrm.data())) return false;
				out.strides[5] = 4; out.offsets[5] = 0; out.usedSlots.set(5);
				out.attribMap.emplace("NORMAL0", MeshData::AttribBinding{ 5, DXGI_FORMAT_R8G8B8A8_SNORM, 0 });

			}

			// slot2: UV0
			if (!packedUV.empty()) {
				if (!MakeVB(device, out.vbs[2], (UINT)(packedUV.size() * sizeof(uint32_t)), /*stride*/4, packedUV.data())) return false;
				out.strides[2] = 4; out.offsets[2] = 0; out.usedSlots.set(2);
				out.attribMap.emplace("TEXCOORD0", MeshData::AttribBinding{ 2, DXGI_FORMAT_R16G16_FLOAT, 0 });

			}

			// slot3: Skin
			if (!skinIdxU32.empty() && !skinWgtU32.empty()) {
				if (!MakeVB(device, out.vbs[3], (UINT)(skinIdxU32.size() * sizeof(uint32_t)), 4, skinIdxU32.data())) return false;
				if (!MakeVB(device, out.vbs[4], (UINT)(skinWgtU32.size() * sizeof(uint32_t)), 4, skinWgtU32.data())) return false;
				out.strides[3] = 4; out.offsets[3] = 0; out.usedSlots.set(3);
				out.strides[4] = 4; out.offsets[4] = 0; out.usedSlots.set(4);
				out.attribMap.emplace("BLENDINDICES0", MeshData::AttribBinding{ 3, DXGI_FORMAT_R8G8B8A8_UINT, 0 });
				out.attribMap.emplace("BLENDWEIGHT0", MeshData::AttribBinding{ 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0 });

			}

			// IB
			{
				D3D11_BUFFER_DESC bd{};
				bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
				bd.Usage = D3D11_USAGE_IMMUTABLE;
				bd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
				D3D11_SUBRESOURCE_DATA sd{ indices.data() };
				if (FAILED(device->CreateBuffer(&bd, &sd, out.ib.GetAddressOf()))) return false;
				out.indexCount = (uint32_t)indices.size();
			}

			return true;
		}

		bool MeshManager::AddFromSoA_R8Snorm(const std::wstring& sourcePath,
			const std::vector<Math::Vec3f>& positions,
			const std::vector<Math::Vec3f>& normals,
			const std::vector<Math::Vec4f>& tangents,
			const std::vector<Math::Vec2f>& tex0,
			const std::vector<std::array<uint8_t, 4>>& skinIdx,
			const std::vector<std::array<uint8_t, 4>>& skinWgt,
			const std::vector<uint32_t>& indices,
			MeshHandle& outHandle,
			NormalWCustomFunc customFunc)
		{
			MeshData data;
			if (!CreateFromGLTF_SoA_R8Snorm(sourcePath, positions, normals, tangents, tex0, skinIdx, skinWgt, indices, data, customFunc))
				return false;

			// ResourceManagerBase に data を登録
			outHandle = AllocateHandle();
			slots[outHandle.index].data = std::move(data);
			slots[outHandle.index].alive = true;
			refCount[outHandle.index].store(1, std::memory_order_relaxed);
			return true;
		}

		// ========= TBN 再計算（MikkTSpace互換方針） =========
		struct TBNAggregate { DirectX::XMFLOAT3 t{ 0,0,0 }; DirectX::XMFLOAT3 b{ 0,0,0 }; };

		// 面積加重のスムース法線
		static void RecomputeNormals_AreaWeighted(
			const std::vector<Math::Vec3f>& pos,
			const std::vector<uint32_t>& ib,
			std::vector<Math::Vec3f>& outN)
		{
			outN.assign(pos.size(), Math::Vec3f{ 0,0,0 });
			for (size_t i = 0; i < ib.size(); i += 3) {
				uint32_t i0 = ib[i + 0], i1 = ib[i + 1], i2 = ib[i + 2];
				auto p0 = pos[i0], p1 = pos[i1], p2 = pos[i2];
				auto e1 = p1 - p0; auto e2 = p2 - p0;
				auto fn = e1.cross(e2);          // 面積×2 を内包
				outN[i0] += fn; outN[i1] += fn; outN[i2] += fn;
			}
			for (auto& n : outN) {
				float l = n.length();
				if (l > 1e-20f) n /= l; else n = { 0,1,0 };
			}
		}

		// MikkTSpaceに準拠した考え方：UV微分からT/Bを出し、頂点で合算→Nに直交化→符号wを算出
		static void RecomputeTangentSpace_MikkLike(
			const std::vector<Math::Vec3f>& pos,
			const std::vector<Math::Vec3f>& nor,
			const std::vector<Math::Vec2f>& uv,
			const std::vector<uint32_t>& ib,
			std::vector<Math::Vec4f>& outTan)
		{
			const size_t n = pos.size();
			std::vector<DirectX::XMFLOAT3> accT(n, { 0,0,0 }), accB(n, { 0,0,0 });

			for (size_t i = 0; i < ib.size(); i += 3) {
				uint32_t i0 = ib[i + 0], i1 = ib[i + 1], i2 = ib[i + 2];
				auto p0 = pos[i0], p1 = pos[i1], p2 = pos[i2];
				auto uv0 = uv[i0], uv1 = uv[i1], uv2 = uv[i2];

				auto e1 = p1 - p0, e2 = p2 - p0;
				float du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
				float du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;
				float det = du1 * dv2 - du2 * dv1;
				if (fabsf(det) < 1e-20f) det = 1e-20f;
				float r = 1.0f / det;

				Math::Vec3f T = (e1 * dv2 - e2 * dv1) * r;
				Math::Vec3f B = (e2 * du1 - e1 * du2) * r;

				for (uint32_t k : {i0, i1, i2}) {
					auto& at = accT[k]; auto& ab = accB[k];
					at.x += T.x; at.y += T.y; at.z += T.z;
					ab.x += B.x; ab.y += B.y; ab.z += B.z;
				}
			}

			outTan.resize(n);
			for (size_t i = 0; i < n; ++i) {
				DirectX::XMVECTOR N = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&nor[i]));
				DirectX::XMVECTOR T = DirectX::XMLoadFloat3(&accT[i]);
				DirectX::XMVECTOR B = DirectX::XMLoadFloat3(&accB[i]);

				// Gram-Schmidt：Nに直交化して正規化
				T = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(T, DirectX::XMVectorMultiply(N, DirectX::XMVector3Dot(N, T))));
				// 手系の符号（handedness）: sign = dot(cross(N,T), B) < 0 ? -1 : +1
				float sign = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVector3Cross(N, T), B)) < 0.0f ? -1.0f : 1.0f;

				DirectX::XMFLOAT3 t3; DirectX::XMStoreFloat3(&t3, T);
				outTan[i] = { t3.x, t3.y, t3.z, sign };
			}
		}

		// ========= 全ストリーム一括 remap =========
		/*static*/ void MeshManager::ApplyRemapToStreams(
			const std::vector<uint32_t>& remap,
			const std::vector<Math::Vec3f>& inPos,
			const std::vector<Math::Vec3f>* inNor,
			const std::vector<Math::Vec4f>* inTan,
			const std::vector<Math::Vec2f>* inUV,
			const std::vector<std::array<uint8_t, 4>>* inSkinIdx,
			const std::vector<std::array<uint8_t, 4>>* inSkinWgt,
			size_t outVertexCount,
			MeshManager::RemappedStreams& out)
		{
			// 必須: POSITION
			out.positions.resize(outVertexCount);
			meshopt_remapVertexBuffer(out.positions.data(), inPos.data(), inPos.size(), sizeof(Math::Vec3f), remap.data());

			// 任意: 他ストリーム（存在するものだけ適用）
			if (inNor && !inNor->empty()) {
				out.normals.resize(outVertexCount);
				meshopt_remapVertexBuffer(out.normals.data(), inNor->data(), inNor->size(), sizeof(Math::Vec3f), remap.data());

			}
			if (inTan && !inTan->empty()) {
				out.tangents.resize(outVertexCount);
				meshopt_remapVertexBuffer(out.tangents.data(), inTan->data(), inTan->size(), sizeof(Math::Vec4f), remap.data());

			}
			if (inUV && !inUV->empty()) {
				out.tex0.resize(outVertexCount);
				meshopt_remapVertexBuffer(out.tex0.data(), inUV->data(), inUV->size(), sizeof(Math::Vec2f), remap.data());

			}
			if (inSkinIdx && !inSkinIdx->empty()) {
				out.skinIdx.resize(outVertexCount);
				meshopt_remapVertexBuffer(out.skinIdx.data(), inSkinIdx->data(), inSkinIdx->size(), sizeof(std::array<uint8_t, 4>), remap.data());

			}
			if (inSkinWgt && !inSkinWgt->empty()) {
				out.skinWgt.resize(outVertexCount);
				meshopt_remapVertexBuffer(out.skinWgt.data(), inSkinWgt->data(), inSkinWgt->size(), sizeof(std::array<uint8_t, 4>), remap.data());

			}
		}

#ifdef USE_MESHOPTIMIZER
		/*static*/ bool MeshManager::BuildClustersWithMeshoptimizer(
			const std::vector<Math::Vec3f>& positions,
			const std::vector<uint32_t>& indices,
			std::vector<ClusterInfo>& outClusters,
			std::vector<uint8_t>& outClusterTriangles, // ← uint8_t
			std::vector<uint32_t>& outClusterVertices,
			uint32_t maxVertsPerCluster,
			uint32_t maxTrisPerCluster,
			float coneWeight)
		{
			if (positions.empty() || indices.empty()) return false;

			// 上限見積り
			const size_t maxMeshlets =
				meshopt_buildMeshletsBound(indices.size(), maxVertsPerCluster, maxTrisPerCluster);

			std::vector<meshopt_Meshlet> meshlets(maxMeshlets);

			// 出力バッファを上限サイズで確保（triangles は 3 バイト/三角形）
			outClusterVertices.resize(maxMeshlets * maxVertsPerCluster);
			outClusterTriangles.resize(maxMeshlets * maxTrisPerCluster * 3);

			const float* vpos = reinterpret_cast<const float*>(&positions[0].x);

			// 生成
			const size_t meshletCount = meshopt_buildMeshlets(
				meshlets.data(),
				outClusterVertices.data(),                         // uint32_t*
				outClusterTriangles.data(),                        // uint8_t*
				indices.data(), indices.size(),
				vpos, positions.size(), sizeof(Math::Vec3f),
				maxVertsPerCluster, maxTrisPerCluster, coneWeight);

			meshlets.resize(meshletCount);

			// メタ（バウンディング等）
			outClusters.clear();
			outClusters.reserve(meshletCount);

			for (size_t i = 0; i < meshletCount; ++i) {
				const meshopt_Meshlet& m = meshlets[i];

				const uint32_t* verts = &outClusterVertices[m.vertex_offset];
				const uint8_t* trisB = &outClusterTriangles[m.triangle_offset * 3]; // 3B/tri
				const uint32_t  triCnt = m.triangle_count;

				// バウンディング算出（meshoptimizer 提供）
				const meshopt_Bounds b = meshopt_computeMeshletBounds(
					verts, reinterpret_cast<const unsigned char*>(trisB), triCnt,
					vpos, positions.size(), sizeof(Math::Vec3f));

				ClusterInfo ci{};
				ci.triOffset = m.triangle_offset * 3; // バイト配列上の先頭オフセット（要 3 の倍数）
				ci.triCount = triCnt;
				ci.center = { b.center[0], b.center[1], b.center[2] };
				ci.radius = b.radius;
				ci.coneAxis = { b.cone_axis[0], b.cone_axis[1], b.cone_axis[2] };
				ci.coneCutoff = b.cone_cutoff;
				outClusters.push_back(ci);
			}

			// 使った分だけ縮める（meshletCount==0 に注意）
			if (meshletCount > 0) {
				const meshopt_Meshlet& last = meshlets.back();
				outClusterVertices.resize(last.vertex_offset + last.vertex_count);
				outClusterTriangles.resize((last.triangle_offset + last.triangle_count) * 3);
			}
			else {
				outClusterVertices.clear();
				outClusterTriangles.clear();
			}

			return meshletCount > 0;
		}
#endif // USE_MESHOPTIMIZER

		void MeshManager::RemoveFromCaches(uint32_t idx)
		{
			auto& data = slots[idx].data;
			auto pathIt = pathToHandle.find(data.path);
			if (pathIt != pathToHandle.end()) {
				pathToHandle.erase(pathIt);
			}
		}
		void MeshManager::DestroyResource(uint32_t idx, uint64_t currentFrame)
		{
			auto& data = slots[idx].data;
			for (auto& vb : data.vbs)
				if (vb) { vb.Reset(); }
			if (data.ib) { data.ib.Reset(); }
		}

		bool MeshManager::InitCommonMeshes()
		{
			// すでに有効なら何もしない（ResourceManagerBase の IsValid を想定）
			if (IsValid(spriteQuadHandle_)) return true;

			// 単位クアッド（中心原点、XY: [-0.5, +0.5]、UV: [0,1]）
			using Math::Vec2f; using Math::Vec3f;

			// 左下→右下→右上→左上（時計回り）
			const std::vector<Vec3f> positions = {
				{-0.5f, -0.5f, 0.0f},
				{ +0.5f, -0.5f, 0.0f},
				{ +0.5f, +0.5f, 0.0f},
				{-0.5f, +0.5f, 0.0f},
			};
			// D3D 標準のテクスチャ座標系（左上(0,0)）に合わせて下→上で v を反転
			const std::vector<Vec2f> tex0 = {
				{0.0f, 1.0f}, // 左下
				{1.0f, 1.0f}, // 右下
				{1.0f, 0.0f}, // 右上
				{0.0f, 0.0f}, // 左上
			};
			const std::vector<uint32_t> indices = {
				0, 1, 2,  0, 2, 3
			};

			// SoA 経由で VB/IB と attribMap を構築（NORMAL/TANGENT/SKIN は不要なので空）
			// 一意キーとして擬似パスを与えてキャッシュ可能にする
			const std::wstring key = L"__builtin:/sprite_unit_quad";

			MeshHandle h{};
			const bool ok = AddFromSoA_R8Snorm(
				key,
				positions,
				/*normals*/  std::vector<Vec3f>{},
				/*tangents*/ std::vector<Math::Vec4f>{},
				/*tex0*/     tex0,
				/*skinIdx*/  std::vector<std::array<uint8_t, 4>>{},
				/*skinWgt*/  std::vector<std::array<uint8_t, 4>>{},
				indices,
				h
			);
			if (!ok) return false;

			spriteQuadHandle_ = h;
			return true;
		}
	}
}
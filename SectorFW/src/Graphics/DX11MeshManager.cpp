#include "Graphics/DX11/DX11MeshManager.h"

#include "Util/logger.h"

#include <numbers>

namespace SectorFW
{
	namespace Graphics
	{
		struct VertexPNUV {
			Math::Vec3f pos;
			Math::Vec3f normal;
			Math::Vec2f uv;
		};

		// 24頂点+36インデックスを生成（中心原点、寸法 w,h,d）
		static void MakeBox(float w, float h, float d,
			std::vector<VertexPNUV>& outVerts,
			std::vector<uint32_t>& outIndices);

		// 頂点/インデックス生成（UV 球）
		//  - radius: 半径
		//  - slices: 経度分割数（最小 3）
		//  - stacks: 緯度分割数（最小 2）
		// 生成結果は CW（時計回り）で表面になるインデックス
		static void MakeSphere(float radius, uint32_t slices, uint32_t stacks,
			std::vector<VertexPNUV>& outVerts,
			std::vector<uint32_t>& outIndices);


		DX11MeshManager::DX11MeshManager(ID3D11Device* dev)
			: device(dev) {
			std::vector<VertexPNUV> boxVerts;
			std::vector<uint32_t> boxIndices;

			MakeBox(1.0f, 1.0f, 1.0f, boxVerts, boxIndices);
			DX11MeshCreateDesc boxDesc{
				.vertices = boxVerts.data(),
				.vSize = boxVerts.size() * sizeof(VertexPNUV),
				.stride = sizeof(VertexPNUV),
				.indices = boxIndices.data(),
				.iSize = boxIndices.size() * sizeof(uint32_t),
				.sourcePath = L"__internal__/Box"
			};

			Add(boxDesc, boxHandle);

			std::vector<VertexPNUV> sphereVerts;
			std::vector<uint32_t> sphereIndices;

			MakeSphere(0.5f, 8, 8, sphereVerts, sphereIndices);
			DX11MeshCreateDesc sphereDesc{
				.vertices = sphereVerts.data(),
				.vSize = sphereVerts.size() * sizeof(VertexPNUV),
				.stride = sizeof(VertexPNUV),
				.indices = sphereIndices.data(),
				.iSize = sphereIndices.size() * sizeof(uint32_t),
				.sourcePath = L"__internal__/Sphere"
			};

			Add(sphereDesc, sphereHandle);
		}
		DX11MeshData DX11MeshManager::CreateResource(const DX11MeshCreateDesc& desc, MeshHandle h)
		{
			DX11MeshData mesh{};
			// Vertex Buffer
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.ByteWidth = (UINT)desc.vSize;
			vbDesc.Usage = D3D11_USAGE_DEFAULT;
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			HRESULT hr;

			D3D11_SUBRESOURCE_DATA vbData = {};
			vbData.pSysMem = desc.vertices;
			hr = device->CreateBuffer(&vbDesc, &vbData, &mesh.vb);
			if (FAILED(hr)) {
				LOG_ERROR("Failed to create vertex buffer: {}", hr);
				assert(false && "Failed to create vertex buffer");
				return {};
			}

			// Index Buffer
			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.ByteWidth = (UINT)desc.iSize;
			ibDesc.Usage = D3D11_USAGE_DEFAULT;
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA ibData = {};
			ibData.pSysMem = desc.indices;
			hr = device->CreateBuffer(&ibDesc, &ibData, &mesh.ib);
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

		void DX11MeshManager::RemoveFromCaches(uint32_t idx)
		{
			auto& data = slots[idx].data;
			auto pathIt = pathToHandle.find(data.path);
			if (pathIt != pathToHandle.end()) {
				pathToHandle.erase(pathIt);
			}
		}
		void DX11MeshManager::DestroyResource(uint32_t idx, uint64_t currentFrame)
		{
			auto& data = slots[idx].data;
			if (data.vb) { data.vb.Reset(); }
			if (data.ib) { data.ib.Reset(); }
		}

		// 24頂点+36インデックスを生成（中心原点、寸法 w,h,d）
		static void MakeBox(float w, float h, float d,
			std::vector<VertexPNUV>& outVerts,
			std::vector<uint32_t>& outIndices)
		{
			const float hx = w * 0.5f;
			const float hy = h * 0.5f;
			const float hz = d * 0.5f;

			outVerts.clear();
			outIndices.clear();
			outVerts.reserve(24);
			outIndices.reserve(36);

			// 各面 4 頂点（UV は [0,0]=左上 想定／画像の原点は上左）
			// +Z (Front)
			outVerts.push_back({ Math::Vec3f(-hx,-hy,+hz), Math::Vec3f(0,0, 1), Math::Vec2f(0,1) }); // 0  bl
			outVerts.push_back({ Math::Vec3f(-hx,+hy,+hz), Math::Vec3f(0,0, 1), Math::Vec2f(0,0) }); // 1  tl
			outVerts.push_back({ Math::Vec3f(+hx,+hy,+hz), Math::Vec3f(0,0, 1), Math::Vec2f(1,0) }); // 2  tr
			outVerts.push_back({ Math::Vec3f(+hx,-hy,+hz), Math::Vec3f(0,0, 1), Math::Vec2f(1,1) }); // 3  br

			// -Z (Back)
			outVerts.push_back({ Math::Vec3f(+hx,-hy,-hz), Math::Vec3f(0,0,-1), Math::Vec2f(0,1) }); // 4  bl (背面視点)
			outVerts.push_back({ Math::Vec3f(+hx,+hy,-hz), Math::Vec3f(0,0,-1), Math::Vec2f(0,0) }); // 5  tl
			outVerts.push_back({ Math::Vec3f(-hx,+hy,-hz), Math::Vec3f(0,0,-1), Math::Vec2f(1,0) }); // 6  tr
			outVerts.push_back({ Math::Vec3f(-hx,-hy,-hz), Math::Vec3f(0,0,-1), Math::Vec2f(1,1) }); // 7  br

			// +X (Right)
			outVerts.push_back({ Math::Vec3f(+hx,-hy,+hz), Math::Vec3f(1,0,0), Math::Vec2f(0,1) });  // 8  bl (右側面を+X側から見る)
			outVerts.push_back({ Math::Vec3f(+hx,+hy,+hz), Math::Vec3f(1,0,0), Math::Vec2f(0,0) });  // 9  tl
			outVerts.push_back({ Math::Vec3f(+hx,+hy,-hz), Math::Vec3f(1,0,0), Math::Vec2f(1,0) });  // 10 tr
			outVerts.push_back({ Math::Vec3f(+hx,-hy,-hz), Math::Vec3f(1,0,0), Math::Vec2f(1,1) });  // 11 br

			// -X (Left)
			outVerts.push_back({ Math::Vec3f(-hx,-hy,-hz), Math::Vec3f(-1,0,0), Math::Vec2f(0,1) }); // 12 bl (左側面を-X側から見る)
			outVerts.push_back({ Math::Vec3f(-hx,+hy,-hz), Math::Vec3f(-1,0,0), Math::Vec2f(0,0) }); // 13 tl
			outVerts.push_back({ Math::Vec3f(-hx,+hy,+hz), Math::Vec3f(-1,0,0), Math::Vec2f(1,0) }); // 14 tr
			outVerts.push_back({ Math::Vec3f(-hx,-hy,+hz), Math::Vec3f(-1,0,0), Math::Vec2f(1,1) }); // 15 br

			// +Y (Top)
			outVerts.push_back({ Math::Vec3f(-hx,+hy,+hz), Math::Vec3f(0,1,0), Math::Vec2f(0,1) });  // 16 bl (上面を+Y側から見る、u=+X, v=-Z)
			outVerts.push_back({ Math::Vec3f(-hx,+hy,-hz), Math::Vec3f(0,1,0), Math::Vec2f(0,0) });  // 17 tl
			outVerts.push_back({ Math::Vec3f(+hx,+hy,-hz), Math::Vec3f(0,1,0), Math::Vec2f(1,0) });  // 18 tr
			outVerts.push_back({ Math::Vec3f(+hx,+hy,+hz), Math::Vec3f(0,1,0), Math::Vec2f(1,1) });  // 19 br

			// -Y (Bottom)
			outVerts.push_back({ Math::Vec3f(-hx,-hy,-hz), Math::Vec3f(0,-1,0), Math::Vec2f(0,1) }); // 20 bl (下面を-Y側から見る、u=+X, v=+Z)
			outVerts.push_back({ Math::Vec3f(-hx,-hy,+hz), Math::Vec3f(0,-1,0), Math::Vec2f(0,0) }); // 21 tl
			outVerts.push_back({ Math::Vec3f(+hx,-hy,+hz), Math::Vec3f(0,-1,0), Math::Vec2f(1,0) }); // 22 tr
			outVerts.push_back({ Math::Vec3f(+hx,-hy,-hz), Math::Vec3f(0,-1,0), Math::Vec2f(1,1) }); // 23 br

			auto addFaceCW = [&](uint32_t base) {
				// CW: (0,2,1), (0,3,2)
				outIndices.push_back(base + 0);
				outIndices.push_back(base + 2);
				outIndices.push_back(base + 1);
				outIndices.push_back(base + 0);
				outIndices.push_back(base + 3);
				outIndices.push_back(base + 2);
				};
			addFaceCW(0);   // +Z
			addFaceCW(4);   // -Z
			addFaceCW(8);   // +X
			addFaceCW(12);  // -X
			addFaceCW(16);  // +Y
			addFaceCW(20);  // -Y
		}

		static void MakeSphere(float radius, uint32_t slices, uint32_t stacks,
			std::vector<VertexPNUV>& outVerts,
			std::vector<uint32_t>& outIndices)
		{
			slices = std::max<uint32_t>(slices, 3u);
			stacks = std::max<uint32_t>(stacks, 2u);

			const uint32_t vxCols = slices + 1;      // U=1 列の複製を含む
			const uint32_t vxRows = stacks + 1;      // 極を含む
			outVerts.resize(size_t(vxCols) * vxRows);

			// 頂点生成
			size_t idx = 0;
			for (uint32_t iy = 0; iy <= stacks; ++iy) {
				const float v = float(iy) / float(stacks);   // 0..1 (0=北極, 1=南極) : DirectX のテクスチャ座標に合う
				const float phi = v * std::numbers::pi_v<float>;                   // 0..π
				const float sp = sinf(phi);
				const float cp = cosf(phi);

				for (uint32_t ix = 0; ix <= slices; ++ix) {
					const float u = float(ix) / float(slices);  // 0..1 （U=1 列はシーム複製）
					const float theta = u * std::numbers::pi_v<float> *2; // 0..2π
					const float st = sinf(theta);
					const float ct = cosf(theta);

					Math::Vec3f n = { sp * ct, cp, sp * st };         // 単位法線（原点中心）
					Math::Vec3f p = { radius * n.x, radius * n.y, radius * n.z };

					outVerts[idx++] = VertexPNUV{ p, n, Math::Vec2f{ u, v } };
				}
			}

			// インデックス生成（各クアッドを 2 三角形に分解）? CW で表面
			outIndices.clear();
			outIndices.reserve(size_t(slices) * stacks * 6);

			for (uint32_t iy = 0; iy < stacks; ++iy) {
				for (uint32_t ix = 0; ix < slices; ++ix) {
					const uint32_t k0 = iy * vxCols + ix;      // 上段・左
					const uint32_t k1 = (iy + 1) * vxCols + ix;      // 下段・左
					const uint32_t k2 = k1 + 1;                // 下段・右
					const uint32_t k3 = k0 + 1;                // 上段・右

					// CW（FrontCounterClockwise = FALSE を想定）
					outIndices.push_back(k0); outIndices.push_back(k2); outIndices.push_back(k1);
					outIndices.push_back(k0); outIndices.push_back(k3); outIndices.push_back(k2);
				}
			}
		}
	}
}
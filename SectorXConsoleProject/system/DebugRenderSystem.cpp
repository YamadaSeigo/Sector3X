#include <SectorFW/Math/Vector.hpp>
#include <numbers>

#include "DebugRenderSystem.h"

// 24頂点+36インデックスを生成（中心原点、寸法 w,h,d）
void MakeBox(float w, float h, float d,
	std::vector<Debug::VertexPNUV>& outVerts,
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

void MakeBoxLines(float w, float h, float d,
	std::vector<Debug::LineVertex>& outVerts,
	std::vector<uint32_t>& outIndices)
{
	const float hx = w * 0.5f;
	const float hy = h * 0.5f;
	const float hz = d * 0.5f;

	// 8 corners: (x,y,z)
	outVerts = {
		Debug::LineVertex{Math::Vec3f{-hx,-hy,-hz}, 0xFFFFFFFF}, // 0
		Debug::LineVertex{Math::Vec3f{-hx,+hy,-hz}, 0xFFFFFFFF}, // 1
		Debug::LineVertex{Math::Vec3f{+hx,+hy,-hz}, 0xFFFFFFFF}, // 2
		Debug::LineVertex{Math::Vec3f{+hx,-hy,-hz}, 0xFFFFFFFF}, // 3
		Debug::LineVertex{Math::Vec3f{-hx,-hy,+hz}, 0xFFFFFFFF}, // 4
		Debug::LineVertex{Math::Vec3f{-hx,+hy,+hz}, 0xFFFFFFFF}, // 5
		Debug::LineVertex{Math::Vec3f{+hx,+hy,+hz}, 0xFFFFFFFF}, // 6
		Debug::LineVertex{Math::Vec3f{+hx,-hy,+hz}, 0xFFFFFFFF} // 7
	};

	// 12 edges (pairs)
	outIndices = {
		0,1, 1,2, 2,3, 3,0, // back (-Z)
		4,5, 5,6, 6,7, 7,4, // front (+Z)
		0,4, 1,5, 2,6, 3,7  // side connectors
	};
}

void MakeSphere(float radius, uint32_t slices, uint32_t stacks,
	std::vector<Debug::VertexPNUV>& outVerts,
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

			outVerts[idx++] = Debug::VertexPNUV{ p, n, Math::Vec2f{ u, v } };
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

// segments: 8 以上推奨。LINELIST なので閉ループは (i, i+1), (last, first) を張る
void AppendCircle(float radius, uint32_t segments, CirclePlane plane,
	std::vector<Debug::LineVertex>& verts, std::vector<uint32_t>& idx,
	float yOffset, float rotY) // rotY は Y 軸回り回転（YZ->任意子午線用）
{
	segments = std::max<uint32_t>(segments, 4u);
	const uint16_t base = static_cast<uint16_t>(verts.size());
	verts.reserve(verts.size() + segments);
	idx.reserve(idx.size() + segments * 2);

	const float sY = sinf(rotY), cY = cosf(rotY);

	for (uint32_t i = 0; i < segments; ++i) {
		const float t = (i / float(segments)) * std::numbers::pi_v<float> *2.0f;
		const float ct = cosf(t), st = sinf(t);
		float x = 0, y = 0, z = 0;

		switch (plane) {
		case CirclePlane::XZ: // 赤道（XZ）- y=一定
			x = radius * ct;
			y = yOffset;
			z = radius * st;
			break;
		case CirclePlane::XY: // 子午線（XY）- z=0
			x = radius * ct;
			y = radius * st;
			z = 0.0f;
			break;
		case CirclePlane::YZ: // 子午線（YZ）- x=0 を rotY で Y 回転して任意経度へ
		{
			// まず YZ 円 (x=0, y=r*cos t, z=r*sin t) を Y 回転
			const float x0 = 0.0f;
			const float y0 = radius * ct;
			const float z0 = radius * st;
			x = x0 * cY + z0 * sY;         // =  r * st * sY
			y = y0;                         // =  r * ct
			z = -x0 * sY + z0 * cY;         // =  r * st * cY
			break;
		}
		}
		verts.push_back({ Math::Vec3f{ x, y, z }, 0xFFFFFFFF });
	}

	for (uint32_t i = 0; i < segments; ++i) {
		const uint32_t a = base + uint32_t(i);
		const uint32_t b = base + uint32_t((i + 1) % segments);
		idx.push_back(a); idx.push_back(b);
	}
}

// 十字（3 本）のみ
void MakeSphereCrossLines(float radius, uint32_t segments,
	std::vector<Debug::LineVertex>& outVerts,
	std::vector<uint32_t>& outIndices,
	bool addXY, bool addYZ, bool addXZ)
{
	outVerts.clear(); outIndices.clear();
	if (addXZ) AppendCircle(radius, segments, CirclePlane::XZ, outVerts, outIndices);             // 赤道
	if (addYZ) AppendCircle(radius, segments, CirclePlane::YZ, outVerts, outIndices, 0.0f, 0.0f); // 子午線
	if (addXY) AppendCircle(radius, segments, CirclePlane::XY, outVerts, outIndices);             // 直交子午線
}
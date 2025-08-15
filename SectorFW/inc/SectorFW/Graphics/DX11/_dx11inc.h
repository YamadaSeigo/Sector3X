#pragma once

#ifndef DIRECTX11_H
#define DIRECTX11_H

#include <d3d11.h>
#pragma comment(lib,"d3d11.lib")

#include <d3dcompiler.h>
#pragma comment(lib,"d3dcompiler.lib")

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

constexpr D3D_PRIMITIVE_TOPOLOGY D3DTopologyLUT[] = {
	D3D_PRIMITIVE_TOPOLOGY_UNDEFINED,             // 0: Undefined
	D3D_PRIMITIVE_TOPOLOGY_POINTLIST,             // 1: PointList
	D3D_PRIMITIVE_TOPOLOGY_LINELIST,              // 2: LineList
	D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,             // 3: LineStrip
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,          // 4: TriangleList
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,         // 5: TriangleStrip
	D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,          // 6: LineListAdj
	D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,         // 7: LineStripAdj
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,      // 8: TriangleListAdj
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ,     // 9: TriangleStripAdj
	D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST,  // 10: Patch1
	D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST,  // 11: Patch2
	// ...
};

#endif //! DIRECTX11_H

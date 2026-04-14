#pragma once

#include <vector>
#include <SectorFW/Math/Vector.hpp>

#include "../app/appconfig.h"


class TerrainWater
{
public:
	struct ClusterNode
	{
		uint32_t clusterX; // クラスターのX座標
		uint32_t clusterZ; // クラスターのZ座標
	};

	struct BuilderParams
	{
		std::string heigthMapPath;
		std::string normal1Path;
		std::string normal2Path;
		float worldMapSizeX; // ワールド全体のX方向のサイズ
		float worldMapSizeZ; // ワールド全体のZ方向のサイズ
		Math::Vec3f worldOffset; // ワールドオフセット
		float heightScale; // (0.0 - 1.0）* heightScale で実際の高さになる
		uint32_t clusterCellsX; // クラスターごとの最大分割数(X方向)
		uint32_t clusterCellsZ; // クラスターごとの最大分割数(Z方向)
		float cellSize; // Heightfield のセルサイズ (ワールド単位)
	};

	struct CSParam
	{
		// メインカメラ用フラスタム
		Math::Frustumf MainFrustum;

		// LOD 判定用（画面サイズベース）
		Math::Matrix4x4f ViewProj;

		uint32_t MaxVisibleIndices; // Visible_* の最大 index 数（uint 単位）
		uint32_t LodLevels; // 期待値: 3~4（LOD0..LOD3）
		float ScreenSize[2]; // px

		// メイン用 LOD しきい値 (px): x=0/1, y=1/2, z=2/3, w=3/4
		float LodPxThreshold_Main[4];

		// Heightfield 全体の頂点数
		uint32_t gVertsX; // (= vertsX)
		uint32_t gVertsZ; // (= vertsZ)
		uint32_t gCellsX; // (= cellsX)
		uint32_t gCellsZ; // (= cellsZ)
	};

	struct GridCB
	{
		Math::Vec3f gOrigin; // ワールド座標の基準 (x,z)
		float heightScale;

		uint32_t gVertsX; // Heightfield 全体の頂点数X
		uint32_t gVertsZ; // Heightfield 全体の頂点数Z

		//使ってないけど一応(どうせpaddingがいるので)
		uint32_t gDimX; // クラスタ数X
		uint32_t gDimZ; // クラスタ数Z

		uint32_t gClusterCellsX; // クラスターごとの最大分割数X
		uint32_t gClusterCellsZ; // クラスターごとの最大分割数Z

		float gCellInvCount[2]; // Heightfield のセルの逆数 (1/gClusterCellsX, 1/gClusterCellsZ)

		float gCellSize[2]; // Heightfield のセルサイズ (x,z)
		float gHeightMapInvSize[2]; // 1/width, 1/height (height/normal texture用)
	};

	struct FrameCB
	{
		Math::Matrix4x4f gInvProj;

		Math::Vec3f gCameraPosWS = {};
		float  gTime = 0.0f;

		float gWaterColor[4] = { 0.85f, 0.95f, 1.00f, 1.0f };      // rgb=color, a=unused
		float gShallowColor[4] = { 0.10f, 0.22f, 0.18f, 1.0f };    // optional
		float gDeepColor[4] = { 0.03f, 0.10f, 0.12f, 1.0f };       // optional
		float fogColor[4] = { 0.62f, 0.70f, 0.76f, 1.0f };

		float gNormalTiling0[2] = {1.0f,1.0f};
		float gNormalTiling1[2] = {1.0f,1.0f};

		float gFlowDir0[2] = { 0.8f,  0.2f };
		float gFlowDir1[2] = { -0.3f,  0.9f };

		float  gFlowSpeed0 = 0.02f;
		float  gFlowSpeed1 = 0.03f;
		float  gNormalStrength = 1.1f;
		float  gSpecPower = 20.0f;

		float depthColorScale = 0.35f;
		float waterAlpha = 0.92f;
		float fogStart = 35.0f;
		float fogEnd = 1200.0f;

		float screenSize[2] = {App::WINDOW_WIDTH, App::WINDOW_HEIGHT};
		float shoreFadeScale = 1.5f;
		float pad1;

		float gFoamColor[4] = {0.95f, 1.0, 1.0f};      // 白に少し黄緑や青を混ぜても良い
		float  gFoamDepthStart = 0.02f; // 例: 0.02
		float  gFoamDepthEnd = 0.25f;   // 例: 0.25
		float  gFoamIntensity = 0.8f;  // 例: 0.8
		float  gFoamNoiseScale = 6.0f; // 例: 6.0
	};


	TerrainWater() = default;
	~TerrainWater() = default;

	void BuildCluster(BuilderParams& p);
	bool CompileShader(ID3D11Device* dev,
		const wchar_t* csBuildPath,
		const wchar_t* csArgPath,
		const wchar_t* vsPath,
		const wchar_t* psPath);

	bool CreateResource(SFW::Graphics::DX11::GraphicsDevice& graphics);

	struct ComputeContext
	{
		ID3D11DeviceContext* devCtx;
		Math::Frustumf mainFrustum;
		Math::Matrix4x4f viewProj;
		float screenSize[2];
	};

	void ComputeVisibleIndices(ComputeContext& ctx, ComPtr<ID3D11Buffer> cbCamera);

	void Render(ID3D11DeviceContext* devCtx,
		ComPtr<ID3D11Buffer> globalLightCB,
		ComPtr<ID3D11ShaderResourceView> depthSRV,
		Math::Matrix4x4f invProj,
		Math::Vec3f camPos,
		float deltaTime,
		float fogStart,
		float fogEnd);

private:
	std::vector<ClusterNode> waterClusters; // 水面とみなされるクラスターのリスト
	std::vector<Math::AABB3f> waterClusterBounds; // 水面クラスターのAABBリスト(主にLODの計算に使用する)


	BuilderParams params;
	uint32_t clustersX = 1; // クラスター数X
	uint32_t clustersZ = 1; // クラスター数Z

	FrameCB frameCBData;

	ComPtr<ID3D11ComputeShader> csBuild;
	ComPtr<ID3D11ComputeShader> csArg;
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;

	ComPtr<ID3D11ShaderResourceView> heightMapSRV;

	ComPtr<ID3D11ShaderResourceView> normal1SRV;
	ComPtr<ID3D11ShaderResourceView> normal2SRV;

	ComPtr<ID3D11Buffer> argsBuf;
	ComPtr<ID3D11Buffer> argsUAVBuf;
	ComPtr<ID3D11UnorderedAccessView> argsUAV;

	ComPtr<ID3D11ShaderResourceView> clusterAabbMinSRV;
	ComPtr<ID3D11ShaderResourceView> clusterAabbMaxSRV;

	ComPtr<ID3D11ShaderResourceView> clusterGridRectSRV;

	ComPtr<ID3D11UnorderedAccessView> counterUAV;

	ComPtr<ID3D11ShaderResourceView> indexSRV;
	ComPtr<ID3D11UnorderedAccessView> indexUAV;

	ComPtr<ID3D11Buffer> cbParam;
	ComPtr<ID3D11Buffer> cbGrid;

	ComPtr<ID3D11Buffer> cbCameraFrame;

	ComPtr<ID3D11Buffer> cbFrame;

	ComPtr<ID3D11SamplerState> wrapSampler;
	ComPtr<ID3D11SamplerState> pointSampler;
};

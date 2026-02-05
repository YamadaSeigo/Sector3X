#pragma once

#include "RenderDefine.h"
#include "TiledDeferredRender.h"

struct DeferredCameraBuffer {
	Math::Matrix4x4f invViewProj = {};
	Math::Vec3f camForward = {};
	float padding = {};
	Math::Vec3f camPos = {};
	float padding2 = {};
};

struct TileCameraBuffer {
	Math::Matrix4x4f view = {};
	Math::Matrix4x4f invProj = {};
	Math::Matrix4x4f invViewProj = {};
	Math::Vec3f camPos = {};
	uint32_t pad;
};

class DeferredRenderingService : public ECS::IUpdateService
{
public:
	static inline constexpr const char* BUFFER_NAME = "DeferredCameraBuffer";

	DeferredRenderingService(
		ID3D11Device* device,
		Graphics::DX11::BufferManager* bufferManager,
		Graphics::DX11::TextureManager* textureManager,
		uint32_t w, uint32_t h,
		const wchar_t* csBuildFrustum,
		const wchar_t* csTileCulling,
		const wchar_t* csDrawTileLight)
		:bufferManager(bufferManager)
	{
		using namespace Graphics;

		DX11::BufferCreateDesc bufferDesc{};
		bufferDesc.name = BUFFER_NAME;
		bufferDesc.size = sizeof(DeferredCameraBuffer);
		bufferManager->Add(bufferDesc, lightCameraBufferHandle);

		// Tile用カメラバッファ
		D3D11_BUFFER_DESC tileBufDesc = {};
		tileBufDesc.ByteWidth = sizeof(TileCameraBuffer);
		tileBufDesc.Usage = D3D11_USAGE_DYNAMIC;
		tileBufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		tileBufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = device->CreateBuffer(&tileBufDesc, nullptr, &tileCameraBuffer);
		if (FAILED(hr)) {
			assert(false && "Failed to create tile camera buffer");
		}


		DX11::TextureRecipe recipe;
		recipe.width = w;
		recipe.height = h;
		recipe.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		recipe.mipLevels = 1;
		recipe.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		recipe.usage = D3D11_USAGE_DEFAULT;
		recipe.arraySize = 1;

		//AlbedoもHDRに対応させる場合はDXGI_FORMAT_R16G16B16A16_FLOATにする
		DXGI_FORMAT texFormats[DeferredTextureCount] = {
			DXGI_FORMAT_R8G8B8A8_UNORM,		//"AlbedoAO",
			DXGI_FORMAT_R8G8B8A8_UNORM,		//"NormalRoughness",
			DXGI_FORMAT_R16G16B16A16_FLOAT  //"EmissiveMetallic"
		};

		for (int i = 0; i < DeferredTextureCount; ++i)
		{
			DX11::TextureCreateDesc texDesc;
			recipe.format = texFormats[i];
			texDesc.recipe = &recipe;
			texDesc.path = ""; // 空パスで生成モード
			textureManager->Add(texDesc, GBufferHandle[i]);
		}

		{
			DX11::TextureRecipe r;
			r.width = w;
			r.height = h;
			r.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			r.mipLevels = 1;
			r.bindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
			r.usage = D3D11_USAGE_DEFAULT;
			r.arraySize = 1;
			DX11::TextureCreateDesc desc;
			desc.recipe = &r;
			desc.path = "";
			textureManager->Add(desc, LightBufferTexHandle);
		}

		tiledDeferredRender.Create(device, w, h, csBuildFrustum, csTileCulling, csDrawTileLight);
	}

	void PreUpdate(double delta) override
	{
		using namespace Graphics;

		currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;

		DX11::BufferUpdateDesc updateDesc;
		{
			auto bufData = bufferManager->Get(lightCameraBufferHandle);
			updateDesc.buffer = bufData.ref().buffer;
		}

		updateDesc.data = &lightCameraBufferData[currentSlot];
		updateDesc.size = sizeof(DeferredCameraBuffer);
		updateDesc.isDelete = false;

		bufferManager->UpdateBuffer(updateDesc, currentSlot);

		updateDesc.buffer = tileCameraBuffer;
		updateDesc.data = &tileCameraBufferData[currentSlot];
		updateDesc.size = sizeof(TileCameraBuffer);

		bufferManager->UpdateBuffer(updateDesc, currentSlot);
	}

	void UpdateCameraBufferData(const DeferredCameraBuffer& data)
	{
		std::lock_guard lock(updateMutex);

		lightCameraBufferData[currentSlot] = data;
	}

	void UpdateTileCameraBufferData(const TileCameraBuffer& data)
	{
		std::lock_guard lock(updateMutex);
		tileCameraBufferData[currentSlot] = data;
	}

	const Graphics::TextureHandle* GetGBufferHandles() const noexcept
	{
		return GBufferHandle;
	}

	const Graphics::TextureHandle GetLightTexHandle() const noexcept
	{
		return LightBufferTexHandle;
	}

	void DrawTiledLightPass(ID3D11DeviceContext* ctx,
		ID3D11ShaderResourceView* normalLightSRV,
		ID3D11ShaderResourceView* fireflyLightSRV,
		ID3D11ShaderResourceView* albedoSRV,
		ID3D11ShaderResourceView* normalSRV,
		ID3D11ShaderResourceView* depthSRV,
		ID3D11UnorderedAccessView* outLightUAV,
		ID3D11Buffer* lightCountCB,
		ID3D11SamplerState* pointSampler
		)
	{
		// タイルフラスタム構築
		tiledDeferredRender.BuildTileFrustums(ctx, tileCameraBuffer.Get());

		// ライトカリング
		tiledDeferredRender.TileCullingLight(
			ctx,
			normalLightSRV,
			fireflyLightSRV,
			depthSRV,
			tileCameraBuffer.Get(),
			lightCountCB
		);

		// タイルライト描画
		tiledDeferredRender.DrawTileLight(
			ctx,
			normalLightSRV,
			fireflyLightSRV,
			albedoSRV,
			normalSRV,
			depthSRV,
			outLightUAV,
			pointSampler,
			tileCameraBuffer.Get()
		);
	}

private:

	TiledDeferredRender tiledDeferredRender;

	std::mutex updateMutex;
	DeferredCameraBuffer lightCameraBufferData[Graphics::RENDER_BUFFER_COUNT];
	Graphics::BufferHandle lightCameraBufferHandle;

	TileCameraBuffer tileCameraBufferData[Graphics::RENDER_BUFFER_COUNT];
	ComPtr<ID3D11Buffer> tileCameraBuffer;

	//staticなことを前提に保持させる
	Graphics::DX11::BufferManager* bufferManager;
	uint8_t currentSlot = 0;

	Graphics::TextureHandle GBufferHandle[DeferredTextureCount];

	Graphics::TextureHandle LightBufferTexHandle;

public:
	STATIC_SERVICE_TAG
};

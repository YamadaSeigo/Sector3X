#pragma once

struct LightCameraBuffer {
	Math::Matrix4x4f invViewProj;
	Math::Vec3f camForward;
	float padding;
	Math::Vec3f camPos;
	float padding2;
};

class DeferredRenderingService : public ECS::IUpdateService
{
public:
	static inline constexpr const char* BUFFER_NAME = "DefferedCameraBuffer";

	DeferredRenderingService(
		Graphics::DX11::BufferManager* bufferManager,
		Graphics::DX11::TextureManager* textureManager,
		uint32_t w, uint32_t h)
		:bufferManager(bufferManager)
	{
		using namespace Graphics;

		DX11::BufferCreateDesc bufferDesc{};
		bufferDesc.name = BUFFER_NAME;
		bufferDesc.size = sizeof(LightCameraBuffer);
		bufferManager->Add(bufferDesc, lightCameraBufferHandle);

		DX11::TextureRecipe recipe;
		recipe.width = w;
		recipe.height = h;
		recipe.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		recipe.mipLevels = 1;
		recipe.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		recipe.usage = D3D11_USAGE_DEFAULT;
		recipe.arraySize = 1;

		for (int i = 0; i < DeferredTextureCount; ++i)
		{
			DX11::TextureCreateDesc texDesc;
			texDesc.recipe = &recipe;
			texDesc.path = ""; // 空パスで生成モード
			textureManager->Add(texDesc, GBufferHandle[i]);
		}
	}

	void Update(double delta) override
	{
		using namespace Graphics;

		currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;

		DX11::BufferUpdateDesc updateDesc;
		{
			auto bufData = bufferManager->Get(lightCameraBufferHandle);
			updateDesc.buffer = bufData.ref().buffer;
		}

		updateDesc.data = &lightCameraBufferData[currentSlot];
		updateDesc.size = sizeof(LightCameraBuffer);
		updateDesc.isDelete = false;

		bufferManager->UpdateBuffer(updateDesc, currentSlot);
	}

	void UpdateBufferData(LightCameraBuffer&& data)
	{
		std::lock_guard lock(updateMutex);

		lightCameraBufferData[currentSlot] = std::move(data);
	}

	const Graphics::TextureHandle* GetGBufferHandles() const noexcept
	{
		return GBufferHandle;
	}

private:

	std::mutex updateMutex;
	LightCameraBuffer lightCameraBufferData[Graphics::RENDER_BUFFER_COUNT];
	Graphics::BufferHandle lightCameraBufferHandle;

	//staticなことを前提に保持させる
	Graphics::DX11::BufferManager* bufferManager;
	uint8_t currentSlot = 0;

	Graphics::TextureHandle GBufferHandle[DeferredTextureCount];

public:
	STATIC_SERVICE_TAG
};

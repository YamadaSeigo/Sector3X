#pragma once

struct LightCameraBuffer {
	Math::Matrix4x4f invViewProj;
	Math::Vec3f camForward;
	float padding;
	Math::Vec3f camPos;
	float padding2;
};

class DefferedRenderingService : public ECS::IUpdateService
{
public:
	static inline constexpr const char* BUFFER_NAME = "DefferedCameraBuffer";

	DefferedRenderingService(Graphics::DX11::BufferManager* bufferManager)
		:bufferManager(bufferManager)
	{
		using namespace Graphics;

		DX11::BufferCreateDesc bufferDesc{};
		bufferDesc.name = BUFFER_NAME;
		bufferDesc.size = sizeof(LightCameraBuffer);
		bufferManager->Add(bufferDesc, lightCameraBufferHandle);
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

		//Systemで更新する前提でServiceのほうが更新が先なので一歩先のスロットを更新させる
		auto targetSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;

		lightCameraBufferData[targetSlot] = std::move(data);
	}

private:

	std::mutex updateMutex;
	LightCameraBuffer lightCameraBufferData[Graphics::RENDER_BUFFER_COUNT];
	Graphics::BufferHandle lightCameraBufferHandle;

	//staticなことを前提に保持させる
	Graphics::DX11::BufferManager* bufferManager;
	uint8_t currentSlot = 0;

public:
	STATIC_SERVICE_TAG
};

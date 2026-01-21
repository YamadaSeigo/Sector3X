#pragma once


struct CSpriteAnimation {

	Graphics::MaterialHandle hMat = {};

	struct alignas(4) Buffer {
		uint32_t divX = 1;
		uint32_t divY = 1;
		uint32_t frameX = 0;
		uint32_t frameY = 0;
	} buf = {};

	float frameTime = 0.0f;
	float duration = 0.1f;
	uint32_t layer = 0;
};

class SpriteAnimationService : public IUpdateService, public ICommitService{

public:
	static inline constexpr const char* BUFFER_NAME = "SpriteAnimationInstanceBuffer";

	static inline constexpr float MIN_FRAME_DURATION = 0.01f; //デフォルトのフレーム時間

	struct InstanceBuffer {
		CSpriteAnimation::Buffer buf;
		Graphics::InstanceIndex idx;
	};

	SpriteAnimationService(Graphics::DX11::BufferManager* bufferMgr)
		: bufferManager(bufferMgr) {
		Graphics::DX11::BufferCreateDesc desc{};
		desc.name = BUFFER_NAME;
		desc.size = sizeof(CSpriteAnimation::Buffer) * Graphics::MAX_INSTANCES_PER_FRAME;
		desc.usage = D3D11_USAGE_DYNAMIC;
		desc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.cpuAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.miscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.structureByteStride = sizeof(CSpriteAnimation::Buffer);
		bufferManager->Add(desc, instanceBufferHandle);
	}

	void PreUpdate(double deltaTime) override {

		m_deltaTime = static_cast<float>(deltaTime);

		currentSlot = (currentSlot + 1) % Graphics::RENDER_BUFFER_COUNT;

		//インスタンスカウントリセット
		instanceCounts.store(0, std::memory_order_relaxed);
	}

	void Commit(double deltaTime) override {

		auto instCount = instanceCounts.load(std::memory_order_relaxed);

		if (instCount == 0) {
			//更新不要
			return;
		}

		auto bufData = bufferManager->Get(instanceBufferHandle);

		// バッファ更新
		Graphics::DX11::BufferUpdateDesc updateDesc{};
		updateDesc.buffer = bufData.ref().buffer;
		updateDesc.data = cpuInstanceBuffers[currentSlot].data();
		updateDesc.size = instCount;
		updateDesc.isDelete = false;
		updateDesc.customUpdateFunc = [](void* dst, const void* src, size_t size) {

			CSpriteAnimation::Buffer* dstBuf = static_cast<CSpriteAnimation::Buffer*>(dst);
			const InstanceBuffer* srcBuf = static_cast<const InstanceBuffer*>(src);

			for (auto i = 0; i < size; ++i) {
				auto idx = srcBuf[i].idx.index;
				dstBuf[idx] = srcBuf[i].buf;
			}
			};

		bufferManager->UpdateBuffer(updateDesc, currentSlot);
	}

	void PushSpriteAnimationInstance(CSpriteAnimation& animation, Graphics::InstanceIndex idx) {

		auto& frameTime = animation.frameTime;
		frameTime += m_deltaTime;
		float duration = (std::max)(animation.duration, MIN_FRAME_DURATION);
		while (frameTime >= duration)
		{
			frameTime -= duration;

			auto& frameX = animation.buf.frameX;
			auto& frameY = animation.buf.frameY;
			auto divX = animation.buf.divX;
			auto divY = animation.buf.divY;
			// frame を 1 つ進める
			uint32_t index = frameY * divX + frameX;
			index = (index + 1) % (divX * divY);

			frameX = index % divX;
			frameY = index / divX;
		}

		auto i = instanceCounts.fetch_add(1, std::memory_order_relaxed);
		if (i >= Graphics::MAX_INSTANCES_PER_FRAME) {
			// 超過
			return;
		}

		auto& data = cpuInstanceBuffers[currentSlot][i];

		data.buf = animation.buf;
		data.idx = idx;
	}

	Graphics::BufferHandle GetInstanceBufferHandle() const noexcept {
		return instanceBufferHandle;
	}

private:
	float m_deltaTime = 0.0;
	uint32_t currentSlot = 0;
	Graphics::DX11::BufferManager* bufferManager;
	Graphics::BufferHandle instanceBufferHandle;

	//でかすぎのでヒープで保持
	std::vector<std::array<InstanceBuffer, Graphics::MAX_INSTANCES_PER_FRAME>> cpuInstanceBuffers = std::vector<std::array<InstanceBuffer, Graphics::MAX_INSTANCES_PER_FRAME>>(Graphics::RENDER_BUFFER_COUNT);
	std::atomic<uint32_t> instanceCounts = 0;
public:
	STATIC_SERVICE_TAG
};

#pragma once

#include <SectorFW/Core/ECS/ServiceContext.hpp>


//==============================================================
// 依存関係的によくないがコンパクトにするためにDX11BufferManager(バッファ更新)に依存
//==============================================================
class GrassMovementService : public SFW::ECS::IUpdateService
{
public:
	using BufferManager = SFW::Graphics::DX11::BufferManager;

    struct alignas(16) GrassWindCB
    {
        float    Time = 0.0f;
        float    WindSpeed = 1.0f;
        float    WindAmplitude = 0.2f;
        Math::Vec2f   WindDirXZ = { 1.0f, 0.3f };
        float    NoiseFreq = 0.05f;
        float    PhaseSpread = 3.14159f;
        float    BladeHeightLocal = 1.0f;  // ローカル空間でのブレード高さ
    };

    GrassMovementService(BufferManager* bufferMgr) : bufferMgr(bufferMgr)
    {
        Graphics::DX11::BufferCreateDesc cd;
		cd.name = "GrassWindCB";
		cd.size = sizeof(GrassWindCB);
		cd.initialData = &m_grassWindCB;

		bufferMgr->Add(cd, hBuffer);
    }

	void Update(double deltaTime) noexcept override
	{
		m_grassWindCB.Time += static_cast<float>(deltaTime);
	}

    /**
    * @brief バッファをGPUにおくる
    * @param slot 現在のCPU側のフレーム
    */
    void UpdateBufferToGPU(uint16_t slot) noexcept
    {
        Graphics::DX11::BufferUpdateDesc updDesc;
        auto data = bufferMgr->Get(hBuffer);
        updDesc.buffer = data.ref().buffer;
        updDesc.data = &m_grassWindCB;
        updDesc.isDelete = false;
        bufferMgr->UpdateBuffer(updDesc, slot);
    }

    const Graphics::BufferHandle GetBufferHandle() const noexcept {
        return hBuffer;
    }

private:
	GrassWindCB m_grassWindCB{};
    Graphics::BufferHandle hBuffer;
	BufferManager* bufferMgr = nullptr;

public:
    STATIC_SERVICE_TAG
};

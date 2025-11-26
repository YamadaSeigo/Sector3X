#pragma once

#include <SectorFW/Core/ECS/ServiceContext.hpp>


//==============================================================
// 依存関係的によくないがコンパクトにするためにDX11BufferManager(バッファ更新)に依存
//==============================================================
class GrassMovementService : public SFW::ECS::IUpdateService
{
public:
	using BufferManager = SFW::Graphics::DX11::BufferManager;

    struct GrassWindCB
    {
		float    Time = 0.0f;                       // 経過時間
		float    NoiseFreq = 0.05f;                 // ノイズ周波数
		float    PhaseSpread = 3.14159f;            // ブレードごとの位相の広がり
        float    BladeHeightLocal = 1.0f;           // ローカル空間でのブレード高さ
		float    WindSpeed = 1.0f;                  // 風速
		float    WindAmplitude = 10.0f;              // 風の振幅
		Math::Vec2f   WindDirXZ = { 1.0f, 0.3f };   // 風向き(XZ平面)
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
        // 生の経過時間（デバッグ用にも便利）
        m_rawTime += deltaTime;

        // グルーブ係数（0.5～1.5くらいの間をふらふらする）を計算
        const float t = static_cast<float>(m_rawTime);

        // 2つの周波数の違う sin を足して、ちょっと複雑な揺れにする
        float w1 = std::sin(t * 0.25f);         // ゆっくり変化
        float w2 = std::sin(t * 0.07f + 1.3f);  // さらにゆっくり・ずらした位相

        // w1,w2 は -1..1 → 足して -2..2 → 0..1 に正規化 → 最後に 0.5..1.5 に
        float blend = (w1 + w2) * 0.25f + 0.5f;   // 0.0..1.0 くらい
        float grooveMul = 0.5f + blend;               // 0.5..1.5 くらい

        // グルーブ付きの位相時間を積分
        m_phaseTime += deltaTime * static_cast<double>(grooveMul);

        // シェーダーに渡す Time は、この「グルーブ済み時間」
        m_grassWindCB.Time = static_cast<float>(m_phaseTime);
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
		updDesc.size = sizeof(GrassWindCB);
        updDesc.data = &m_grassWindCB;
        updDesc.isDelete = false;
        bufferMgr->UpdateBuffer(updDesc, slot);
    }

    const Graphics::BufferHandle GetBufferHandle() const noexcept {
        return hBuffer;
    }

private:

    // 内部状態
    double m_rawTime = 0.0;  // 単純な経過時間
    double m_phaseTime = 0.0;  // グルーブした「位相用の時間」

	GrassWindCB m_grassWindCB{};
    Graphics::BufferHandle hBuffer;
	BufferManager* bufferMgr = nullptr;

public:
    STATIC_SERVICE_TAG
};

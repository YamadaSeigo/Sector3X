#pragma once

#include <SectorFW/Core/ECS/ServiceContext.hpp>


//==============================================================
// 依存関係的によくないがコンパクトにするためにDX11BufferManager(バッファ更新)に依存
//==============================================================
class WindMovementService : public SFW::ECS::IUpdateService
{
public:
	using BufferManager = SFW::Graphics::DX11::BufferManager;

    struct WindCB
    {
		float    Time = 0.0f;                       // 経過時間
		float    NoiseFreq = 0.05f;                 // ノイズ周波数
		float    PhaseSpread = 3.14159f;            // ブレードごとの位相の広がり
        float    BigWaveWeight = 0.3f;              // おおきな波(全体)の重み
		float    WindSpeed = 1.0f;                  // 風速
		float    WindAmplitude = 1.0f;              // 風の振幅
		Math::Vec2f   WindDirXZ = { 1.0f, 0.3f };   // 風向き(XZ平面)
    };

    WindMovementService(BufferManager* bufferMgr) : bufferMgr(bufferMgr)
    {
        Graphics::DX11::BufferCreateDesc cd;
		cd.name = "GrassWindCB";
		cd.size = sizeof(WindCB);
		cd.initialData = &m_grassWindCB;

		bufferMgr->Add(cd, hBuffer);

		// デバッグUI登録
        BIND_DEBUG_SLIDER_FLOAT("Wind", "BigWaveWeight", &m_grassWindCB.BigWaveWeight, 0.0f, 1.0f, 0.01f);
        BIND_DEBUG_SLIDER_FLOAT("Wind", "Amplitude", &m_grassWindCB.WindAmplitude, 0.0f, 100.0f, 0.1f);
        BIND_DEBUG_SLIDER_FLOAT("Wind", "DirectionX", &m_grassWindCB.WindDirXZ.x, -1.0f, 1.0f, 0.01f);
        BIND_DEBUG_SLIDER_FLOAT("Wind", "DirectionZ", &m_grassWindCB.WindDirXZ.y, -1.0f, 1.0f, 0.01f);
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
		updDesc.size = sizeof(WindCB);
        updDesc.data = &m_grassWindCB;
        updDesc.isDelete = false;
        bufferMgr->UpdateBuffer(updDesc, slot);
    }

    const Graphics::BufferHandle GetBufferHandle() const noexcept {
        return hBuffer;
    }

    static std::vector<float> ComputeGrassWeight(const std::vector<Math::Vec3f>& vertices)
    {
		float minY = +FLT_MAX;
		float maxY = -FLT_MAX;
        for (const auto v : vertices) {
            minY = (std::min)(minY, v.y);
            maxY = (std::max)(maxY, v.y);
        }
		float height = (std::max)(0.0001f, maxY - minY);
		std::vector<float> weights;
		weights.reserve(vertices.size());
		for (const auto v : vertices) {
			float t = (v.y - minY) / height; // 0..1 高くなるほど大きく
			float w = std::pow(t, 2.0f); // 高さに応じて二次曲線的に増加.しなやかにカーブ
			weights.push_back(w);
		}
		return weights;
    }

    static std::vector<float> ComputeTreeWeight(const std::vector<Math::Vec3f>& vertices)
    {
        // 最大と最小の座標
        float minY = +FLT_MAX;
        float maxY = -FLT_MAX;
        for (const auto v : vertices) {
            minY = (std::min)(minY, v.y);
            maxY = (std::max)(maxY, v.y);
        }
        float height = (std::max)(0.0001f, maxY - minY);

        // 簡易的に「幹の軸 = 上方向」と仮定
        Math::Vec3f trunkAxis = Math::Vec3f(0, 1, 0);

        // 幹の中心線からの最大半径を測る
        float maxRadius = 0.0f;
        for (auto v : vertices) {
            float t = (v.y - minY) / height;    // 軸方向の正規化位置
            Math::Vec3f proj = Math::Vec3f(0, minY + t * height, 0); // 超雑な投影（0,y,0）
            float r = (v - proj).length();
            maxRadius = (std::max)(maxRadius, r);
        }
        maxRadius = (std::max)(maxRadius, 0.0001f);

        std::vector<float> weights;
        weights.reserve(vertices.size());
        for (auto v : vertices) {
            float t = (v.y - minY) / height; // 0..1 高くなるほど大きく
            Math::Vec3f proj = Math::Vec3f(0, minY + t * height, 0);
            float r = (v - proj).length() / maxRadius; // 0..1 幹から遠いほど大きく

            // 高さと半径をミックス
            float w = std::pow(t, 2.0f) * 0.2f + std::pow(r, 2.0f) * 0.8f;
            w = std::clamp(w, 0.0f, 1.0f);
            weights.push_back(w);
        }
        return weights;
    }

private:

    // 内部状態
    double m_rawTime = 0.0;  // 単純な経過時間
    double m_phaseTime = 0.0;  // グルーブした「位相用の時間」

	WindCB m_grassWindCB{};
    Graphics::BufferHandle hBuffer;
	BufferManager* bufferMgr = nullptr;

public:
    STATIC_SERVICE_TAG
};

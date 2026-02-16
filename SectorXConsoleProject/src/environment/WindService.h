#pragma once

#include <SectorFW/Core/ECS/ServiceContext.hpp>


//==============================================================
// 依存関係的によくないがコンパクトにするためにDX11BufferManager(バッファ更新)に依存
//==============================================================
class WindService : public SFW::ECS::IUpdateService
{
public:
	using BufferManager = SFW::Graphics::DX11::BufferManager;

    struct WindCB
    {
		float    Time = 0.0f;                       // 経過時間
		float    NoiseFreq = 0.05f;                 // ノイズ周波数
        float    BigWaveWeight = 0.3f;              // おおきな波(全体)の重み
		float    WindSpeed = 1.0f;                  // 風速
		float    WindAmplitude = 1.6f;              // 風の振幅
		Math::Vec3f   WindDir = { 1.0f, 0.0f, 0.3f };   // 風向き(XZ平面)
    };

    WindService(BufferManager* bufferMgr) : bufferMgr(bufferMgr)
    {
        Graphics::DX11::BufferCreateDesc cd;
		cd.name = "GrassWindCB";
		cd.size = sizeof(WindCB);
		cd.initialData = &m_grassWindCB;

		bufferMgr->Add(cd, hBuffer);

		// デバッグUI登録
        BIND_DEBUG_SLIDER_FLOAT("Wind", "BigWaveWeight", &m_grassWindCB.BigWaveWeight, 0.0f, 1.0f, 0.01f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "NoiseFreq", &m_grassWindCB.NoiseFreq, 0.0f, 1.0f, 0.001f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "BaseSpeed", &m_baseWindSpeed, 0.0f, 20.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "TimeSpeed", &m_windTimeSpeed, 0.0f, 100.0f, 0.01f);
        BIND_DEBUG_SLIDER_FLOAT("Wind", "Amplitude", &m_grassWindCB.WindAmplitude, 0.0f, 100.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "EnableAutoWind", &m_autoWindEnable, 0.0f, 1.0f, 1.0f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "DirDriftSpeed", &m_dirDriftSpeed, 0.01f, 10.0f, 0.01f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "BaseSpeedMulMin", &m_baseSpeedMulMin, 0.01f, 10.0f, 0.01f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "BaseSpeedMulMax", &m_baseSpeedMulMax, 0.01f, 10.0f, 0.01f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "DirTimeScaleBase", &m_dirTimeScaleBase, 0.01f, 10.0f, 0.01f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "SpeedTimeScale", &m_speedTimeScale, 0.01f, 1.0f, 0.001f);
		BIND_DEBUG_SLIDER_FLOAT("Wind", "GustTimeScale", &m_gustTimeScale, 0.01f, 1.0f, 0.001f);

    }

	void PreUpdate(double deltaTime) noexcept override
	{
		// windSpeedを反映させて風の時間変化を進める
		float mul = powf(m_grassWindCB.WindSpeed * 0.8f, 2.0f) * m_windTimeSpeed;

        m_grassWindCB.Time += static_cast<float>(deltaTime) * mul;

		// 自動風が有効なら、風向き/風速を「それっぽく」時間変化させる
		UpdateWind(deltaTime);
	}

    // 風向き/風速を「それっぽく」時間変化させる
    void UpdateWind(double deltaTime) noexcept
    {
		// -------------------------
		// 小さな 1D 連続ノイズ（value noise + fBm）
		// -------------------------
		auto HashU32 = [](uint32_t x) -> uint32_t {
			x ^= 61u; x ^= x >> 16;
			x *= 9u;
			x ^= x >> 4;
			x *= 0x27d4eb2du;
			x ^= x >> 15;
			return x;
			};

		auto Hash01 = [&](uint32_t x) -> float {
			const uint32_t h = HashU32(x);
			return (h & 0x00FFFFFFu) / float(0x01000000u); // 0..1
			};

		auto Smooth01 = [](float t) -> float {
			t = std::clamp(t, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
			};

		auto Lerp = [](float a, float b, float t) -> float {
			return a + (b - a) * t;
			};

		auto Noise1D01 = [&](float x, uint32_t seed) -> float {
			const float xf = std::floor(x);
			const int i0 = (int)xf;
			const int i1 = i0 + 1;
			const float f = x - xf;
			const float u = Smooth01(f);

			const float a = Hash01(uint32_t(i0) + seed * 1013u);
			const float b = Hash01(uint32_t(i1) + seed * 1013u);
			return Lerp(a, b, u); // 0..1
			};

		auto FBm1D01 = [&](float x, uint32_t seed) -> float {
			float sum = 0.0f;
			float amp = 0.5f;
			float freq = 1.0f;
			float norm = 0.0f;
			for (int o = 0; o < 4; ++o) {
				sum += Noise1D01(x * freq, seed + uint32_t(o) * 97u) * amp;
				norm += amp;
				amp *= 0.5f;
				freq *= 2.0f;
			}
			return (norm > 0.0f) ? (sum / norm) : 0.0f; // 0..1
			};

		auto Saturate = [](float v) -> float { return std::clamp(v, 0.0f, 1.0f); };

		// -------------------------
		// 時間
		// -------------------------
		m_windTimeSec += deltaTime;
		const float T = static_cast<float>(m_windTimeSec);

		// -------------------------
		// “自然っぽい”風速：普段は弱め + ときどき突風
		// -------------------------
		const float baseN = FBm1D01(T * m_speedTimeScale, 17u); // 0..1 (ゆっくり)
		const float gustN = FBm1D01(T * m_gustTimeScale, 91u); // 0..1 (速め)

		// 突風は正側だけ強調（たまに来る感じ）
		const float gust = (std::max)(0.0f, gustN - 0.5f) * 2.0f; // 0..1

		float strength01 = 0.25f + baseN * 0.55f + gust * 0.85f;
		strength01 = Saturate(strength01);

		// -------------------------
		// 風向き：強いほど方向が安定（現実っぽい）
		// -------------------------
		const float dirTimeScale = m_dirTimeScaleBase * (1.35f - strength01);
		const float dirN = FBm1D01(T * dirTimeScale, 123u); // 0..1

		const float TWO_PI = 6.28318530718f;
		const float angle = dirN * TWO_PI;

		Math::Vec3f autoDir = { std::cos(angle), 0.0f, std::sin(angle) };

		// -------------------------
		// 既存の UI 値（m_grassWindCB.WindDir / WindSpeed）を“ベース”として尊重しつつ、
		// 自動風をブレンドして上乗せする
		// -------------------------
		auto NormalizeXZ = [](Math::Vec3f v) -> Math::Vec3f {
			v.y = 0.0f;
			const float len = std::sqrt(v.x * v.x + v.z * v.z);
			if (len > 1e-6f) { v.x /= len; v.z /= len; }
			else { v = { 1.0f, 0.0f, 0.0f }; }
			return v;
			};

		Math::Vec3f baseDir = NormalizeXZ(m_grassWindCB.WindDir);
		autoDir = NormalizeXZ(autoDir);

		// ブレンド量（0で手動、1で自動）
		const float a = Saturate(m_autoWindEnable);

		// “いきなり方向が飛ばない”ように、現在方向をスムーズに追従させる
		Math::Vec3f targetDir = NormalizeXZ({
			Lerp(baseDir.x, autoDir.x, a),
			0.0f,
			Lerp(baseDir.z, autoDir.z, a)
			});

		// 現在のWindDirを状態として使って滑らかに
		Math::Vec3f curDir = NormalizeXZ(m_grassWindCB.WindDir);
		const float follow = 1.0f - std::exp(-m_dirDriftSpeed * static_cast<float>(deltaTime));
		Math::Vec3f newDir = NormalizeXZ({
			Lerp(curDir.x, targetDir.x, follow),
			0.0f,
			Lerp(curDir.z, targetDir.z, follow)
			});

		// 風速は「ベース値 * 倍率」で自然に上下
		const float speedMul = Lerp(m_baseSpeedMulMin, m_baseSpeedMulMax, strength01);
		const float newSpeed = (std::max)(0.0f, m_baseWindSpeed) * Lerp(1.0f, speedMul, a);

		// 反映
		m_grassWindCB.WindDir = newDir;
		m_grassWindCB.WindSpeed = newSpeed;
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

	void GetWindDirAndSpeed(Math::Vec3f& outDir, float& outSpeed) const noexcept
	{
		outDir = m_grassWindCB.WindDir;
		outSpeed = m_grassWindCB.WindSpeed;
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
            float w = std::pow(t, 2.0f) * 0.1f + std::pow(r, 3.0f) * 1.25f;
            w = std::clamp(w, 0.0f, 1.0f);
            weights.push_back(w);
        }
        return weights;
    }

    static std::vector<float> ComputeGhostWeight(const std::vector<Math::Vec3f>& vertices)
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
            float t = (v.y - minY) / height; // 1..0 高くなるほど小さく
            float w = std::pow(t, 2.0f); // 高さに応じて二次曲線的に増加.しなやかにカーブ
            weights.push_back(w);
        }
        return weights;
    }

private:

    // ---- Auto wind params/state ----
    double m_windTimeSec = 0.0;

	float m_baseWindSpeed = 1.0f;      // ベースの風速（UIで直接いじる用）
	float m_windTimeSpeed = 1.0f;      // 風の時間変化の速さ（全体の時間スケール）

    float m_autoWindEnable = 1.0f;     // 0..1（1で自動風フル）
    float m_dirDriftSpeed = 0.35f;     // 方向の追従（大きいほどスムーズに追う）
    float m_baseSpeedMulMin = 0.60f;   // 風速の下限倍率（WindSpeedに掛ける）
    float m_baseSpeedMulMax = 3.00f;   // 風速の上限倍率
    float m_dirTimeScaleBase = 0.010f; // 方向変化の遅さ（小さいほどゆっくり）
    float m_speedTimeScale = 0.020f;   // 風速の大域変化
    float m_gustTimeScale = 0.040f;   // 突風の変化


	WindCB m_grassWindCB{};
    Graphics::BufferHandle hBuffer;
	BufferManager* bufferMgr = nullptr;

public:
    STATIC_SERVICE_TAG
};

// BiomeScatterGenerator.h
#pragma once
#include <cstdint>
#include <vector>
#include <span>
#include <cmath>
#include <algorithm>

#include <SectorFW/Math/sx_math.h>

struct Rgba8 { uint8_t r = 0, g = 0, b = 0, a = 0; };

/// 外部の配列を「画像」として扱うための軽いビュー
template<class T>
struct Image2D
{
    const T* data = nullptr;
    int w = 0;
    int h = 0;
    int stride = 0; // 0なら w と同じ

    const T& at(int x, int y) const
    {
        if (!data || w <= 0 || h <= 0) { static T dummy{}; return dummy; }
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        const int s = (stride > 0) ? stride : w;
        return data[y * s + x];
    }
};

/// サンプル結果（0..1）
struct GroundSample
{
    float grass = 0; // R
    float dirt = 0; // G
    float rock = 0; // B
    float snow = 0; // A
};

/// モデルハンドルはエンジン側の型に置き換えてOK
using ModelHandle = uint32_t;

/// 生成されるインスタンス（あなたが言ってた項目）
struct ScatterInstance
{
    ModelHandle     model = 0;
    Math::Vec3f     offsetWS{}; // world-space
    float           yaw = 0.0f;    // radians
    float           uniformScale = 1.0f;
    Math::Vec3f     tint = { 1,1,1 };
    float           roughness = 0.5f;
};

/// バイオーム内の「枝分かれグループ」(木/低木/岩小物…など)
struct BranchGroup
{
    // このグループが“候補点1つ”に対して出現を試みる確率（0..1）
    float spawnProbability = 0.25f;

    // 候補点が受理されたときのスケール範囲
    float scaleMin = 0.8f;
    float scaleMax = 1.2f;

    // roughness 範囲（個体差）
    float roughnessMin = 0.35f;
    float roughnessMax = 0.8f;

    // tint の揺らぎ（±）
    Math::Vec3f tintJitter = { 0.05f, 0.05f, 0.05f };

    // 地表との関係（簡易）
    // grass/dirt/rock/snow を重みに掛ける係数（0..2くらい想定）
    float wGrass = 0.0f;
    float wDirt = 0.0f;
    float wRock = 0.0f;
    float wSnow = 0.0f;

    // 湿り気による重み（0..2くらい）
    float wWetness = 1.0f;

    // 斜面制限（度数法で指定）
    float maxSlopeDeg = 35.0f;

    // モデル候補と重み
    struct ModelChoice { ModelHandle handle; float weight; };
    std::vector<ModelChoice> models;
};

/// バイオームごとのパラメータ
struct BiomeParams
{
    uint16_t biomeId = 0;
    float    baseDensityPerSquareMeter = 0.03f; // “候補点密度”の目安
    std::vector<BranchGroup> branches;
};

/// ワールド上の各マップの解釈
struct ScatterInputs
{
    // ワールドサイズ（X,Z）
    Math::Vec2f worldSize = { 4096.0f, 4096.0f };

    // これらは “ワールド全体” をカバーする0..1想定のCPU配列
    Image2D<Rgba8> groundRgba;   // RGBA=草/土/岩/雪
    Image2D<uint8_t> wetness;    // 0..255
    Image2D<uint8_t> noVegetation; // 0..255
    Image2D<uint16_t> biomeId;   // 2DバイオームID

    // 高さ・法線・傾斜はプロジェクト次第なのでコールバックで渡すのが一番強い
    // ここでは「高さ」と「傾斜（度）」を関数で問い合わせる前提
    float (*sampleHeight)(float x, float z, void* user) = nullptr;
    float (*sampleSlopeDeg)(float x, float z, void* user) = nullptr;
    void* heightUser = nullptr;

    // 乱数のベースシード（再現性）
    uint32_t globalSeed = 1;
};

struct ScatterConfig
{
    // 候補点生成のセルサイズ（m）。小さいほど密度が細かく調整できるが重い
    float candidateCellSize = 4.0f;

    // noVegetationがこの値以上なら棄却（0..1）
    float noVegRejectThreshold = 0.5f;

    // Wetness (0..1) をそのまま枝の重みに掛けるための曲線係数
    // 1.0=そのまま、>1で強調、<1で弱め
    float wetnessPower = 1.0f;

    // 生成上限（暴走防止）
    uint32_t maxInstances = 2'000'000;
};

class BiomeScatterGenerator
{
public:
    void SetInputs(const ScatterInputs& in) { m_in = in; }
    void SetConfig(const ScatterConfig& cfg) { m_cfg = cfg; }
    void SetBiomes(std::span<const BiomeParams> biomes);

    // ワールド全域の生成
    [[nodiscard]] std::vector<ScatterInstance> GenerateAll() const;

private:
    ScatterInputs m_in{};
    ScatterConfig m_cfg{};
    std::vector<BiomeParams> m_biomes;

    const BiomeParams* FindBiome(uint16_t id) const;

    // サンプリング
    uint16_t SampleBiomeId(float x, float z) const;
    GroundSample SampleGround(float x, float z) const;
    float SampleWetness01(float x, float z) const;
    float SampleNoVeg01(float x, float z) const;

    // RNG
    static uint32_t HashU32(uint32_t a);
    static uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed);
    static float    U01(uint32_t& state);
    static float    URange(uint32_t& state, float a, float b);

    // 抽選
    static int WeightedPickIndex(std::span<const BranchGroup::ModelChoice> items, uint32_t& rng);

    // 候補点生成（グリッド + ジッター）
    void GenerateCandidates(std::vector<Math::Vec2f>& out) const;
};

// BiomeScatterGenerator.cpp
#include "BiomeScatterGenerator.h"

void BiomeScatterGenerator::SetBiomes(std::span<const BiomeParams> biomes)
{
    m_biomes.assign(biomes.begin(), biomes.end());
}

const BiomeParams* BiomeScatterGenerator::FindBiome(uint16_t id) const
{
    for (auto& b : m_biomes) if (b.biomeId == id) return &b;
    return nullptr;
}

static inline int WorldToTexel(float x, float worldSize, int texSize)
{
    if (texSize <= 1) return 0;
    const float u = Math::saturate(x / worldSize);
    return (int)std::floor(u * (texSize - 1) + 0.5f);
}

uint16_t BiomeScatterGenerator::SampleBiomeId(float x, float z) const
{
    const int tx = WorldToTexel(x, m_in.worldSize.x, m_in.biomeId.w);
    const int ty = WorldToTexel(z, m_in.worldSize.y, m_in.biomeId.h);
    return m_in.biomeId.at(tx, ty);
}

GroundSample BiomeScatterGenerator::SampleGround(float x, float z) const
{
    const int tx = WorldToTexel(x, m_in.worldSize.x, m_in.groundRgba.w);
    const int ty = WorldToTexel(z, m_in.worldSize.y, m_in.groundRgba.h);
    const Rgba8 c = m_in.groundRgba.at(tx, ty);

    GroundSample g;
    g.grass = c.r / 255.0f;
    g.dirt = c.g / 255.0f;
    g.rock = c.b / 255.0f;
    g.snow = c.a / 255.0f;

    // たまに合計が1じゃないことがあるので正規化（任意）
    const float sum = g.grass + g.dirt + g.rock + g.snow;
    if (sum > 1e-6f) {
        g.grass /= sum; g.dirt /= sum; g.rock /= sum; g.snow /= sum;
    }
    return g;
}

float BiomeScatterGenerator::SampleWetness01(float x, float z) const
{
    const int tx = WorldToTexel(x, m_in.worldSize.x, m_in.wetness.w);
    const int ty = WorldToTexel(z, m_in.worldSize.y, m_in.wetness.h);
    return m_in.wetness.at(tx, ty) / 255.0f;
}

float BiomeScatterGenerator::SampleNoVeg01(float x, float z) const
{
    const int tx = WorldToTexel(x, m_in.worldSize.x, m_in.noVegetation.w);
    const int ty = WorldToTexel(z, m_in.worldSize.y, m_in.noVegetation.h);
    return m_in.noVegetation.at(tx, ty) / 255.0f;
}

// ---- RNG helpers (fast, deterministic) ----
uint32_t BiomeScatterGenerator::HashU32(uint32_t a)
{
    // xorshift-ish mix
    a ^= a >> 16;
    a *= 0x7feb352dU;
    a ^= a >> 15;
    a *= 0x846ca68bU;
    a ^= a >> 16;
    return a;
}

uint32_t BiomeScatterGenerator::Hash2D(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = seed;
    h ^= HashU32(x + 0x9e3779b9U);
    h ^= HashU32(y + 0x85ebca6bU);
    return HashU32(h);
}

float BiomeScatterGenerator::U01(uint32_t& state)
{
    state = HashU32(state);
    // 24-bit mantissa
    return (state & 0x00FFFFFFu) / 16777216.0f;
}

float BiomeScatterGenerator::URange(uint32_t& state, float a, float b)
{
    return Math::lerp(a, b, U01(state));
}

int BiomeScatterGenerator::WeightedPickIndex(std::span<const BranchGroup::ModelChoice> items, uint32_t& rng)
{
    float sum = 0.0f;
    for (auto& it : items) sum += (std::max)(0.0f, it.weight);
    if (sum <= 1e-8f) return -1;

    float r = U01(rng) * sum;
    float acc = 0.0f;
    for (int i = 0; i < (int)items.size(); ++i) {
        acc += (std::max)(0.0f, items[i].weight);
        if (r <= acc) return i;
    }
    return (int)items.size() - 1;
}

// 候補点生成：ワールドをセルで走査してセルごとに1点（ジッター）
void BiomeScatterGenerator::GenerateCandidates(std::vector<Math::Vec2f>& out) const
{
    const float cell = (std::max)(0.25f, m_cfg.candidateCellSize);
    const int nx = (int)std::ceil(m_in.worldSize.x / cell);
    const int nz = (int)std::ceil(m_in.worldSize.y / cell);

    out.clear();
    out.reserve((size_t)nx * (size_t)nz);

    for (int z = 0; z < nz; ++z) {
        for (int x = 0; x < nx; ++x) {
            uint32_t rng = Hash2D((uint32_t)x, (uint32_t)z, m_in.globalSeed);

            // セル内ジッター（均一）
            const float jx = URange(rng, 0.0f, 1.0f);
            const float jz = URange(rng, 0.0f, 1.0f);

            Math::Vec2f p;
            p.x = (x + jx) * cell;
            p.y = (z + jz) * cell;

            // 念のためクランプ
            if (p.x < 0 || p.y < 0 || p.x > m_in.worldSize.x || p.y > m_in.worldSize.y)
                continue;

            out.push_back(p);
        }
    }
}

[[nodiscard]] std::vector<ScatterInstance> BiomeScatterGenerator::GenerateAll() const
{
    std::vector<Math::Vec2f> candidates;
    GenerateCandidates(candidates);

    std::vector<ScatterInstance> out;
    out.reserve(std::min<uint32_t>((uint32_t)candidates.size(), m_cfg.maxInstances));

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        const float x = candidates[i].x;
        const float z = candidates[i].y;

        // no-vegetation mask
        if (SampleNoVeg01(x, z) >= m_cfg.noVegRejectThreshold) continue;

        // biome lookup
        const uint16_t biomeId = SampleBiomeId(x, z);
        const BiomeParams* biome = FindBiome(biomeId);
        if (!biome) continue;

        // density gate（バイオームの密度に応じて候補点を間引く）
        // 目安: baseDensityPerSquareMeter × (cellArea) が 1候補あたりの採用確率
        const float cellArea = m_cfg.candidateCellSize * m_cfg.candidateCellSize;
        float acceptP = Math::saturate(biome->baseDensityPerSquareMeter * cellArea);

        uint32_t rngCell = Hash2D((uint32_t)(x / m_cfg.candidateCellSize),
            (uint32_t)(z / m_cfg.candidateCellSize),
            m_in.globalSeed ^ (uint32_t)biomeId);
        if (U01(rngCell) > acceptP) continue;

        // samples
        const GroundSample g = SampleGround(x, z);
        float wet = SampleWetness01(x, z);
        wet = std::pow(Math::saturate(wet), (std::max)(0.001f, m_cfg.wetnessPower));

        float slopeDeg = 0.0f;
        float y = 0.0f;
        if (m_in.sampleSlopeDeg) slopeDeg = m_in.sampleSlopeDeg(x, z, m_in.heightUser);
        if (m_in.sampleHeight)   y = m_in.sampleHeight(x, z, m_in.heightUser);

        // branch loop（「枝分かれ」）
        for (const BranchGroup& br : biome->branches)
        {
            if (out.size() >= m_cfg.maxInstances) return out;
            if (br.models.empty()) continue;

            // slope reject
            if (slopeDeg > br.maxSlopeDeg) continue;

            // branch spawn gate
            uint32_t rng = HashU32(rngCell ^ HashU32((uint32_t)(&br - biome->branches.data())));
            if (U01(rng) > Math::saturate(br.spawnProbability)) continue;

            // 地表＆湿り気で重みを作る（“枝分け”の中のモデル選択に反映）
            // ここは好みで「枝自体の採否」にも使える
            const float surfaceW =
                g.grass * br.wGrass +
                g.dirt * br.wDirt +
                g.rock * br.wRock +
                g.snow * br.wSnow;

            const float finalW = Math::saturate(surfaceW) * Math::saturate(br.wWetness * wet + (1.0f - br.wWetness));

            // finalWが低いなら棄却（地表が合わない）
            if (finalW < 0.15f) continue;
            // 受理率として使う
            if (U01(rng) > finalW) continue;

            // model pick (weighted)
            int pick = WeightedPickIndex(br.models, rng);
            if (pick < 0) continue;

            ScatterInstance inst;
            inst.model = br.models[(size_t)pick].handle;
            inst.positionWS = { x, y, z };
            inst.yaw = URange(rng, 0.0f, 6.28318530718f);
            inst.uniformScale = URange(rng, br.scaleMin, br.scaleMax);

            // tint base: 地表に少し寄せる例（任意）
            Math::Vec3f baseTint = { 1,1,1 };
            baseTint.x = Math::lerp(1.0f, 0.90f, g.snow); // 雪多いと少し青白く
            baseTint.y = Math::lerp(1.0f, 0.95f, g.rock);
            baseTint.z = Math::lerp(1.0f, 0.92f, g.dirt);

            inst.tint = baseTint;
            inst.tint.x = Math::saturate(inst.tint.x + URange(rng, -br.tintJitter.x, br.tintJitter.x));
            inst.tint.y = Math::saturate(inst.tint.y + URange(rng, -br.tintJitter.y, br.tintJitter.y));
            inst.tint.z = Math::saturate(inst.tint.z + URange(rng, -br.tintJitter.z, br.tintJitter.z));

            inst.roughness = URange(rng, br.roughnessMin, br.roughnessMax);
            // 湿り気が高いと粗さ↑みたいな補正（任意）
            inst.roughness = Math::saturate(Math::lerp(inst.roughness, 1.0f, wet * 0.25f));

            out.push_back(inst);
        }
    }

    return out;
}

#pragma once

#include "../app/LeafService.h"
#include "../app/PlayerService.h"


struct CLeafVolume
{
    // 空間（最低限）
    Math::Vec3f centerWS = {};
    float       hitRadius = 20.0f;      // 球ボリューム（まずは球が楽）
    float       spawnRadius = 30.0f;    // 発生範囲

    // 見た目（emissive / bloom 基準）
    Math::Vec3f color = { 0.4f, 1.5f, 0.0f };
    float       emissiveIntensity = 1.0f;

    // 群れ密度（GPU targetCount の元）
    uint32_t    maxCountNear = 10000;    // 近距離での最大個体数

    // 動き（UpdateCSで使う）
    float       speed = 0.1f;
    float       noiseScale = 0.25f;

    // LOD距離（CPUがactive判定・targetCount計算）
    float       nearDistance = 0.1f;    // ここより近い：maxCountNear
    float       farDistance = 20.0f;     // ここより遠い：inactive（0）

    // ライト化予算（近距離だけN個ライト化する）
    uint32_t    nearLightBudget = 8;

    uint32_t seed = 0;

    float burstT = 0.0f; // 0..1（1=発動直後、時間で0へ）

    bool        isHit = false;

    // 例：index 20bit + generation 12bit = 32bit
    uint32_t MakeUID(uint32_t index, uint32_t gen)
    {
        return (gen << 20) | (index & ((1u << 20) - 1));
    }
};

template<typename Partition>
class LeafSystem : public ITypeSystem<
    LeafSystem,
    Partition,
    //アクセスするコンポーネントの指定
    ComponentAccess<
        Read<CLeafVolume>
    >,
    //受け取るサービスの指定
    ServiceContext<
        LeafService,
        PlayerService,
        TimerService
    >>
{
    using Accessor = ComponentAccessor<Read<CLeafVolume>>;
public:

    //指定したサービスを関数の引数として受け取る
    void UpdateImpl(Partition& partition,
        NoDeletePtr<LeafService> leafService,
        NoDeletePtr<PlayerService> playerService,
        NoDeletePtr<TimerService> timerService) {

        auto playerPos = playerService->GetPlayerPosition();

        //プレイヤーの位置をfireflyServiceにも教える
        leafService->SetPlayerPos(playerPos);

        constexpr float chunkRadius = 100.0f;

        auto spatialChunks = partition.CullChunks(playerPos, chunkRadius);

        Query query;
        query.With<CLeafVolume>();

        std::vector<ArchetypeChunk*> archetypeChunks = query.MatchingChunks<std::vector<SFW::SpatialChunk*>&>(spatialChunks);

        for (auto& chunk : archetypeChunks) {
            Accessor accessor = Accessor(chunk);
            size_t entityCount = chunk->GetEntityCount();
            const auto& entities = chunk->GetEntityIDs();

            auto leafVolume = accessor.Get<Read<CLeafVolume>>();
            if (!leafVolume.has_value()) [[unlikely]] {
                return;
            }

            for (auto i = 0; i < entityCount; ++i) {
                auto volume = leafVolume.value()[i];

                //範囲外の場合はスキップ
                auto length = (volume.centerWS - playerPos).lengthSquared();

                if (length > volume.hitRadius * volume.hitRadius) {
                    volume.isHit = false;
                    continue;
                }

                LeafVolumeGPU gpuVolume{};
                gpuVolume.centerWS = volume.centerWS;
                gpuVolume.radius = volume.spawnRadius;
                gpuVolume.color = volume.color;
                gpuVolume.intensity = volume.emissiveIntensity;
                gpuVolume.targetCount = static_cast<float>(volume.maxCountNear);
                gpuVolume.speed = volume.speed;
                gpuVolume.noiseScale = volume.noiseScale;
                gpuVolume.nearLightBudget = volume.nearLightBudget;
                gpuVolume.seed = volume.seed;

                if (volume.isHit == false) {
                    volume.burstT = 1.0f; // 発動直後
                    volume.isHit = true;
                }

                gpuVolume.burstT = volume.burstT;

                volume.burstT = (std::max)(0.0f, volume.burstT - timerService->GetDeltaTime() / 4.0f); //4秒継続

                leafService->PushActiveVolume(entities[i].index, gpuVolume);
            }
        }
    }
};

#pragma once

#include "environment/LeafService.h"
#include "app/PlayerService.h"


struct CLeafVolume
{
    // 空間（最低限）
    Math::Vec3f centerWS = {};
    float       radius = 30.0f;    // 発生範囲

    float orbitR = 2.0f;     // 回転半径
    float orbitW = 0.9f;     // 回転速度(rad/sでもOK)
	float k = 5.0f; // 追従の強さ


    // 見た目（emissive / bloom 基準）
    Math::Vec3f color = { 1.0f, 1.0f, 1.0f };
    float       intensity = 1.0f;

    // 群れ密度（GPU targetCount の元）
    uint32_t    maxCountNear = 2000;    // 近距離での最大個体数

    // 動き（UpdateCSで使う）
    float       speed = 20.0f;
    float       noiseScale = 0.1f;

    // LOD距離（CPUがactive判定・targetCount計算）
    float       nearDistance = 0.1f;    // ここより近い：maxCountNear
    float       farDistance = 20.0f;     // ここより遠い：inactive（0）

    uint32_t seed = 0;

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
        Write<CLeafVolume>,
        Write<CSpatialMotionTag>
    >,
    //受け取るサービスの指定
    ServiceContext<
        LeafService,
        PlayerService,
        TimerService,
        SpatialChunkRegistry
    >>
{
    using Accessor = ComponentAccessor<Write<CLeafVolume>, Write<CSpatialMotionTag>>;
public:

    //指定したサービスを関数の引数として受け取る
    void UpdateImpl(Partition& partition,
        LevelContext<Partition>& levelCtx,
        NoDeletePtr<LeafService> leafService,
        NoDeletePtr<PlayerService> playerService,
        NoDeletePtr<TimerService> timerService,
        NoDeletePtr<SpatialChunkRegistry> chunkReg) {

        auto playerPos = playerService->GetPlayerPosition();

        //プレイヤーの位置をfireflyServiceにも教える
        leafService->SetPlayerPos(playerPos);

        constexpr float chunkRadius = 200.0f;

        auto spatialChunks = partition.CullChunks(playerPos, chunkRadius);

        Query query;
        query.With<CLeafVolume>();

        std::vector<ArchetypeChunk*> archetypeChunks = query.MatchingChunks<std::vector<SFW::SpatialChunk*>&>(spatialChunks);

        BudgetMover::LocalBatch moveBatch(levelCtx.mover, 8);
        bool chasePlayer = leafService->IsChasePlayer();

        for (auto& chunk : archetypeChunks) {
            Accessor accessor = Accessor(chunk);
            size_t entityCount = chunk->GetEntityCount();
            const auto& entities = chunk->GetEntityIDs();

            auto leafVolume = accessor.Get<Write<CLeafVolume>>();
			auto motionTag = accessor.Get<Write<CSpatialMotionTag>>();

            if (!leafVolume.has_value()) [[unlikely]] {
                return;
            }

            for (auto i = 0; i < entityCount; ++i) {
                auto& volume = leafVolume.value()[i];

                if (chasePlayer)
                {
                    // dt（TimerServiceのAPIに合わせて修正）
                    const float dt = static_cast<float>(timerService->GetDeltaTime()); // <-- ここだけ合わせる

                    const float t = leafService->GetElapsedTime();

                    // 「まとわりつく」用の目標位置：プレイヤー周りをふわっと回す
                    const float orbitR = volume.orbitR;     // 回転半径
                    const float orbitW = volume.orbitW;     // 回転速度(rad/sでもOK)
                    Math::Vec3f orbitOff{
                        std::cos(t * orbitW) * orbitR,
                        1.2f + std::sin(t * 1.7f) * 0.4f,   // 少し上下
                        std::sin(t * orbitW) * orbitR
                    };

                    Math::Vec3f target = playerPos + orbitOff;

                    // 追従状態（System内 static で十分：1プレイヤー想定）
                    static Math::Vec3f s_pos = target;
                    static Math::Vec3f s_vel = {};

                    // ばね追従（軽い/見た目良い）
                    const float k = volume.k;   // 追従の強さ
                    const float d = volume.radius;    // 減衰（大きいほど粘る/遅れる）
                    Math::Vec3f a = (target - s_pos) * k - s_vel * d;

                    s_vel += a * dt;
                    s_pos += s_vel * dt;

                    volume.centerWS = s_pos;

                    CSpatialMotionTag& tag = (*motionTag)[i];

                    SFW::MoveIfCrossed_Deferred(entities[i], s_pos, partition, *chunkReg, levelCtx.GetID(), tag.handle, moveBatch);
                }

				float distSqrt = (playerPos - volume.centerWS).lengthSquared();
				// 範囲外なら登録しない
                if (distSqrt > volume.radius * volume.radius) {
                    continue;
                }

				float lodT = (std::sqrt(distSqrt) - volume.nearDistance) / (volume.farDistance - volume.nearDistance);
				lodT = Math::clamp01(lodT);
				float targetCount = Math::lerp((float)volume.maxCountNear, 0.0f, lodT);

                LeafVolumeGPU follow{};
                follow.centerWS = volume.centerWS;
                follow.radius = volume.radius;              // プレイヤー周りの葉っぱ範囲
                follow.color = volume.color;
                follow.intensity = volume.intensity;
                follow.targetCount = targetCount;
                follow.speed = volume.speed;
                follow.noiseScale = volume.noiseScale;
                follow.seed = volume.seed;

                leafService->PushActiveVolume(entities[i].index, follow);
            }
        }
    }
};

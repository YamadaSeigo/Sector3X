#pragma once

struct CChasePlayer
{
	float chaseStrength = 5.0f; // 追従の強さ
};

struct FollowParams {
	float targetRadius = 2.0f;   // R
	float springK = 80.0f;  // k
	float dampingC = 30.0f;  // c
	float omega = 2.5f;   // 周回角速度(rad/s) ※見た目調整
	float tanGain = 6.0f;   // 接線速度追従の強さ
	float maxAccel = 80.0f;  // 加速度上限
};

/// playerPos/playerVel: プレイヤー
/// pos/vel: 浮遊物
/// upHint: 周回平面の上方向（通常は {0,1,0} かプレイヤーのUp）
/// dt: delta time
/// return: 「加える加速度」(m/s^2)  ※力なら mass を掛ける
static Math::Vec3f ComputeFloaterAccel(
	const Math::Vec3f& playerPos, const Math::Vec3f& playerVel,
	const Math::Vec3f& pos, const Math::Vec3f& vel,
	const Math::Vec3f& upHint,
	const FollowParams& p)
{
	// 相対
	Math::Vec3f to = pos - playerPos;
	float r = to.length();
	if (r < 1e-4f) {
		// 真上に飛ばす等で回避（ゼロ割防止）
		to = { 0, 0, 1 };
		r = 1.0f;
	}

	Math::Vec3f rad = to * (1.0f / r);

	// 浮遊物の相対速度（プレイヤー速度を引くと“まとわり”が安定しやすい）
	Math::Vec3f relVel = vel - playerVel;

	// --- 半径方向: バネ + 減衰 ---
	float e = (r - p.targetRadius);           // 距離誤差
	float vr = Dot(relVel, rad);              // 半径方向速度
	Math::Vec3f a_spring = rad * (-p.springK * e);
	Math::Vec3f a_damp = rad * (-p.dampingC * vr);

	// --- 接線方向: 周回 ---
	Math::Vec3f up = Normalize(upHint);
	if (up.lengthSquared() < 1e-6f) up = { 0,1,0 };

	Math::Vec3f tan = Cross(up, rad);
	float tanL2 = tan.lengthSquared();
	if (tanL2 < 1e-6f) {
		// up と rad が平行なら別軸で
		Math::Vec3f alt = (std::fabs(rad.y) < 0.99f) ? Math::Vec3f{ 0,1,0 } : Math::Vec3f{ 1,0,0 };
		tan = Cross(alt, rad);
	}
	tan = Normalize(tan);

	// 目標接線速度 = omega * R
	Math::Vec3f v_tgt = tan * (p.omega * p.targetRadius);

	// 現在の接線速度（相対速度から半径成分を除外）
	Math::Vec3f v_tan = relVel - rad * Dot(relVel, rad);

	Math::Vec3f a_tan = (v_tgt - v_tan) * p.tanGain;

	// 合成 + クランプ
	Math::Vec3f a = a_spring + a_damp + a_tan;
	float clampLen = (std::min)(a.length(), p.maxAccel);
	a = a.normalized() * clampLen;
	return a;
}

static inline FollowParams gFollowParams{};

#ifdef _ENABLE_IMGUI
struct BindFollowParamsToImGui {
	BindFollowParamsToImGui() {
		BIND_DEBUG_SLIDER_FLOAT("ChasePlayer", "TargetRadius", &gFollowParams.targetRadius, 0.1f, 10.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("ChasePlayer", "SpringK", &gFollowParams.springK, 0.1f, 100.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("ChasePlayer", "DampingC", &gFollowParams.dampingC, 0.1f, 100.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("ChasePlayer", "Omega", &gFollowParams.omega, 0.1f, 10.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("ChasePlayer", "TanGain", &gFollowParams.tanGain, 0.1f, 20.0f, 0.1f);
		BIND_DEBUG_SLIDER_FLOAT("ChasePlayer", "MaxAccel", &gFollowParams.maxAccel, 0.1f, 200.0f, 0.1f);
	}
};
static inline BindFollowParamsToImGui s_bindFollowParamsToImGui;

#endif

template<typename Partition>
class ChasePlayerSystem : public ITypeSystem <
	ChasePlayerSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
	Read<CChasePlayer>,
	Read<CTransform>,
	Read<Physics::PhysicsInterpolation>
	>,
	//受け取るサービスの指定
	ServiceContext <
	Physics::PhysicsService,
	PlayerService,
	TimerService
	>>{
	using Accessor = ComponentAccessor<Read<CChasePlayer>, Read<CTransform>, Read<Physics::PhysicsInterpolation>>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		NoDeletePtr<Physics::PhysicsService> physicsService,
		NoDeletePtr<PlayerService> playerService,
		NoDeletePtr<TimerService> timerService) {
		auto targetPos = playerService->GetPlayerPosition();
		auto playerVel = playerService->GetPlayerVelocity();

		auto deltaTime = timerService->GetDeltaTime();

		constexpr float chunkRadius = 300.0f;

		auto spatialChunks = partition.CullChunks(targetPos, chunkRadius);

		targetPos.y += 5.0f; // プレイヤーの頭上5mを目標位置にする

		Query query;
		query.With<CChasePlayer, CTransform, Physics::PhysicsInterpolation>();

		std::vector<ArchetypeChunk*> archetypeChunks = query.MatchingChunks<std::vector<SFW::SpatialChunk*>&>(spatialChunks);

		for (auto& chunk : archetypeChunks) {
			Accessor accessor = Accessor(chunk);
			size_t entityCount = chunk->GetEntityCount();
			const auto& entities = chunk->GetEntityIDs();

			auto chasePlayerComp = accessor.Get<Read<CChasePlayer>>();
			auto transformComp = accessor.Get<Read<CTransform>>();
			auto interpolationComp = accessor.Get<Read<Physics::PhysicsInterpolation>>();

			if (!chasePlayerComp.has_value()) [[unlikely]] {
				return;
			}

			for (auto i = 0; i < entityCount; ++i) {
				auto chasePlayer = chasePlayerComp.value()[i];

				Math::Vec3f oldPos = { interpolationComp.value().ppx()[i], interpolationComp.value().ppy()[i], interpolationComp.value().ppz()[i] };
				Math::Vec3f position = { transformComp.value().px()[i],transformComp.value().py()[i], transformComp.value().pz()[i] };

				// 遠すぎたらテレポートしてしまう（追従が破綻するのを防ぐため）
				float distToPlayer = (position - targetPos).lengthSquared();
				if (distToPlayer > 10000.0f) {
					Math::Quatf rot = { interpolationComp.value().crx()[i],  interpolationComp.value().cry()[i] , interpolationComp.value().crz()[i] , interpolationComp.value().crw()[i] };
					Math::Vec3f vec = position - targetPos.normalized();
					Math::Vec3f newPos = targetPos + Math::Vec3f{ vec.x, 5.0f, vec.z }.normalized() * 30.0f; // プレイヤーから30mの位置にテレポート
					physicsService->Teleport(entities[i], { newPos, rot });

					continue; // プレイヤーから遠すぎるエンティティは処理しない
				}

				Math::Vec3f velocity = (position - oldPos);

				auto accel = ComputeFloaterAccel(targetPos, playerVel, position, velocity, { 0,1,0 }, gFollowParams);
				physicsService->AddForce(entities[i], accel * deltaTime * 5.0f);
			}
		}
	}
};

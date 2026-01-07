#pragma once

#include "../app/SpriteAnimationService.h"

template<typename Partition>
class SpriteAnimationSystem : public ITypeSystem<
	SpriteAnimationSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Write<CSpriteAnimation>,
		Read<CTransform>
	>,
	//受け取るサービスの指定
	ServiceContext<
		SpriteAnimationService,
		SFW::Graphics::RenderService
	>>{
	using Accessor = ComponentAccessor<Write<CSpriteAnimation>, Read<CTransform>>;
public:
	void StartImpl(
		NoDeletePtr<SpriteAnimationService> spriteAnimationService,
		NoDeletePtr< SFW::Graphics::RenderService> renderService)
	{
		using namespace SFW::Graphics;

		auto shaderMgr = renderService->GetResourceManager<DX11::ShaderManager>();
		auto psoMgr = renderService->GetResourceManager<DX11::PSOManager>();

		DX11::ShaderCreateDesc shaderDesc;
		shaderDesc.vsPath = L"assets/shader/VS_SpriteAnimation.cso";
		shaderDesc.psPath = L"assets/shader/PS_Color.cso";
		ShaderHandle shaderHandle;
		shaderMgr->Add(shaderDesc, shaderHandle);

		DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
		psoMgr->Add(psoDesc, psoHandle);
	}


	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		NoDeletePtr<SpriteAnimationService> spriteAnimationService,
		NoDeletePtr< SFW::Graphics::RenderService> renderService) {

		auto uiSession = renderService->GetProducerSession(PassGroupName[GROUP_UI]);
		auto meshManager = renderService->GetResourceManager<Graphics::DX11::MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11::MaterialManager>();
		auto textureManager = renderService->GetResourceManager<Graphics::DX11::TextureManager>();

		auto& globalEntityManager = partition.GetGlobalEntityManager();

		Query query;
		query.With<CSpriteAnimation, CTransform>();
		auto chunks = query.MatchingChunks<ECS::EntityManager&>(globalEntityManager);

		auto updateFunc = [&](Accessor& accessor, size_t entityCount) {

			auto sprite = accessor.Get<Write<CSpriteAnimation>>();
			auto transform = accessor.Get<Read<CTransform>>();

			Math::MTransformSoA mtf = {
					transform->px(), transform->py(), transform->pz(),
					transform->qx(), transform->qy(), transform->qz(), transform->qw(),
					transform->sx(), transform->sy(), transform->sz()
			};

			// ワールド行列を一括生成
			std::vector<float> worldMtxBuffer(12 * entityCount);
			Math::Matrix3x4fSoA worldMtxSoA(worldMtxBuffer.data(), entityCount);
			Math::BuildWorldMatrixSoA_FromTransformSoA(mtf, worldMtxSoA, false);

			std::vector<Graphics::InstanceIndex> instanceIndices(entityCount);
			uiSession.AllocInstancesFromWorldSoA(worldMtxSoA, instanceIndices.data());

			Graphics::DrawCommand cmd;
			cmd.mesh = meshManager->GetSpriteQuadHandle().index;
			cmd.overridePSO = psoHandle.index;
			cmd.viewMask = PASS_UI_MAIN;
			cmd.sortKey = 0;

			for (auto i = 0; i < entityCount; ++i) {
				auto& sp = sprite.value()[i];

				auto instIdx = instanceIndices[i];

				// スプライトアニメーションのインスタンス登録(Time上書き)
				spriteAnimationService->PushSpriteAnimationInstance(sp, instIdx);

				cmd.material = sp.hMat.index;
				cmd.instanceIndex = instIdx;
				cmd.sortKey = sp.layer;
				uiSession.Push(cmd);
			}
			};

		for (ECS::ArchetypeChunk* chunk : chunks)
		{
			Accessor accessor(chunk);
			updateFunc(accessor, chunk->GetEntityCount());
		}
	}
private:
	Graphics::PSOHandle psoHandle = {};
};

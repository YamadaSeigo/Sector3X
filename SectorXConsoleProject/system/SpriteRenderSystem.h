#pragma once

struct CSprite
{
	static inline constexpr uint32_t invalidPSOIndex = 0xFFFFFFFF;

	Graphics::MaterialHandle hMat = {};
	Graphics::PSOHandle overridePSO = { invalidPSOIndex, 0};
	uint32_t layer = 0;

	bool IsOverridePso() const noexcept {
		return overridePSO.index != invalidPSOIndex;
	}
};

template<typename Partition>
class SpriteRenderSystem : public ITypeSystem<
	SpriteRenderSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<CSprite>,
		Read<CTransform>
	>,
	//受け取るサービスの指定
	ServiceContext<
		SFW::Graphics::RenderService
	>>
{
	using Accessor = ComponentAccessor<Read<CSprite>,Read<CTransform>>;
public:
	void StartImpl(NoDeletePtr<SFW::Graphics::RenderService> renderService)
	{
		using namespace SFW::Graphics;

		auto shaderMgr = renderService->GetResourceManager<DX11::ShaderManager>();
		auto psoMgr = renderService->GetResourceManager<DX11::PSOManager>();

		DX11::ShaderCreateDesc shaderDesc;
		shaderDesc.vsPath = L"assets/shader/VS_ClipUV.cso";
		shaderDesc.psPath = L"assets/shader/PS_Color.cso";
		ShaderHandle shaderHandle;
		shaderMgr->Add(shaderDesc, shaderHandle);

		DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
		psoMgr->Add(psoDesc, psoHandle);
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		NoDeletePtr<SFW::Graphics::RenderService> renderService) {

		auto uiSession = renderService->GetProducerSession(PassGroupName[GROUP_UI]);
		auto meshManager = renderService->GetResourceManager<Graphics::DX11::MeshManager>();
		auto materialManager = renderService->GetResourceManager<Graphics::DX11::MaterialManager>();
		auto textureManager = renderService->GetResourceManager<Graphics::DX11::TextureManager>();

		auto& globalEntityManager = partition.GetGlobalEntityManager();

		Query query;
		query.With<CSprite, CTransform>();
		auto chunks = query.MatchingChunks<ECS::EntityManager&>(globalEntityManager);

		auto updateFunc = [&](Accessor& accessor, size_t entityCount) {

			auto sprite = accessor.Get<Read<CSprite>>();
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
				const auto& sp = sprite.value()[i];

				if(sp.IsOverridePso())
					cmd.overridePSO = sp.overridePSO.index;

				cmd.material = sp.hMat.index;
				cmd.instanceIndex = instanceIndices[i];
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
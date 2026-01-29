#pragma once

struct CSprite
{
	static inline constexpr uint32_t invalidIndex = 0xFFFFFFFF;

	Graphics::MaterialHandle hMat = { invalidIndex, 0 };
	Graphics::PSOHandle pso = { invalidIndex, 0 };
	uint32_t layer = 0;

	bool IsOverrideMaterial() const noexcept {
		return hMat.index != invalidIndex;
	}

	bool IsOverridePso() const noexcept {
		return pso.index != invalidIndex;
	}
};

template<typename Partition>
class SpriteRenderSystem : public ITypeSystem<
	SpriteRenderSystem,
	Partition,
	//アクセスするコンポーネントの指定
	ComponentAccess<
		Read<CSprite>,
		Read<CTransform>,
		Read<CColor>
	>,
	//受け取るサービスの指定
	ServiceContext<
		SFW::Graphics::RenderService
	>>
{
	using Accessor = ComponentAccessor<Read<CSprite>,Read<CTransform>, Read<CColor>>;
public:
	void StartImpl(NoDeletePtr<SFW::Graphics::RenderService> renderService)
	{
		using namespace SFW::Graphics;

		auto shaderMgr = renderService->GetResourceManager<DX11::ShaderManager>();
		auto psoMgr = renderService->GetResourceManager<DX11::PSOManager>();
		auto texMgr = renderService->GetResourceManager<DX11::TextureManager>();
		auto materialMgr = renderService->GetResourceManager<DX11::MaterialManager>();

		DX11::ShaderCreateDesc shaderDesc;
		shaderDesc.vsPath = L"assets/shader/VS_ClipUVColor.cso";
		shaderDesc.psPath = L"assets/shader/PS_Color.cso";
		ShaderHandle shaderHandle;
		shaderMgr->Add(shaderDesc, shaderHandle);

		DX11::PSOCreateDesc psoDesc = { shaderHandle, RasterizerStateID::SolidCullBack };
		psoMgr->Add(psoDesc, psoHandle);

		uint32_t initialData[1] = { 0xFFFFFFFF }; // 1ピクセルの白色データ

		DX11::TextureRecipe onePixelWhite = {};
		onePixelWhite.width = 1;
		onePixelWhite.height = 1;
		onePixelWhite.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		onePixelWhite.mipLevels = 1;
		onePixelWhite.bindFlags = D3D11_BIND_SHADER_RESOURCE;
		onePixelWhite.usage = D3D11_USAGE_IMMUTABLE;
		onePixelWhite.initialData = initialData;
		onePixelWhite.initialRowPitch = sizeof(initialData);

		DX11::TextureCreateDesc textureDesc;
		textureDesc.recipe = &onePixelWhite;
		Graphics::TextureHandle onePixelTexHandle;
		texMgr->Add(textureDesc, onePixelTexHandle);

		DX11::MaterialCreateDesc matDesc;
		matDesc.shader = shaderHandle;
		matDesc.psSRV[2] = onePixelTexHandle; // TEX2 にセット
		materialMgr->Add(matDesc, materialHandle);
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
			auto color = accessor.Get<Read<CColor>>();

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
			uiSession.AllocInstancesFromWorldSoAAndColorSoA(worldMtxSoA, &color.value()->color, instanceIndices.data());

			Graphics::DrawCommand cmd;
			cmd.mesh = meshManager->GetSpriteQuadHandle().index;
			cmd.pso = psoHandle.index;
			cmd.viewMask = PASS_UI_MAIN;
			cmd.sortKey = 0;

			for (auto i = 0; i < entityCount; ++i) {
				const auto& sp = sprite.value()[i];

				cmd.pso = sp.IsOverridePso() ? sp.pso.index : psoHandle.index;
				cmd.material = sp.IsOverrideMaterial() ? sp.hMat.index : materialHandle.index;
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
	Graphics::MaterialHandle materialHandle = {};
};
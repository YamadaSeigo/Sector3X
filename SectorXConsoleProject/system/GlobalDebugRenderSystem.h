#pragma once

template<typename Partition>
class GlobalDebugRenderSystem : public ITypeSystem<
	GlobalDebugRenderSystem,
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	//受け取るサービスの指定
	ServiceContext<
		Graphics::RenderService,
		Graphics::I3DPerCameraService,
		Graphics::I2DCameraService,
		DeferredRenderingService
	>>
{

public:
	void StartImpl(
		NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<Graphics::I3DPerCameraService> camera3DService,
		NoDeletePtr<Graphics::I2DCameraService>,
		NoDeletePtr<DeferredRenderingService> deferredRenderService){

		using namespace Graphics;

		auto shaderMgr = renderService->GetResourceManager<Graphics::DX11::ShaderManager>();
		auto psoMgr = renderService->GetResourceManager<Graphics::DX11::PSOManager>();

		DX11::ShaderCreateDesc shaderDesc;
		shaderDesc.vsPath = L"assets/shader/VS_ClipUV.cso";
		shaderDesc.psPath = L"assets/shader/PS_MOCDebug.cso";
		ShaderHandle mocShaderHandle;
		shaderMgr->Add(shaderDesc, mocShaderHandle);

		DX11::PSOCreateDesc psoDesc;
		psoDesc = { mocShaderHandle, RasterizerStateID::SolidCullBack };
		psoMgr->Add(psoDesc, psoMOCHandle);

		auto texMgr = renderService->GetResourceManager<DX11::TextureManager>();

		auto resolution = camera3DService->GetResolution();

		uint32_t width = (UINT)resolution.x;
		uint32_t height = (UINT)resolution.y;

		mocDepth.resize(width * height);

		Graphics::DX11::TextureRecipe recipe =
		{
		.width = width,
		.height = height,
		.format = DXGI_FORMAT_R32_FLOAT,
		.mipLevels = 1,
		.arraySize = 1,
		.usage = D3D11_USAGE_DEFAULT,
		.bindFlags = D3D11_BIND_SHADER_RESOURCE, // 必要に応じて RENDER_TARGET など追加
		.cpuAccessFlags = 0,
		.miscFlags = 0,
		.initialData = mocDepth.data(),
		.initialRowPitch = (UINT)width * sizeof(float)
		};

		DX11::TextureCreateDesc texDesc;
		texDesc.forceSRGB = false;
		texDesc.recipe = &recipe;
		texMgr->Add(texDesc, mocDepthTexHandle);

		Graphics::DX11::MaterialCreateDesc matDesc;
		matDesc.shader = mocShaderHandle;
		matDesc.psSRV[10] = mocDepthTexHandle; // TEX10 にセット
		auto matMgr = renderService->GetResourceManager<Graphics::DX11::MaterialManager>();
		matMgr->Add(matDesc, mocMaterialHandle);

		Graphics::DX11::ShaderCreateDesc deferredShaderDesc;
		deferredShaderDesc.templateID = MaterialTemplateID::Unlit;
		deferredShaderDesc.vsPath = L"assets/shader/VS_ClipUV.cso";
		deferredShaderDesc.psPath = L"assets/shader/PS_DebugDeferred.cso";
		ShaderHandle deferredShaderHandle;
		shaderMgr->Add(deferredShaderDesc, deferredShaderHandle);

		Graphics::DX11::PSOCreateDesc deferredPsoDesc = { deferredShaderHandle, RasterizerStateID::SolidCullBack };
		psoMgr->Add(deferredPsoDesc, deferredPsoHandle);

		PBRMaterialCB pbrMatCB{};
		pbrMatCB.baseColorFactor[0] = 1.0f;
		pbrMatCB.baseColorFactor[1] = 1.0f;
		pbrMatCB.baseColorFactor[2] = 1.0f;
		pbrMatCB.baseColorFactor[3] = 0.0f;

		auto bufferMgr = renderService->GetResourceManager<DX11::BufferManager>();

		BufferHandle matCB_RGM = bufferMgr->AcquireWithContent(&pbrMatCB, sizeof(PBRMaterialCB));

		pbrMatCB.baseColorFactor[0] = 0.0f;
		pbrMatCB.baseColorFactor[1] = 0.0f;
		pbrMatCB.baseColorFactor[2] = 0.0f;
		pbrMatCB.baseColorFactor[3] = 1.0f;

		BufferHandle matCB_A = bufferMgr->AcquireWithContent(&pbrMatCB, sizeof(PBRMaterialCB));

		const auto* deferredTexHandle = deferredRenderService->GetGBufferHandles();

		uint32_t matSlot = 10;
		uint32_t texSlot = 10;
		{
			const auto shaderData = shaderMgr->Get(deferredShaderHandle);
			for (const auto& bind : shaderData.ref().psBindings)
			{
				if (bind.name == DX11::ModelAssetManager::gMaterialBindName)
				{
					matSlot = bind.bindPoint;
				}
				else if (bind.name == DX11::ModelAssetManager::gBaseColorTexBindName)
				{
					texSlot = bind.bindPoint;
				}
			}
		}

		matDesc.shader = deferredShaderHandle;

		for (size_t i = 0; i < DeferredTextureCount; ++i)
		{
			matDesc.psSRV[texSlot] = deferredTexHandle[i];
			matDesc.psCBV[matSlot] = matCB_RGM;
			matMgr->Add(matDesc, deferredMaterialHandle[i]);
			matDesc.psCBV[matSlot] = matCB_A;
			matMgr->Add(matDesc, deferredMaterialHandle[i + DeferredTextureCount]);
		}

		matDesc.psSRV[texSlot] = mocDepthTexHandle;
		matMgr->Add(matDesc, dummyMatHandle);

		DX11::ShaderCreateDesc spriteShaderDesc;
		spriteShaderDesc.vsPath = L"assets/shader/VS_ClipUV.cso";
		spriteShaderDesc.psPath = L"assets/shader/PS_Color.cso";
		ShaderHandle spriteShaderHandle;
		shaderMgr->Add(spriteShaderDesc, spriteShaderHandle);
		DX11::PSOCreateDesc spritePsoDesc = { spriteShaderHandle, RasterizerStateID::SolidCullBack };
		psoMgr->Add(spritePsoDesc, spritePsoHandle);

		DX11::MaterialCreateDesc bloomMatDesc;
		bloomMatDesc.shader = spriteShaderHandle;
		bloomMatDesc.psSRV[2] = DebugRenderType::debugBloomTexHandle;
		matMgr->Add(bloomMatDesc, bloomMaterialHandle);
	}

	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(NoDeletePtr<Graphics::RenderService> renderService,
		NoDeletePtr<Graphics::I3DPerCameraService> camera3DService,
		NoDeletePtr<Graphics::I2DCameraService> camera2DService,
		NoDeletePtr<DeferredRenderingService> deferredRenderService) {

		auto uiSession = renderService->GetProducerSession(PassGroupName[GROUP_UI]);
		auto meshManager = renderService->GetResourceManager<Graphics::DX11::MeshManager>();

		Math::Vec2f resolution = camera2DService->GetVirtualResolution();

		constexpr uint32_t showDeferredTextureCount = sizeof(DebugRenderType::ShowDeferredBufferName) / sizeof(DebugRenderType::ShowDeferredBufferName[0]);

		bool showDeferred = false;
		for (uint32_t i = 0; i < showDeferredTextureCount; ++i)
		{
			if (!DebugRenderType::drawDeferredTextureFlags[i]) continue;

			Math::Matrix4x4f transMat = Math::MakeTranslationMatrix(Math::Vec3f(-resolution.x / DeferredTextureCount + (i % DeferredTextureCount) * resolution.x / DeferredTextureCount, -resolution.y / DeferredTextureCount * (i >= DeferredTextureCount ? -1.0f : 1.0f), 0.0f));
			Math::Matrix4x4f scaleMat = Math::MakeScalingMatrix(Math::Vec3f{ resolution.x / DeferredTextureCount, resolution.y / DeferredTextureCount, 1.0f });
			Graphics::DrawCommand cmd;
			cmd.instanceIndex = uiSession.AllocInstance(transMat * scaleMat);
			cmd.mesh = meshManager->GetSpriteQuadHandle().index;
			cmd.pso = deferredPsoHandle.index;
			cmd.material = deferredMaterialHandle[i].index;
			cmd.viewMask = PASS_UI_MAIN;
			cmd.sortKey = 0;
			uiSession.Push(std::move(cmd));

			showDeferred = true;
		}

		//SRVのバインドを外すためにダミーのコマンド発行
		if (showDeferred)
		{
			Math::Matrix4x4f scaleMat = Math::MakeScalingMatrix(Math::Vec3f{ 0.0f, 0.0f, 0.0f });
			Graphics::DrawCommand cmd;
			cmd.instanceIndex = uiSession.AllocInstance(scaleMat);
			cmd.mesh = meshManager->GetSpriteQuadHandle().index;
			cmd.pso = deferredPsoHandle.index;
			cmd.material = dummyMatHandle.index;
			cmd.viewMask = PASS_UI_MAIN;
			cmd.sortKey = 0;
			uiSession.Push(std::move(cmd));
		}

		if (DebugRenderType::drawMOCDepth)
		{
			renderService->GetDepthBuffer(mocDepth);

			Graphics::DX11::TextureManager* texMgr = renderService->GetResourceManager<Graphics::DX11::TextureManager>();

			texMgr->UpdateTexture(mocDepthTexHandle, mocDepth.data(), (UINT)(resolution.x) * sizeof(float));

			Math::Matrix4x4f transMat = Math::MakeTranslationMatrix(Math::Vec3f(resolution.x / 3.0f, 0.0f, 0.0f));
			Math::Matrix4x4f scaleMat = Math::MakeScalingMatrix(Math::Vec3f{ resolution.x / 3.0f, resolution.y / 3.0f, 1.0f });

			Graphics::DrawCommand cmd;
			cmd.instanceIndex = uiSession.AllocInstance(transMat * scaleMat);
			cmd.mesh = meshManager->GetSpriteQuadHandle().index;
			cmd.pso = psoMOCHandle.index;
			cmd.material = mocMaterialHandle.index;
			cmd.viewMask = PASS_UI_MAIN;
			cmd.sortKey = 0;

			uiSession.Push(std::move(cmd));
		}

		if (DebugRenderType::drawBloom)
		{
			Math::Matrix4x4f transMat = Math::MakeTranslationMatrix(Math::Vec3f(-resolution.x / 3.0f, 0.0f, 0.0f));
			Math::Matrix4x4f scaleMat = Math::MakeScalingMatrix(Math::Vec3f{ resolution.x / 3.0f, resolution.y / 3.0f, 1.0f });
			Graphics::DrawCommand cmd;
			cmd.instanceIndex = uiSession.AllocInstance(transMat * scaleMat);
			cmd.mesh = meshManager->GetSpriteQuadHandle().index;
			cmd.pso = spritePsoHandle.index;
			cmd.material = bloomMaterialHandle.index;
			cmd.viewMask = PASS_UI_MAIN;
			cmd.sortKey = 0;
			uiSession.Push(std::move(cmd));
		}
	}

private:
	Graphics::PSOHandle psoMOCHandle = {};
	Graphics::TextureHandle mocDepthTexHandle = {};
	Graphics::MaterialHandle mocMaterialHandle = {};

	std::vector<float> mocDepth;

	Graphics::PSOHandle deferredPsoHandle = {};
	Graphics::MaterialHandle deferredMaterialHandle[DeferredTextureCount * 2] = {};

	Graphics::MaterialHandle dummyMatHandle;

	Graphics::PSOHandle spritePsoHandle = {};

	Graphics::MaterialHandle bloomMaterialHandle = {};
};

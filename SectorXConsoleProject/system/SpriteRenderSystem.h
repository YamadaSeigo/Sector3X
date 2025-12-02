#pragma once

struct CSprite
{
	SFW::Graphics::TextureHandle hTex;
};

template<typename Partition>
class SpriteRenderSystem : public ITypeSystem<
	SpriteRenderSystem<Partition>,
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
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition,
		UndeletablePtr<SFW::Graphics::RenderService> renderService) {

		auto uiSession = renderService->GetProducerSession(PassGroupName[GROUP_UI]);

		this->ForEachChunkWithAccessor([&](Accessor& accessor, size_t entityCount) {

			auto sprite = accessor.Get<Read<CSprite>>();
			auto transform = accessor.Get<Read<CTransform>>();

			}, partition);
	}
};
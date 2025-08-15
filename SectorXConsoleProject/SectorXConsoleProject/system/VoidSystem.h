#pragma once

template<typename Partition>
class VoidSystem : public ITypeSystem<
	Partition,
	ComponentAccess<>,//アクセスするコンポーネントの指定
	ServiceContext<>>{//受け取るサービスの指定
	using Accessor = ComponentAccessor<>;
public:
	//指定したサービスを関数の引数として受け取る
	void UpdateImpl(Partition& partition) override {
	}
};

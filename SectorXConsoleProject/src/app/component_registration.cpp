#include "component_registration.h"
#include "system/ModelRenderSystem.h"
#include "system/PointLightSystem.h"
#include "system/SpriteRenderSystem.h"
#include "system/SpriteAnimationSystem.h"
#include "system/FireflySystem.h"
#include "system/LeafSystem.h"
#include "system/TitleSystem.h"
#include "system/ChasePlayerSystem.h"

namespace App
{
	void RegisterComponents()
	{
		//ひとりのプロジェクトのなのでまとめているが、
		//複数の場合、ファイルごとに登録関数を呼び出すスタイルでやる

		ComponentTypeRegistry::Register<CModel>();
		ComponentTypeRegistry::Register<CTransform>();
		ComponentTypeRegistry::Register<CSpatialMotionTag>();
		ComponentTypeRegistry::Register<Physics::CPhyBody>();
		ComponentTypeRegistry::Register<Physics::PhysicsInterpolation>();
		ComponentTypeRegistry::Register<Physics::ShapeDims>();
		ComponentTypeRegistry::Register<CColor>();
		ComponentTypeRegistry::Register<CSprite>();
		ComponentTypeRegistry::Register<CPointLight>();
		ComponentTypeRegistry::Register<CSpriteAnimation>();
		ComponentTypeRegistry::Register<CFireflyVolume>();
		ComponentTypeRegistry::Register<CLeafVolume>();
		ComponentTypeRegistry::Register<CTitleSprite>();
		ComponentTypeRegistry::Register<CChasePlayer>();
	}
}
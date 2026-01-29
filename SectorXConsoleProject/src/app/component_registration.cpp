
#include "component_registration.h"
#include "system/ModelRenderSystem.h"
#include "system/PointLightSystem.h"
#include "system/SpriteRenderSystem.h"
#include "system/SpriteAnimationSystem.h"
#include "system/FireflySystem.h"
#include "system/LeafSystem.h"
#include "system/TitleSystem.h"


namespace App
{
    void RegisterComponents()
    {
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
    }
}

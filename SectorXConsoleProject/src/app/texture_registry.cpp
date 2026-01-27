#include "texture_registry.h"
#include <unordered_map>

namespace Assets
{
    struct MaterialRecord {
        std::string albedoPath;
        bool        albedoSRGB = true;
    };

    static std::unordered_map<uint32_t, MaterialRecord> gMaterials = {
        { Mat_Grass, { "assets/texture/terrain/grass.png", true } },
        { Mat_Rock,  { "assets/texture/terrain/RockHigh.jpg", true } },
        { Mat_Dirt,  { "assets/texture/terrain/DirtHigh.png", true } },
        { Mat_Snow,  { "assets/texture/terrain/snow.png", true } },
    };

    static std::unordered_map<uint32_t, std::pair<std::string, bool>> gTextures = {
        { Tex_Splat_Control_0, { "assets/texture/terrain/splat_thin.png", false } },
    };

    bool ResolveTexturePath(uint32_t id, std::string& path, bool& forceSRGB)
    {
        if (auto it = gMaterials.find(id); it != gMaterials.end()) {
            path = it->second.albedoPath;
            forceSRGB = it->second.albedoSRGB;
            return true;
        }
        if (auto it2 = gTextures.find(id); it2 != gTextures.end()) {
            path = it2->second.first;
            forceSRGB = it2->second.second;
            return true;
        }
        return false;
    }
}

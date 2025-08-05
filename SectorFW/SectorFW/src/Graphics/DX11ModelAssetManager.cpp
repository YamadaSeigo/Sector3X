#include "Graphics/DX11/DX11ModelAssetManager.h"

#define CGLTF_IMPLEMENTATION

#include "cgltf/cgltf.h"

#include "Util/logger.h"

namespace SectorFW
{
	namespace Graphics
	{
        DX11ModelAssetManager::DX11ModelAssetManager(DX11MeshManager& meshMgr, DX11MaterialManager& matMgr,
            DX11ShaderManager& shaderMgr, DX11TextureManager& texMgr, ID3D11Device* device):
			meshMgr(meshMgr), matMgr(matMgr), shaderMgr(shaderMgr), texMgr(texMgr), device(device){}

        void DX11ModelAssetManager::ScheduleDestroy(uint32_t idx, uint64_t deleteFrame)
        {
            slots[idx].alive = false;
            pendingDelete.push_back({ idx, deleteFrame });
        }

        void DX11ModelAssetManager::ProcessDeferredDeletes(uint64_t currentFrame)
        {
            auto it = pendingDelete.begin();
            while (it != pendingDelete.end()) {
                if (it->deleteSync <= currentFrame) {
                    auto& data = slots[it->index].data;
                    for (auto& sm : data.subMeshes) {
                        meshMgr.Release(sm.mesh, currentFrame + RENDER_QUEUE_BUFFER_COUNT);
                        matMgr.Release(sm.material, currentFrame + RENDER_QUEUE_BUFFER_COUNT);
                    }

                    {
                        std::scoped_lock lock(cacheMutex);
                        auto cacheIt = assetCache.find(data.path.to_path());
                        if (cacheIt != assetCache.end()) {
                            assetCache.erase(cacheIt);
                        }
                    }

                    freeList.push_back(it->index);
                    it = pendingDelete.erase(it);
                }
                else {
                    ++it;
                }
            }
        }

        int FindParentIndex(const cgltf_node* joint, cgltf_node* const* joints, cgltf_size jointCount)
        {
            if (!joint || !joint->parent)
                return -1;

            const cgltf_node* parent = joint->parent;

            for (cgltf_size i = 0; i < jointCount; ++i)
                if (joints[i] == parent)
                    return static_cast<int>(i);

            return -1;
        }

        template<typename TMatrix>
        TMatrix ExtractMatrixFromAccessor(const cgltf_accessor* accessor, size_t index)
        {
            assert(accessor);
            assert(accessor->type == cgltf_type_mat4);
            assert(accessor->component_type == cgltf_component_type_r_32f);

            float matrixValues[16] = {};
            cgltf_accessor_read_float(accessor, index, matrixValues, 16);

            TMatrix m;
            for (size_t row = 0; row < 4; ++row)
                for (size_t col = 0; col < 4; ++col)
                    m[row][col] = matrixValues[col + row * 4]; // glTF は column-major
            return m;
        }

        const DX11ModelAssetData& DX11ModelAssetManager::LoadFromGLTF(
            const std::string& path,
            ShaderHandle shader,
            PSOHandle pso)
        {
            std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path);

            {
                std::scoped_lock lock(cacheMutex);
                auto it = assetCache.find(canonicalPath);
                if (it != assetCache.end()) {
                    return it->second;
                }
            }

            // 読み込み & アセット構築
            DX11ModelAssetData asset;
            asset.name = canonicalPath.stem().string();

            cgltf_options options = {};
            cgltf_data* data = nullptr;
            if (cgltf_parse_file(&options, canonicalPath.string().c_str(), &data) != cgltf_result_success) {
				assert(false && "Failed to parse GLTF file");
				static DX11ModelAssetData emptyAsset;
                return emptyAsset;
            }
            if (cgltf_load_buffers(&options, data, canonicalPath.string().c_str()) != cgltf_result_success) {
                cgltf_free(data);
				assert(false && "Failed to load GLTF buffers");
                static DX11ModelAssetData emptyAsset;
                return emptyAsset;
            }

            std::filesystem::path baseDir = canonicalPath.parent_path();

            for (size_t ni = 0; ni < data->nodes_count; ++ni) {
                cgltf_node& node = data->nodes[ni];
                if (!node.mesh) continue;

                Math::Matrix4x4f transform = Math::Matrix4x4f::Identity();
                if (node.has_matrix) {
                    memcpy(&transform.m[0][0], node.matrix, sizeof(float) * 16);
                }

                cgltf_mesh& mesh = *node.mesh;
                for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
                    cgltf_primitive& prim = mesh.primitives[pi];

                    size_t vertexCount = prim.attributes[0].data->count;
                    std::vector<float> vertexBuffer(vertexCount * 8, 0.0f);
                    for (size_t i = 0; i < prim.attributes_count; ++i) {
                        cgltf_attribute& attr = prim.attributes[i];
                        for (size_t vi = 0; vi < vertexCount; ++vi) {
                            float v[4] = {};
                            cgltf_accessor_read_float(attr.data, vi, v, 4);
                            if (attr.type == cgltf_attribute_type_position)
                                memcpy(&vertexBuffer[vi * 8 + 0], v, sizeof(float) * 3);
                            else if (attr.type == cgltf_attribute_type_normal)
                                memcpy(&vertexBuffer[vi * 8 + 3], v, sizeof(float) * 3);
                            else if (attr.type == cgltf_attribute_type_texcoord)
                                memcpy(&vertexBuffer[vi * 8 + 6], v, sizeof(float) * 2);
                        }
                    }

                    std::vector<uint32_t> indices;
                    if (prim.indices) {
                        indices.resize(prim.indices->count);
                        for (size_t i = 0; i < prim.indices->count; ++i)
                            indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
                    }

                    DX11MeshCreateDesc meshDesc{
                        .vertices = vertexBuffer.data(),
                        .vSize = vertexBuffer.size() * sizeof(float),
                        .stride = sizeof(float) * 8,
                        .indices = indices.data(),
                        .iSize = indices.size() * sizeof(uint32_t),
                        .sourcePath = canonicalPath.wstring() + L"#" + std::to_wstring(pi) // optional submesh ID
                    };
                    MeshHandle meshHandle = meshMgr.Add(meshDesc);
					meshMgr.AddRef(meshHandle);

                    std::unordered_map<UINT, TextureHandle> srvMap;
                    if (prim.material && prim.material->pbr_metallic_roughness.base_color_texture.texture) {
                        cgltf_image* image = prim.material->pbr_metallic_roughness.base_color_texture.texture->image;
                        if (image && image->uri) {
                            std::filesystem::path texPath = baseDir / image->uri;
                            TextureHandle texHandle = texMgr.Add({ texPath.string() });
							texMgr.AddRef(texHandle);

                            const auto& bindings = shaderMgr.GetBindings(shader);
                            for (const auto& b : bindings) {
                                if (b.name == "gBaseColorTex" && b.type == D3D_SIT_TEXTURE)
                                    srvMap[b.bindPoint] = texHandle;
                            }
                        }
                    }

                    DX11MaterialCreateDesc matDesc{
                        .shader = shader,
                        .srvMap = std::move(srvMap)
                    };
                    MaterialHandle matHandle = matMgr.Add(matDesc);
					matMgr.AddRef(matHandle);

                    asset.subMeshes.push_back(DX11ModelAssetData::SubMesh{
                        .mesh = meshHandle,
                        .material = matHandle,
                        .pso = pso,
                        .instance = {.worldMtx = transform }
                        });
                }
            }

            if (data->skins_count > 0) {
                const cgltf_skin& skin = data->skins[0];

                Skeleton skeleton;

                for (cgltf_size i = 0; i < skin.joints_count; ++i) {
                    const cgltf_node* joint = skin.joints[i];

                    SkeletonJoint j;
                    j.name = joint->name ? joint->name : "";
                    j.parentIndex = FindParentIndex(joint, skin.joints, skin.joints_count);
                    j.inverseBindMatrix = ExtractMatrixFromAccessor<decltype(j.inverseBindMatrix)>(skin.inverse_bind_matrices, i);

                    skeleton.joints.push_back(std::move(j));
                }

                asset.skeleton = std::move(skeleton);
            }

			asset.path = path_view(canonicalPath);

            cgltf_free(data);

			LOG_INFO("Loaded model asset: %s", asset.name);

            // キャッシュに保存して返却
            {
                std::scoped_lock lock(cacheMutex);
                auto [it, inserted] = assetCache.emplace(canonicalPath, std::move(asset));
                return it->second;
            }
        }
	}
}

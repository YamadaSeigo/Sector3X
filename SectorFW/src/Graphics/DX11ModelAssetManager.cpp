#include "Graphics/DX11/DX11ModelAssetManager.h"

#define CGLTF_IMPLEMENTATION

#include "cgltf/cgltf.h"

#include "Util/logger.h"

#include <numbers>

namespace SectorFW
{
	namespace Graphics
	{
		DX11ModelAssetManager::DX11ModelAssetManager(
			DX11MeshManager& meshMgr, DX11MaterialManager& matMgr,
			DX11ShaderManager& shaderMgr, DX11TextureManager& texMgr,
			DX11BufferManager& cbMgr, DX11SamplerManager& samplMgr,
			ID3D11Device* device) :
			meshMgr(meshMgr), matMgr(matMgr), shaderMgr(shaderMgr), texMgr(texMgr), cbManager(cbMgr), samplerManager(samplMgr), device(device) {
		}

		void DX11ModelAssetManager::RemoveFromCaches(uint32_t idx)
		{
			auto& data = slots[idx].data;
			auto cacheIt = pathToHandle.find(data.path.to_path());
			if (cacheIt != pathToHandle.end()) {
				pathToHandle.erase(cacheIt);
			}
		}

		void DX11ModelAssetManager::DestroyResource(uint32_t idx, uint64_t currentFrame)
		{
			auto& data = slots[idx].data;
			for (auto& sm : data.subMeshes) {
				meshMgr.Release(sm.mesh, currentFrame + RENDER_QUEUE_BUFFER_COUNT);
				matMgr.Release(sm.material, currentFrame + RENDER_QUEUE_BUFFER_COUNT);
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

		const DX11ModelAssetData DX11ModelAssetManager::LoadFromGLTF(
			const std::string& path,
			ShaderHandle shader,
			PSOHandle pso,
			bool flipZ)
		{
			std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path);

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

			size_t meshIndex = 0;
			for (size_t ni = 0; ni < data->nodes_count; ++ni) {
				cgltf_node& node = data->nodes[ni];
				if (!node.mesh) continue;

				Math::Matrix4x4f transform = Math::Matrix4x4f::Identity();
				if (node.has_matrix) {
					memcpy(&transform.m[0][0], node.matrix, sizeof(float) * 16);
				}

				//右手座標系のGLTFの場合、Z軸を反転する
				if (flipZ) {
					Math::Quatf rot = Math::Quatf::FromAxisAngle(Math::Vec3f(0, 1, 0), std::numbers::pi_v<float>);
					transform = transform * Math::MakeScalingMatrix(Math::Vec3f(1, 1, -1)) * Math::MakeRotationMatrix(rot);
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
						.sourcePath = canonicalPath.wstring() + L"#" + std::to_wstring(meshIndex++) // optional submesh ID
					};
					MeshHandle meshHandle;
					meshMgr.Add(meshDesc, meshHandle);

					// 1) glTF の PBR 情報を抽出
					PBRMaterialCB pbrCB{};
					if (prim.material) {
						const auto* m = prim.material;
						// baseColorFactor
						if (m->pbr_metallic_roughness.base_color_factor) {
							auto* f = m->pbr_metallic_roughness.base_color_factor;
							memcpy(pbrCB.baseColorFactor, f, sizeof(float) * 4);
						}
						// metallic/roughness
						pbrCB.metallicFactor = (float)m->pbr_metallic_roughness.metallic_factor;
						pbrCB.roughnessFactor = (float)m->pbr_metallic_roughness.roughness_factor;

						// テクスチャ有無フラグ
						pbrCB.hasBaseColorTex = (m->pbr_metallic_roughness.base_color_texture.texture) ? 1.0f : 0.0f;
						pbrCB.hasNormalTex = (m->normal_texture.texture) ? 1.0f : 0.0f;
						pbrCB.hasMRRTex = (m->pbr_metallic_roughness.metallic_roughness_texture.texture) ? 1.0f : 0.0f;
					}

					// 2) マテリアルCBを作成（内容キャッシュで自動重複排除）
					BufferHandle matCB = cbManager.AcquireWithContent(&pbrCB, sizeof(PBRMaterialCB));

					std::unordered_map<UINT, TextureHandle> psSRVMap;
					std::unordered_map<UINT, TextureHandle> vsSRVMap;
					std::unordered_map<UINT, BufferHandle>  psCBVMap;
					std::unordered_map<UINT, BufferHandle>  vsCBVMap;
					std::unordered_map<UINT, SamplerHandle> samplerMap;

					const auto& psBindings = shaderMgr.GetPSBindings(shader);
					for (const auto& b : psBindings) {
						if (b.type == D3D_SIT_CBUFFER && b.name == "MaterialCB") {
							psCBVMap[b.bindPoint] = matCB;
						}
					}
					const auto& vsBindings = shaderMgr.GetVSBindings(shader);
					for (const auto& b : vsBindings) {
						if (b.type == D3D_SIT_CBUFFER && b.name == "MaterialCB") {
							vsCBVMap[b.bindPoint] = matCB;
						}
					}

					// 4) テクスチャの自動割り当て（既存のBaseColorに加えて Normal / MRR にも対応）
					auto bindTex = [&](const char* name, TextureHandle h,
						const std::vector<ShaderResourceBinding>& binding,
						std::unordered_map<UINT, TextureHandle>& map) {
							for (const auto& b : binding)
								if (b.type == D3D_SIT_TEXTURE && b.name == name)
									map[b.bindPoint] = h;
						};

					if (prim.material) {
						const auto* m = prim.material;
						// BaseColor
						if (auto* t = m->pbr_metallic_roughness.base_color_texture.texture) {
							std::filesystem::path texPath = baseDir / t->image->uri;
							TextureHandle tex;
							texMgr.Add({ texPath.string(), /*forceSRGB=*/true }, tex);
							bindTex("gBaseColorTex", tex, psBindings, psSRVMap);
							bindTex("gBaseColorTex", tex, vsBindings, vsSRVMap);
						}
						// Normal
						if (auto* t = m->normal_texture.texture) {
							std::filesystem::path texPath = baseDir / t->image->uri;
							TextureHandle tex;
							texMgr.Add({ texPath.string(), /*forceSRGB=*/false }, tex);
							bindTex("gNormalTex", tex, psBindings, psSRVMap);
							bindTex("gNormalTex", tex, vsBindings, vsSRVMap);
						}
						// MetallicRoughness (通常 R=Occlusion, G=Roughness, B=Metallic 等の流儀があるのでシェーダ側で取り決め)
						if (auto* t = m->pbr_metallic_roughness.metallic_roughness_texture.texture) {
							std::filesystem::path texPath = baseDir / t->image->uri;
							TextureHandle tex;
							texMgr.Add({ texPath.string(), /*forceSRGB=*/false }, tex);
							bindTex("gMetallicRoughness", tex, psBindings, psSRVMap);
							bindTex("gMetallicRoughness", tex, vsBindings, vsSRVMap);
						}
					}

					// 5) サンプラ（デフォルト1個を全テクスチャに共有でもOK）
					D3D11_SAMPLER_DESC sampDesc = {};
					sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
					sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
					SamplerHandle samp = samplerManager.AddWithDesc(sampDesc);

					for (const auto& b : psBindings)
						if (b.type == D3D_SIT_SAMPLER && b.name == "gSampler")
							samplerMap[b.bindPoint] = samp;

					// 6) Material を作る（desc に cbvMap を追加！）
					DX11MaterialCreateDesc matDesc{
						.shader = shader,
						.psSRV = psSRVMap,
						.vsSRV = vsSRVMap,
						.psCBV = psCBVMap,
						.vsCBV = vsCBVMap,
						.samplerMap = samplerMap
					};
					MaterialHandle matHandle;
					bool find = matMgr.Add(matDesc, matHandle);

					//再利用ではない場合呼び出し側の一時参照を返す
					if (!find) {
						for (auto& [slot, th] : psSRVMap) {
							texMgr.Release(th, 0);         // Material側が AddRef 済みなのでここで返す
						}
						for (auto& [slot, th] : vsSRVMap) {
							texMgr.Release(th, 0);         // 同上
						}
						for (auto& [slot, cb] : psCBVMap) {
							cbManager.Release(cb, 0);         // 同上
						}
						for (auto& [slot, cb] : vsCBVMap) {
							cbManager.Release(cb, 0);         // 同上
						}
						for (auto& [slot, sp] : samplerMap) {
							samplerManager.Release(sp, 0);         // 同上
						}
					}
					// -----------------------------------------------

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

			LOG_INFO("Loaded model asset: %s", asset.name.c_str());
			return asset;
		}
	}
}
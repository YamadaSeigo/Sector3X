#include "Graphics/DX11/DX11ModelAssetManager.h"

#define CGLTF_IMPLEMENTATION

#include "cgltf/cgltf.h"

#include "Debug/logger.h"

#include "Math/aabb_util.h"

#include <numbers>

#ifdef USE_MESHOPTIMIZER
#ifdef _DEBUG
#include <meshoptimizer/MDdx64/include/meshoptimizer.h>
#else
#include <meshoptimizer/MDx64/include/meshoptimizer.h>
#endif//_DEBUG
#endif//USE_MESHOPTIMIZER

#define SFW_MATH_ROWVEC 1
#include "Graphics/OccluderToolkit.h"

namespace SectorFW
{
	namespace Graphics
	{
		//-------------------------
			// Occluder適性スコア計算
			//-------------------------
		static float ComputeOccluderScore(const DX11ModelAssetManager::AssetStats& a,
			const SectorFW::Math::AABB3f& bbox,
			bool alphaCutoutThisSubmesh,
			float minThicknessRatio)
		{
			using namespace SectorFW::Math;
			// 寸法と厚み指標
			Vec3f sz = bbox.size(); // ub - lb
			float ex = (std::max)(sz.x, 1e-6f), ey = (std::max)(sz.y, 1e-6f), ez = (std::max)(sz.z, 1e-6f);
			float maxd = (std::max)({ ex, ey, ez });
			float mind = (std::min)({ ex, ey, ez });
			float t = mind / maxd; // 最小厚み比

			// 基本スコア：大きい＆厚いほど高評価
			float s_size = std::clamp(maxd / 10.0f, 0.0f, 1.0f); // 目安: 10mで1.0
			float s_thick = std::clamp((t - minThicknessRatio) / (0.1f - minThicknessRatio + 1e-6f), 0.0f, 1.0f);
			float s_static = a.skinned ? 0.0f : 1.0f;
			float s_alpha = alphaCutoutThisSubmesh ? 0.0f : 1.0f; // 葉/フェンスなどは0寄り

			// 合成（重みは実用的なバランス）
			float score = 0.45f * s_size + 0.30f * s_thick + 0.15f * s_static + 0.10f * s_alpha;
			// ヒーローは邪魔になりがちなので少し抑制
			if (a.hero) score *= 0.8f;
			// 大量配置は価値が上がる（“遮蔽が効く場面が多い”）
			float instBoost = std::clamp(std::log10((std::max)(1u, a.instancesPeak) * 1.0f) * 0.05f, 0.0f, 0.15f);
			score = std::clamp(score + instBoost, 0.0f, 1.0f);
			return score;
		}

		DX11ModelAssetManager::DX11ModelAssetManager(
			DX11MeshManager& meshMgr, DX11MaterialManager& matMgr,
			DX11ShaderManager& shaderMgr, DX11PSOManager& psoMgr,
			DX11TextureManager& texMgr,DX11BufferManager& cbMgr,
			DX11SamplerManager& samplMgr, ID3D11Device* device) :
			meshMgr(meshMgr), matMgr(matMgr), shaderMgr(shaderMgr), psoMgr(psoMgr),
			texMgr(texMgr), cbManager(cbMgr), samplerManager(samplMgr), device(device) {
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
				meshMgr.Release(sm.proxy, currentFrame + RENDER_BUFFER_COUNT);
				matMgr.Release(sm.material, currentFrame + RENDER_BUFFER_COUNT);
				for (auto& lod : sm.lods) {
					meshMgr.Release(lod.mesh, currentFrame + RENDER_BUFFER_COUNT);
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

		const DX11ModelAssetData DX11ModelAssetManager::LoadFromGLTF(const DX11ModelAssetCreateDesc& desc)
		{
			std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(desc.path);

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

				cgltf_mesh& mesh = *node.mesh;
				for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
					cgltf_primitive& prim = mesh.primitives[pi];

					const size_t vertexCount = prim.attributes[0].data->count;
					// --- SoA バッファ ---
					std::vector<Math::Vec3f> positions(vertexCount);
					std::vector<Math::Vec3f> normals;   normals.reserve(vertexCount);
					std::vector<Math::Vec4f> tangents;  tangents.reserve(vertexCount); // w=handedness
					std::vector<Math::Vec2f> tex0;      tex0.reserve(vertexCount);
					std::vector<std::array<uint8_t, 4>> skinIdx; skinIdx.reserve(vertexCount);
					std::vector<std::array<uint8_t, 4>> skinWgt; skinWgt.reserve(vertexCount); // 0..255（合計は≃255）

					auto flip_vec3z = [](float& x, float& y, float& z) { x = -x; };
					auto flip_tangent = [](float& x, float& y, float& z, float& w) { z = -z; w = -w; };

					// まず POSITION を埋める（存在必須）
					for (size_t i = 0; i < prim.attributes_count; ++i) {
						cgltf_attribute& attr = prim.attributes[i];
						if (attr.type != cgltf_attribute_type_position) continue;
						for (size_t vi = 0; vi < vertexCount; ++vi) {
							float v[4] = {};
							cgltf_accessor_read_float(attr.data, vi, v, 4);
							if (desc.rhFlipZ) flip_vec3z(v[0], v[1], v[2]);
							positions[vi] = { v[0], v[1], v[2] };
						}
					}
					// その他の属性（存在すれば取り出し）
					for (size_t i = 0; i < prim.attributes_count; ++i) {
						cgltf_attribute& attr = prim.attributes[i];
						if (attr.type == cgltf_attribute_type_normal) {
							normals.resize(vertexCount);
							for (size_t vi = 0; vi < vertexCount; ++vi) {
								float v[4] = {};
								cgltf_accessor_read_float(attr.data, vi, v, 4);
								if (desc.rhFlipZ) flip_vec3z(v[0], v[1], v[2]);
								normals[vi] = { v[0], v[1], v[2] };
							}
						}
						else if (attr.type == cgltf_attribute_type_tangent) {
							tangents.resize(vertexCount);
							for (size_t vi = 0; vi < vertexCount; ++vi) {
								float v[4] = {};
								cgltf_accessor_read_float(attr.data, vi, v, 4); // (x,y,z,w)
								if (desc.rhFlipZ) flip_tangent(v[0], v[1], v[2], v[3]);
								tangents[vi] = { v[0], v[1], v[2], v[3] };
							}
						}
						else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
							tex0.resize(vertexCount);
							for (size_t vi = 0; vi < vertexCount; ++vi) {
								float v[4] = {};
								cgltf_accessor_read_float(attr.data, vi, v, 4);
								tex0[vi] = { v[0], v[1] };
							}
						}
						else if (attr.type == cgltf_attribute_type_joints && attr.index == 0) {
							skinIdx.resize(vertexCount);
							for (size_t vi = 0; vi < vertexCount; ++vi) {
								uint32_t j[4] = { 0,0,0,0 };
								// 16bit の場合もあるので float で読み→丸め（0..255）
								float tmp[4] = {};
								cgltf_accessor_read_float(attr.data, vi, tmp, 4);
								for (int k = 0; k < 4; ++k) {
									int val = (int)std::round(tmp[k]);
									skinIdx[vi][k] = (uint8_t)std::clamp(val, 0, 255);
								}
							}
						}
						else if (attr.type == cgltf_attribute_type_weights && attr.index == 0) {
							skinWgt.resize(vertexCount);
							for (size_t vi = 0; vi < vertexCount; ++vi) {
								float w[4] = {};
								cgltf_accessor_read_float(attr.data, vi, w, 4); // 0..1 or UNORM
								// 0..255 へ量子化し、合計≃255 になるよう再正規化
								int iw[4] = {
								(int)std::round(w[0] * 255.0f),
								(int)std::round(w[1] * 255.0f),
								(int)std::round(w[2] * 255.0f),
								(int)std::round(w[3] * 255.0f),
								};
								int sum = (std::max)(1, iw[0] + iw[1] + iw[2] + iw[3]);
								// 比率維持で合計255にスケール
								float scale = 255.0f / (float)sum;
								for (int k = 0; k < 4; ++k) iw[k] = (int)std::round(iw[k] * scale);
								// 丸め誤差の補正
								int fix = 255 - (iw[0] + iw[1] + iw[2] + iw[3]);
								iw[0] = std::clamp(iw[0] + fix, 0, 255);
								for (int k = 0; k < 4; ++k) skinWgt[vi][k] = (uint8_t)std::clamp(iw[k], 0, 255);
							}
						}
					}

					std::vector<uint32_t> indices;
					if (prim.indices) {
						indices.resize(prim.indices->count);
						for (size_t i = 0; i < prim.indices->count; ++i) {
							indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
						}
						//if (flipZ) {
						//	// 巻き順反転: (i0, i1, i2) → (i0, i2, i1)
						//	const size_t triCount = indices.size() / 3;
						//	for (size_t t = 0; t < triCount; ++t) {
						//		std::swap(indices[t * 3 + 1], indices[t * 3 + 2]);
						//	}
						//}
					}
					DX11ModelAssetData::SubMesh sub;

					// AABB生成
					sub.aabb = MakeAABB(positions, indices);

					// LOD生成
					//============================================================================

					bool alphaCutout = (prim.material && prim.material->alpha_mode == cgltf_alpha_mode_mask);
					AssetStats stats = {
						(uint32_t)vertexCount,
						desc.instancesPeak,
						desc.viewMin, desc.viewMax,
						data->skins_count > 0,
						alphaCutout,
						desc.hero
					};
					auto recipes = BuildLodRecipes(stats);

					size_t lodLevelNum = recipes.size() + 1;
					sub.lods.resize(lodLevelNum);

					//メッシュ本体（最も高精細な LOD0）
					// =============================================================================
					const std::wstring srcW = canonicalPath.wstring() + L"#" + std::to_wstring(meshIndex++);
					bool ok = meshMgr.AddFromSoA_R8Snorm(
						srcW, positions, normals, tangents, tex0, skinIdx, skinWgt, indices, sub.lods[0].mesh);
					if (!ok) {
						assert(false && "MeshManager::AddFromSoA_R8Snorm failed");
						continue;
					}

					std::vector<DX11MeshManager::ClusterInfo> clusters;
					std::vector<uint32_t> clusterVerts;
					std::vector<uint8_t>  clusterTris;
					DX11MeshManager::BuildClustersWithMeshoptimizer(
						positions, indices,
						clusters, clusterTris, clusterVerts);
					sub.lods[0].clusters = std::move(clusters);

					// 2) LOD1～N を生成
					for (int li = 1; li < lodLevelNum; ++li) {
						DX11MeshManager::RemappedStreams rs;
						std::vector<uint32_t> idx;
						std::wstring tag = canonicalPath.wstring() + L"#sub" + std::to_wstring(meshIndex) + L"-lod" + std::to_wstring(li + 1);
						bool ok = BuildOneLodMesh(indices, positions,
							normals.empty() ? nullptr : &normals,
							tangents.empty() ? nullptr : &tangents,
							tex0.empty() ? nullptr : &tex0,
							skinIdx.empty() ? nullptr : &skinIdx,
							skinWgt.empty() ? nullptr : &skinWgt,
							recipes[li - 1], meshMgr, tag,
							sub.lods[li], idx, rs,
							/*buildClusters=*/true);
						if (!ok) {
							// フォールバック: ひとつ前のLODを使う等
							sub.lods[li] = (li > 0) ? sub.lods[li - 1] : DX11ModelAssetData::SubmeshLOD{};
						}
					}

					// 7) プロキシ（超低比率）
					sub.proxy = sub.lods.back().mesh; // たとえば LOD2 を流用

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

					ShaderHandle shaderHandle;
					{
						auto psoData = psoMgr.Get(desc.pso);
						shaderHandle = psoData.ref().shader;
					}

					{
						auto psShader = shaderMgr.Get(shaderHandle);
						const auto& psBindings = psShader.ref().psBindings;
						for (const auto& b : psBindings) {
							if (b.type == D3D_SIT_CBUFFER && b.name == "MaterialCB") {
								psCBVMap[b.bindPoint] = matCB;
							}
						}

						auto vsShader = shaderMgr.Get(shaderHandle);
						const auto& vsBindings = vsShader.ref().vsBindings;
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
					}

					// 6) Material を作る（desc に cbvMap を追加！）
					DX11MaterialCreateDesc matDesc{
						.shader = shaderHandle,
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

					sub.material = matHandle;
					sub.pso = desc.pso;

					sub.lodThresholds = BuildLodThresholds(stats, (int)lodLevelNum);

					if (node.has_matrix) {
						Math::Matrix4x4f transform = Math::Matrix4x4f::Identity();
						memcpy(transform.data(), node.matrix, sizeof(float) * 16);
						sub.instance.SetData(transform);
					}

					asset.subMeshes.push_back(std::move(sub));

					// ====== Occluder 適性判定 ＆ melt AABB 生成 ======
					if (desc.buildOccluders) {
						auto& subRef = asset.subMeshes.back();
						// 透明/カットアウト判定は prim.material から取ったものを再利用
						bool alphaCutoutThis = alphaCutout;

						// 世界サイズの目安（モデルTRSをここで掛けないならローカル[m]想定）
						SectorFW::Math::Vec3f sz = subRef.aabb.size();
						float diag = std::sqrt(sz.x * sz.x + sz.y * sz.y + sz.z * sz.z);
						if (diag >= desc.minWorldSizeM) {
							float occScore = ComputeOccluderScore(stats, subRef.aabb, alphaCutoutThis, desc.minThicknessRatio);
							subRef.occluder.score = occScore;
							subRef.occluder.candidate = (occScore >= desc.occScoreThreshold);

							if (subRef.occluder.candidate) {
								std::vector<SectorFW::Math::AABB3f> meltAABBs;
								GenerateOccluderAABBs_MaybeWithMelt(
									positions, indices, desc.meltResolution, desc.meltStopRatio, meltAABBs);
								if (meltAABBs.empty()) {
									// 何も作れなかったときだけ候補を落とす
									subRef.occluder.candidate = false;
									subRef.occluder.estimatedAABBCount = 0;
								}
								else {
									subRef.occluder.meltAABBs = std::move(meltAABBs);
									subRef.occluder.estimatedAABBCount =
										static_cast<uint32_t>(subRef.occluder.meltAABBs.size());
									// 任意: 劣化フラグ（melt未導入/フォールバック時にtrueにしたい場合）
									// subRef.occluder.degraded = (subRef.occluder.estimatedAABBCount == 1 && usedFallback);
								}
							}
						}
						else {
							subRef.occluder.candidate = false;
							subRef.occluder.score = 0.0f;
						}
					}
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
					j.inverseBindMatrix = ExtractMatrixFromAccessor<decltype(j.inverseBindMatrix)>(skin.inverse_bind_matrices, i);
					if (desc.rhFlipZ) {
						// IBM' = S · IBM · S, S=diag(1,1,-1)
						const auto S = Math::MakeScalingMatrix(Math::Vec3f(1.f, 1.f, -1.f));
						j.inverseBindMatrix = S * j.inverseBindMatrix * S;
					}

					skeleton.joints.push_back(std::move(j));
				}

				asset.skeleton = std::move(skeleton);
			}

			asset.path = path_view(canonicalPath);

			cgltf_free(data);

			LOG_INFO("Loaded model asset: %s", asset.name.c_str());
			return asset;
		}

		static void MakeStreamsAoS(const std::vector<Math::Vec3f>& pos,
			const std::vector<Math::Vec3f>* nor,
			const std::vector<Math::Vec4f>* tan,
			const std::vector<Math::Vec2f>* uv,
			const std::vector<std::array<uint8_t, 4>>* si,
			const std::vector<std::array<uint8_t, 4>>* sw,
			meshopt_Stream* streams, size_t& sc)
		{
			auto as_u8 = [](auto p) { return reinterpret_cast<const unsigned char*>(p); };
			sc = 0;
			streams[sc++] = { as_u8(&pos[0].x), sizeof(Math::Vec3f), sizeof(Math::Vec3f) };
			if (nor && !nor->empty()) streams[sc++] = { as_u8(&(*nor)[0].x), sizeof(Math::Vec3f), sizeof(Math::Vec3f) };
			if (tan && !tan->empty()) streams[sc++] = { as_u8(&(*tan)[0].x), sizeof(Math::Vec4f), sizeof(Math::Vec4f) };
			if (uv && !uv->empty())  streams[sc++] = { as_u8(&(*uv)[0].x),  sizeof(Math::Vec2f), sizeof(Math::Vec2f) };
			if (si && !si->empty())  streams[sc++] = { reinterpret_cast<const unsigned char*>(si->data()), sizeof((*si)[0]), sizeof((*si)[0]) };
			if (sw && !sw->empty())  streams[sc++] = { reinterpret_cast<const unsigned char*>(sw->data()), sizeof((*sw)[0]), sizeof((*sw)[0]) };
		}

		static bool SimplifyIndices(const DX11ModelAssetManager::LodRecipe& r,
			const std::vector<uint32_t>& baseIdx,
			const std::vector<Math::Vec3f>& pos,
			/*inout*/ std::vector<uint32_t>& outIdx,
			/*for attributes*/ const float* attrAoS, size_t attrStrideBytes, const float* weights, int attrCount,
			float& outError)
		{
			outIdx.resize(baseIdx.size());
			const size_t targetIndexCount = std::max<size_t>(3, size_t(baseIdx.size() * r.targetRatio));
			size_t outCount = 0;

			switch (r.mode)
			{
			case DX11ModelAssetManager::LodQualityMode::Attributes:
				outCount = meshopt_simplifyWithAttributes(
					outIdx.data(), baseIdx.data(), baseIdx.size(),
					&pos[0].x, pos.size(), sizeof(Math::Vec3f),
					attrAoS, attrStrideBytes, weights, attrCount,
					/*locks*/nullptr,
					targetIndexCount, r.targetError,
					/*options*/0, &outError);
				break;

			case DX11ModelAssetManager::LodQualityMode::Permissive:
				outCount = meshopt_simplifyWithAttributes(
					outIdx.data(), baseIdx.data(), baseIdx.size(),
					&pos[0].x, pos.size(), sizeof(Math::Vec3f),
					attrAoS, attrStrideBytes, weights, attrCount,
					/*locks*/nullptr,
					targetIndexCount, /*target_error*/FLT_MAX,
					/*options*/meshopt_SimplifyPermissive, &outError);
				// 到達しないときのフォールバック：Sloppy
				if (outCount < targetIndexCount * 95 / 100) break; // 充分削れた
				if (outCount > targetIndexCount + 6 && targetIndexCount >= 36) {
					// 頂点数ぶん 0 初期化（＝全頂点が自由）
					std::vector<unsigned char> locks(pos.size(), 0);
					outCount = meshopt_simplifySloppy(outIdx.data(), baseIdx.data(), baseIdx.size(),
						&pos[0].x, pos.size(), sizeof(Math::Vec3f),
						locks.data(),
						targetIndexCount, r.targetError, &outError);
				}
				break;

			case DX11ModelAssetManager::LodQualityMode::Sloppy:
				// 頂点数ぶん 0 初期化（＝全頂点が自由）
				std::vector<unsigned char> locks(pos.size(), 0);
				outCount = meshopt_simplifySloppy(
					outIdx.data(), baseIdx.data(), baseIdx.size(),
					&pos[0].x, pos.size(), sizeof(Math::Vec3f),
					locks.data(),
					targetIndexCount, r.targetError, &outError);
				break;
			}

			if (outCount == 0) return false;
			outIdx.resize(outCount);
			return true;
		}

		bool DX11ModelAssetManager::BuildOneLodMesh(
			const std::vector<uint32_t>& baseIndices,
			const std::vector<Math::Vec3f>& basePositions,
			const std::vector<Math::Vec3f>* baseNormals,
			const std::vector<Math::Vec4f>* baseTangents,
			const std::vector<Math::Vec2f>* baseUV0,
			const std::vector<std::array<uint8_t, 4>>* baseSkinIdx,
			const std::vector<std::array<uint8_t, 4>>* baseSkinWgt,
			const LodRecipe& recipe,
			DX11MeshManager& meshMgr,
			const std::wstring& tagForCaching,
			DX11ModelAssetData::SubmeshLOD& outMesh,
			std::vector<uint32_t>& outIdx,
			DX11MeshManager::RemappedStreams& outStreams,
			bool buildClusters)
		{
			if (baseIndices.empty() || basePositions.empty()) return false;

			// --- 0) 属性 AoS テンポラリ（Normal+UV を優先; 必要なら Tangent も） ---
			const bool hasN = baseNormals && !baseNormals->empty();
			const bool hasU = baseUV0 && !baseUV0->empty();
			const bool includeTangent = false; // 接線は後で再生成する想定
			const int attrCount = hasN * 3 + hasU * 2 + (includeTangent ? 4 : 0);
			std::vector<float> attrAoS;
			std::array<float, 16> weights{}; // 上限ゆとり
			size_t cursor = 0;

			if (attrCount > 0) {
				attrAoS.resize(basePositions.size() * attrCount);
				for (size_t i = 0; i < basePositions.size(); ++i) {
					size_t o = i * attrCount;
					if (hasN) {
						const auto& n = (*baseNormals)[i];
						attrAoS[o + 0] = n.x; attrAoS[o + 1] = n.y; attrAoS[o + 2] = n.z; o += 3;
					}
					if (hasU) {
						const auto& t = (*baseUV0)[i];
						attrAoS[o + 0] = t.x; attrAoS[o + 1] = t.y; o += 2;
					}
					if (includeTangent && baseTangents && !baseTangents->empty()) {
						const auto& tg = (*baseTangents)[i];
						attrAoS[o + 0] = tg.x; attrAoS[o + 1] = tg.y; attrAoS[o + 2] = tg.z; attrAoS[o + 3] = tg.w; o += 4;
					}
				}
				// 重み（近=重く, 中=軽め）
				float wN = (recipe.mode == LodQualityMode::Attributes ? recipe.wNormal : 0.6f);
				float wU = (recipe.mode == LodQualityMode::Attributes ? recipe.wUV : 0.3f);
				cursor = 0;
				if (hasN) { weights[cursor++] = wN; weights[cursor++] = wN; weights[cursor++] = wN; }
				if (hasU) { weights[cursor++] = wU; weights[cursor++] = wU; }
				if (includeTangent) { weights[cursor++] = 0.4f; weights[cursor++] = 0.4f; weights[cursor++] = 0.4f; weights[cursor++] = 0.2f; }
			}

			// --- 1) 簡略化（レシピに応じて） ---
			float resultError = 0.f;
			std::vector<uint32_t> idx_lod;
			if (!SimplifyIndices(recipe, baseIndices, basePositions, idx_lod,
				attrCount ? attrAoS.data() : nullptr,
				sizeof(float) * attrCount,
				attrCount ? weights.data() : nullptr, attrCount,
				resultError))
				return false;

			// --- 2) Multi リマップ（UV破綻防止） ---
			std::vector<uint32_t> remap(basePositions.size());
			meshopt_Stream streams[6]; size_t sc = 0;
			MakeStreamsAoS(basePositions, baseNormals, baseTangents, baseUV0, baseSkinIdx, baseSkinWgt, streams, sc);

			const size_t newVertexCount = meshopt_generateVertexRemapMulti(
				remap.data(), idx_lod.data(), idx_lod.size(),
				basePositions.size(), streams, sc);

			// --- 3) インデックス / 全ストリームをリマップ ---
			outIdx.resize(idx_lod.size());
			meshopt_remapIndexBuffer(outIdx.data(), idx_lod.data(), idx_lod.size(), remap.data());

			DX11MeshManager::ApplyRemapToStreams(
				remap, basePositions,
				baseNormals, baseTangents, baseUV0, baseSkinIdx, baseSkinWgt,
				newVertexCount, outStreams);

			// --- 4) 最適化（Cache → Overdraw → FetchRemap） ---
			meshopt_optimizeVertexCache(outIdx.data(), outIdx.data(), outIdx.size(), newVertexCount);
			meshopt_optimizeOverdraw(outIdx.data(), outIdx.data(), outIdx.size(),
				&outStreams.positions[0].x, newVertexCount, sizeof(Math::Vec3f), 1.05f);

			std::vector<uint32_t> fetchRemap(newVertexCount);
			meshopt_optimizeVertexFetchRemap(fetchRemap.data(), outIdx.data(), outIdx.size(), newVertexCount);
			meshopt_remapIndexBuffer(outIdx.data(), outIdx.data(), outIdx.size(), fetchRemap.data());

			meshopt_remapVertexBuffer(outStreams.positions.data(), outStreams.positions.data(), newVertexCount, sizeof(Math::Vec3f), fetchRemap.data());
			if (!outStreams.normals.empty())
				meshopt_remapVertexBuffer(outStreams.normals.data(), outStreams.normals.data(), newVertexCount, sizeof(Math::Vec3f), fetchRemap.data());
			if (!outStreams.tangents.empty())
				meshopt_remapVertexBuffer(outStreams.tangents.data(), outStreams.tangents.data(), newVertexCount, sizeof(Math::Vec4f), fetchRemap.data());
			if (!outStreams.tex0.empty())
				meshopt_remapVertexBuffer(outStreams.tex0.data(), outStreams.tex0.data(), newVertexCount, sizeof(Math::Vec2f), fetchRemap.data());
			if (!outStreams.skinIdx.empty())
				meshopt_remapVertexBuffer(outStreams.skinIdx.data(), outStreams.skinIdx.data(), newVertexCount, sizeof(std::array<uint8_t, 4>), fetchRemap.data());
			if (!outStreams.skinWgt.empty())
				meshopt_remapVertexBuffer(outStreams.skinWgt.data(), outStreams.skinWgt.data(), newVertexCount, sizeof(std::array<uint8_t, 4>), fetchRemap.data());

			// --- 5) MeshManager 登録（SoA → VB/IB） ---
			// ※ あなたの AddFromSoA_R8Snorm のシグネチャに合わせてください
			if (!meshMgr.AddFromSoA_R8Snorm(tagForCaching,
				outStreams.positions, outStreams.normals,
				outStreams.tangents, outStreams.tex0,
				outStreams.skinIdx, outStreams.skinWgt,
				outIdx, outMesh.mesh))
				return false;

#ifdef USE_MESHOPTIMIZER
			if (buildClusters) {
				std::vector<DX11MeshManager::ClusterInfo> clusters;
				std::vector<uint32_t> clusterVerts;
				std::vector<uint8_t>  clusterTris;
				DX11MeshManager::BuildClustersWithMeshoptimizer(
					outStreams.positions, outIdx,
					clusters, clusterTris, clusterVerts);
				outMesh.clusters = std::move(clusters);
			}
#endif
			return true;
		}

		int DX11ModelAssetManager::SelectLod(float s, const LodThresholds& th, int lodCount, int prevLod, float globalBias)
		{
			// globalBias は距離スケールに変換（段±1 ≒しきい値×2^±1 の感覚）
			float biasScale = std::pow(2.0f, globalBias);
			// 遷移方向ごとにヒステリシス
			auto t = [&](int i, bool up) {
				float h = up ? (1.0f + th.hysteresisUp) : (1.0f - th.hysteresisDown);
				return th.T[i] * biasScale * (1.0f - 0.1 * i) * h;
				};

			// s が大きいほど高品質
			if (lodCount <= 1) return 0;
			bool goingUp = (prevLod > 0 && s > th.T[prevLod - 1]); // 粗→細 方向かの簡易判定

			// LOD0?
			if (s > t(0, goingUp)) return 0;
			if (lodCount == 2)     return 1;
			// LOD1?
			if (s > t(1, goingUp)) return 1;
			if (lodCount == 3)     return 2;
			// LOD2?
			if (s > t(2, goingUp)) return 2;
			return (std::min)(lodCount - 1, 3); // LOD3 以降
		}

		std::vector<DX11ModelAssetManager::LodRecipe> DX11ModelAssetManager::BuildLodRecipes(const AssetStats& a)
		{
			auto lg = [](float x) { return std::log10((std::max)(1.0f, x)); };

			// --- 段数決定（ざっくり・安全寄り） ---
			int lodCount = 1; // LOD0のみ
			if (a.vertices >= 300 && a.vertices < 3000)       lodCount = 2; // LOD0-1
			else if (a.vertices >= 3000 && a.vertices < 30000) lodCount = 3; // LOD0-2
			else if (a.vertices >= 30000)                      lodCount = 4; // LOD0-3

			// 大量配置は段数+1、ヒーローは-1（最低1は維持）
			if (a.instancesPeak >= 1000)  lodCount = (std::min)(4, lodCount + 1);
			if (a.hero)                    lodCount = (std::max)(2, lodCount - 1);

			if (lodCount <= 1) return {}; // LOD0のみ

			// --- 基本の ratio ラダー（LOD1, LOD2, LOD3 の目安）---
			// *Attributes→Permissive→Sloppy と遠くほど強めに
			constexpr float baseRatios[3] = { 0.50f, 0.25f, 0.05f }; // LOD1,2,3
			// インスタンス大量 & 視距離レンジ広い → さらに強め
			float instBoost = std::clamp<float>(0.05f * lg((float)(std::max)((uint32_t)1, a.instancesPeak)), 0.0f, 0.20f);
			float rangeBoost = std::clamp<float>(0.05f * lg((std::max)(1.0f, a.viewMax / (std::max)(0.5f, a.viewMin))), 0.0f, 0.20f);

			// 品質を守るべき要因 → 弱め（ratioを上げる＝削減を緩く）
			float qualityGuard = 0.0f;
			if (a.skinned)     qualityGuard += 0.10f;
			if (a.alphaCutout) qualityGuard += 0.05f;  // カードの輪郭破綻を抑える
			if (a.hero)        qualityGuard += 0.15f;

			// ratio 調整係数（下げる=強く削減、上げる=弱く）
			float pushStronger = 1.0f - (instBoost + rangeBoost);   // 0.6～1.0 くらい
			float pushSofter = 1.0f + qualityGuard;               // 1.0～1.3 くらい
			float tune = std::clamp(pushStronger * pushSofter, 0.6f, 1.3f);

			// --- mode / weights / error の方針 ---
			auto modeOf = [&](int level)->LodQualityMode {
				// LOD1=Attributes, LOD2=Permissive, LOD3=Sloppy（ヒーローなら一段ずつ保守的に）
				if (level == 1) return LodQualityMode::Attributes;
				if (level == 2) return a.hero ? LodQualityMode::Attributes : LodQualityMode::Permissive;
				return a.hero ? LodQualityMode::Permissive : LodQualityMode::Sloppy;
				};

			auto weightsOf = [&](LodQualityMode m)->std::pair<float, float> {
				switch (m) {
				case LodQualityMode::Attributes: return { 0.9f, 0.7f }; // 法線・UVを強く保持
				case LodQualityMode::Permissive: return { 0.7f, 0.5f };
				case LodQualityMode::Sloppy:     return { 0.4f, 0.3f };
				}
				return { 0.8f,0.5f };
				};

			auto errorOf = [&](int level, LodQualityMode m)->float {
				// 基本は誤差制限なし（FLT_MAX）。ただし近距離＆重要寄りは少し縛る。
				if (level == 1 && (a.hero || a.skinned)) return 0.02f; // 相対単位：ツール側でスケール扱い
				if (m == LodQualityMode::Attributes && a.alphaCutout)  return 0.03f;
				return std::numeric_limits<float>::infinity();          // FLT_MAX 相当
				};

			// --- レシピ構築 ---
			std::vector<LodRecipe> out;
			out.reserve(lodCount - 1);
			for (int i = 1; i < lodCount; ++i) {
				LodQualityMode m = modeOf(i);
				auto [wn, wuv] = weightsOf(m);

				float r = baseRatios[i - 1];
				// LODが深いほどほんの少しだけ強め（遠景はより削ってOK）
				float depthMul = 1.0f - 0.05f * (i - 1);
				float targetRatio = std::clamp(r * tune * depthMul, 0.05f, 0.90f);

				// カットアウトは輪郭重視 → 近距離は弱め、遠距離は逆に強め
				if (a.alphaCutout) {
					if (i == 1) targetRatio = (std::max)(targetRatio, r * 1.05f);
					else        targetRatio = (std::min)(targetRatio, r * 0.95f);
				}

				LodRecipe rec{};
				rec.mode = m;
				rec.targetRatio = targetRatio;
				rec.targetError = errorOf(i, m);
				rec.wNormal = wn;
				rec.wUV = wuv;

				out.push_back(rec);
			}
			return out;
		}
		LodThresholds DX11ModelAssetManager::BuildLodThresholds(const AssetStats& a, int lodCount)
		{
			LodThresholds th{};
			// --- グローバル既定 ---
			constexpr float base[3] = { 0.10f, 0.05f, 0.01f };

			// --- 係数 k を作る（>1 で早めにLODを落とす、<1で粘る） ---
			auto lg = [](float x) { return std::log10((std::max)(1.0f, x)); };

			// 性能側プッシュ（大量配置・遠近幅）
			float perfPush =
				0.10f * std::clamp<float>(lg((float)(std::max)((uint32_t)1, a.instancesPeak)), 0.0f, 2.0f) +   // 0..0.20
				0.08f * std::clamp<float>(lg((std::max)(1.0f, a.viewMax / (std::max)(0.5f, a.viewMin))), 0.0f, 2.0f); // 0..0.16

			// 品質側ガード（ヒーロー・スキン・カットアウト）
			float qualPull =
				(a.hero ? 0.15f : 0.0f) +
				(a.skinned ? 0.10f : 0.0f) +
				(a.alphaCutout ? 0.05f : 0.0f); // カードの縁を守る

			float k = std::clamp(1.0f + perfPush - qualPull, 0.6f, 1.6f);

			// LODが深いほど少しだけ厳しめ（遠景は落としやすく）
			for (int i = 0; i < 3; ++i) {
				float depthMul = 1.0f + 0.05f * i; // i=0,1,2 → 1.00,1.05,1.10
				th.T[i] = std::clamp(base[i] * k * depthMul, 0.005f, 0.6f);
			}
			th.T[3] = 0.0f; // 番兵

			// ヒステリシスも少し調整（ヒーローはポップ抑制強め）
			if (a.hero) { th.hysteresisUp = 0.20f; th.hysteresisDown = 0.12f; }
			if (a.instancesPeak >= 2000) { th.hysteresisUp *= 0.9f; th.hysteresisDown *= 0.9f; } // 大量配置は敏捷性優先

			// lodCount に合わせて使う本数だけ有効
			// 例: lodCount=2 → T[0]のみ, lodCount=4 → T[0],T[1],T[2]
			return th;
		}
	}
}
/*****************************************************************//**
 * @file   gltf_to_shape.h
 * @brief cgltf のメッシュデータから Physics::ShapeCreateDesc を構築するユーティリティ
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/
#pragma once
#include "PhysicsTypes.h"
#include "../external/cgltf/cgltf.h" // 事前に cgltf を組み込み済みであること
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>

namespace SectorFW
{
	namespace Physics
	{
		/**
		 * @brief アクセサから float3 を読み出す（位置専用、型はFLOAT / COUNT = 3を想定）
		 * @param acc cgltf_accessor
		 * @param out 出力先ベクター
		 * @return 成功/失敗
		 */
		inline bool ReadPositions(const cgltf_accessor* acc, std::vector<Vec3f>& out) {
			if (!acc || acc->type != cgltf_type_vec3) return false;
			out.resize((size_t)acc->count);
			for (cgltf_size i = 0; i < acc->count; ++i) {
				float v[3];
				cgltf_accessor_read_float(acc, i, v, 3);
				out[i] = { v[0], v[1], v[2] };
			}
			return true;
		}

		/**
		 * @brief インデックス（U16 / U32）を取り出す（トライアングル前提）
		 * @param acc cgltf_accessor
		 * @param out 出力先ベクター
		 * @param baseVertex baseVertex 分を加算して格納
		 * @return 成功/失敗
		 */
		inline bool ReadIndices(const cgltf_accessor* acc, std::vector<uint32_t>& out, uint32_t baseVertex) {
			if (!acc) return false;
			out.reserve(out.size() + (size_t)acc->count);
			for (cgltf_size i = 0; i < acc->count; ++i) {
				uint32_t idx = (uint32_t)cgltf_accessor_read_index(acc, i);
				out.push_back(baseVertex + idx);
			}
			return true;
		}

		/**
		 * @brief 一つの cgltf_mesh から MeshDesc を構築（すべての primitive を結合）
		 * @param mesh cgltf_mesh
		 * @param out 出力先 MeshDesc
		 * @return 成功/失敗
		 */
		inline bool BuildMeshDescFromGLTFMesh(const cgltf_mesh* mesh, MeshDesc& out) {
			if (!mesh) return false;

			std::vector<Vec3f> allPos;
			std::vector<uint32_t> allIdx;
			uint32_t baseVertex = 0;

			for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
				const cgltf_primitive& prim = mesh->primitives[p];
				// triangles のみ対象（その他はスキップ or 変換）
				if (prim.type != cgltf_primitive_type_triangles) continue;

				// POSITION
				const cgltf_accessor* posAcc = nullptr;
				for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
					const cgltf_attribute& attr = prim.attributes[a];
					if (attr.type == cgltf_attribute_type_position) { posAcc = attr.data; break; }
				}
				if (!posAcc) continue;

				// 頂点
				std::vector<Vec3f> localPos;
				if (!ReadPositions(posAcc, localPos)) continue;

				// indices（無ければ 0..N-1 連番）
				if (prim.indices) {
					ReadIndices(prim.indices, allIdx, baseVertex);
				}
				else {
					// 連番
					for (uint32_t i = 0; i + 2 < (uint32_t)localPos.size(); i += 3) {
						allIdx.push_back(baseVertex + i + 0);
						allIdx.push_back(baseVertex + i + 1);
						allIdx.push_back(baseVertex + i + 2);
					}
				}

				// 末尾に連結
				allPos.insert(allPos.end(), localPos.begin(), localPos.end());
				baseVertex += (uint32_t)localPos.size();
			}

			if (allPos.empty() || allIdx.empty()) return false;
			out.vertices = std::move(allPos);
			out.indices = std::move(allIdx);
			return true;
		}

		/**
		 * @brief GLTF → ShapeCreateDesc（メッシュ or 凸包）
		 * @param data cgltf_data （cgltf_parse などで取得したもの）
		 * @param meshIndex data->meshes[] の何番目を使うか
		 * @param asConvex false: MeshDesc を使う（トライメッシュ） true : ConvexHullDesc を使う（凸包）
		 * @param scale スケール
		 * @param out 出力先 ShapeCreateDesc
		 * @return 成功/失敗
		 */
		inline bool BuildShapeCreateDescFromGLTF(
			const cgltf_data* data,
			size_t meshIndex,
			bool asConvex,
			const ShapeScale& scale,
			ShapeCreateDesc& out)
		{
			if (!data || meshIndex >= data->meshes_count) return false;
			const cgltf_mesh* mesh = &data->meshes[meshIndex];

			if (!asConvex) {
				MeshDesc md;
				if (!BuildMeshDescFromGLTFMesh(mesh, md)) return false;
				out.shape = std::move(md);
				out.scale = scale;
				return true;
			}
			else {
				// 凸包は“頂点集合のみ”で良い
				MeshDesc md;
				if (!BuildMeshDescFromGLTFMesh(mesh, md)) return false;
				ConvexHullDesc ch;
				ch.points = std::move(md.vertices); // 全頂点を候補に投げる（Jolt が凸包化）
				// ch.maxConvexRadius / ch.hullTolerance は必要に応じて調整
				out.shape = std::move(ch);
				out.scale = scale;
				return true;
			}
		}
	}
}

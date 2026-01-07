#pragma once
#include <vector>
#include <algorithm>
#include <type_traits>
#include <cstddef>

#include "ArchetypeManager.h"
#include "ArchetypeChunk.h"
#include "Query.h"
#include "EntityManager.h"

namespace SFW::ECS {
	//==============================
	// 1) Dense 最速ランナ
	//   - Dense コンポーネントのみ対象（Sparse なし）
	//   - チャンクごとに列ポインタを一度だけ取って連続アクセス
	//   - fn(EntityID, Dense&...)
	//==============================
	struct DenseRunner {
		template<class... Dense, class Fn>
			requires (sizeof...(Dense) > 0)
		static size_t Run(ArchetypeManager& am, Fn&& fn) {
			Query q;
			q.With<Dense...>(); // Dense 限定クエリ（AllDense 制約） :contentReference[oaicite:4]{index=4}
			auto chunks = q.MatchingChunks(am);    // マスク一致チャンク列挙 :contentReference[oaicite:5]{index=5}

			size_t total = 0;
			for (auto* chunk : chunks) {
				// 列ポインタを先に束ねる（GetColumn<T>() は列先頭の生ポインタを返す）
				// 以後は &col[i] で連続メモリアクセス。 :contentReference[oaicite:6]{index=6}
				auto cols = std::tuple{ chunk->GetColumn<Dense>().value()... };

				const auto& ids = chunk->GetEntityIDs(); // 昇順 ID 配列を想定 :contentReference[oaicite:7]{index=7}
				const size_t n = chunk->GetEntityCount();

				for (size_t i = 0; i < n; ++i) {
					std::apply([&](auto*... col) {
						fn(ids[i], col[i]...);
						}, cols);
				}
				total += n;
			}
			return total;
		}
	};

	//==============================
	// 2) Sparse マージ補助
	//   - unordered_map<EntityID, S> など “疎” を Dense 走査にマージ
	//   - フレーム頭などでキーを一度だけ抽出→昇順ソート
	//   - 各チャンクの entities(昇順) と二本指マージで命中だけ適用
	//==============================
	struct SparseMerge {
		// ---- (A) キー抽出 & ソート（フレームに1回推奨） ----
		template<class SparseMap>
		static void BuildSortedKeys(const SparseMap& m, std::vector<EntityID>& outSortedKeys) {
			outSortedKeys.clear();
			outSortedKeys.reserve(m.size());
			for (auto& kv : m) outSortedKeys.push_back(kv.first);
			std::sort(outSortedKeys.begin(), outSortedKeys.end());
		}

		// ---- (B) 二本指マージで命中だけ fn を呼ぶ ----
		// fn の形: fn(rowIndexInChunk, const S& sparseValue)
		template<class SparseMap, class Fn>
		static size_t MergeJoinApply(const std::vector<EntityID>& chunkIDs,
			const SparseMap& sparseMap,
			const std::vector<EntityID>& sortedKeys,
			Fn&& fn)
		{
			size_t i = 0, j = 0, hits = 0;
			const size_t n = chunkIDs.size(), m = sortedKeys.size();

			while (i < n && j < m) {
				EntityID a = chunkIDs[i];
				EntityID b = sortedKeys[j];
				if (a < b) { ++i; }
				else if (a > b) { ++j; }
				else {
					// 命中: find は命中時のみ実行（回数最小化）
					if (auto it = sparseMap.find(a); it != sparseMap.end()) {
						fn(i, it->second);
						++hits;
					}
					++i; ++j;
				}
			}
			return hits;
		}

		// ---- (C) Dense と併用するユーティリティ ----
		// DenseRunner と同じく Dense 列を握り、命中行にだけ applyDense(row, sparseVal, denseRefs...)
		template<class S, class... Dense, class Apply>
		static size_t RunDenseWithSparse(ArchetypeManager& am,
			const std::unordered_map<EntityID, S>& sparseMap,
			const std::vector<EntityID>& sortedKeys,
			Apply&& applyDense)
		{
			Query q; q.With<Dense...>();               // Dense 限定でチャンク列挙 :contentReference[oaicite:8]{index=8}
			auto chunks = q.MatchingChunks(am);        // :contentReference[oaicite:9]{index=9}
			size_t totalHits = 0;

			for (auto* chunk : chunks) {
				auto cols = std::tuple{ chunk->GetColumn<Dense>().value()... }; // 列ポインタ取得 :contentReference[oaicite:10]{index=10}
				const auto& ids = chunk->GetEntityIDs();                         // :contentReference[oaicite:11]{index=11}

				totalHits += MergeJoinApply(ids, sparseMap, sortedKeys,
					[&](size_t row, const S& sval) {
						std::apply([&](auto*... col) {
							applyDense(row, sval, col[row]...);
							}, cols);
					});
			}
			return totalHits;
		}
	};
} // namespace SectorFW::ECS
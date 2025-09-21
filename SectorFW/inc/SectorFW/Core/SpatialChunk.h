/*****************************************************************//**
 * @file   SpatialChunk.h
 * @brief 空間チャンクを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "ECS/EntityManager.h"
#include "RegistryTypes.h"

namespace SectorFW
{
	/**
	 * @brief　空間で分割したチャンクを表すクラス
	 */
	class SpatialChunk
	{
	public:
		/**
		 * @brief 空間チャンクのサイズを定義する型
		 * @detail 32ビット符号なし整数を使用
		 */
		typedef uint32_t SizeType;
		/**
		 * @brief コンストラクタ
		 * @detail EntityManagerの初期化も行っている
		 */
		SpatialChunk() :entityManager(std::make_unique<ECS::EntityManager>()) {}
		/**
		 * @brief エンティティマネージャーの取得関数
		 * @return ECS::EntityManager& エンティティマネージャーへの参照
		 */
		ECS::EntityManager& GetEntityManager() noexcept { return *entityManager; }
		const ECS::EntityManager& GetEntityManager() const noexcept { return *entityManager; }

		// --- NodeKey (space / code / depth / generation) をチャンク単位で保持 ---
		const SpatialChunkKey& GetNodeKey() const noexcept { return nodeKey; }
		void SetNodeKey(const SpatialChunkKey& k) noexcept { nodeKey = k; }
		void BumpGeneration() noexcept { ++nodeKey.generation; }
	private:
		//エンティティマネージャー.空間チャンク内のエンティティを管理する
		std::unique_ptr<ECS::EntityManager> entityManager;
		//このチャンクを特定するキー。現在世代を含む
		SpatialChunkKey nodeKey{};
	};
	/**
	 * @brief 空間チャンクのサイズ型を定義する
	 */
	using ChunkSizeType = SpatialChunk::SizeType;

	namespace ECS
	{
		template<>
		inline std::vector<ArchetypeChunk*> Query::MatchingChunks(std::vector<SectorFW::SpatialChunk*>& context) const noexcept
		{
			std::vector<ArchetypeChunk*> result;
			auto collect_from = [&](const ECS::EntityManager& em) {
				const auto& all = em.GetArchetypeManager().GetAll();
				for (const auto& [_, arch] : all) {
					const ComponentMask& mask = arch->GetMask();
					if ((mask & required) == required && (mask & excluded).none()) {
						const auto& chunks = arch->GetChunks();
						// 先に必要分だけまとめて拡張（平均的に再確保を減らす）
						result.reserve(result.size() + chunks.size());
						for (const auto& ch : chunks) {
							result.push_back(ch.get());
						}
					}
				}
				};

			// 空間ごと
			for (auto spatial : context) {
				collect_from(spatial->GetEntityManager());
			}
			return result;
		}
	}
}

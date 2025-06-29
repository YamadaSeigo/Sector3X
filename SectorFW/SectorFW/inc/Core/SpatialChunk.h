/*****************************************************************//**
 * @file   SpatialChunk.h
 * @brief 空間チャンクを定義するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "../Math/AABB.hpp"
#include "ECS/EntityManager.h"

namespace SectorFW
{
	/**
	 * @brief 空間チャンクを表すクラス
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
		ECS::EntityManager& GetEntityManager() const noexcept { return *entityManager; }
	private:
		Math::AABB3f aabb;
		/**
		 * @brief エンティティマネージャー
		 * @detail 空間チャンク内のエンティティを管理する
		 */
		std::unique_ptr<ECS::EntityManager> entityManager;
	};
	/**
	 * @brief 空間チャンクのサイズ型を定義する
	 */
	using ChunkSizeType = SpatialChunk::SizeType;
}

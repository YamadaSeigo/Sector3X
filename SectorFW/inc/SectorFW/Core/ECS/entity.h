/*****************************************************************//**
 * @file   entity.h
 * @brief エンティティIDを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief 最大エンティティ数
		 */
		static inline constexpr size_t MAX_ENTITY_NUM = 100000;

		/**
		 * @brief EntityのIDを表す構造体
		 */
		struct EntityID {
			uint32_t index = 0;
			uint32_t generation = 0;
			/**
			 * @brief コンストラクタ
			 * @param other EntityIDをコピーする
			 * @return EntityID
			 */
			bool operator==(const EntityID& other) const noexcept {
				return index == other.index && generation == other.generation;
			}
			/**
			 * @brief EntityIDが有効かどうかを確認する
			 * @return 有効な場合はtrue、無効な場合はfalse
			 */
			bool IsValid() const noexcept {
				return index != UINT32_MAX;
			}
			/**
			 * @brief 無効なEntityIDを取得する
			 * @return 無効なEntityID
			 */
			static constexpr EntityID Invalid() noexcept {
				return EntityID{ UINT32_MAX, 0 };
			}
		};
	}
}

namespace std {
	/**
	 * @brief EntityIDのハッシュ関数
	 * @param id EntityID
	 * @return ハッシュ値
	 */
	template <>
	struct hash<SFW::ECS::EntityID> {
		size_t operator()(const SFW::ECS::EntityID& id) const noexcept {
			// 高速な組み合わせ：FNV1a, Boostのhash_combine風
			return std::hash<uint64_t>{}(static_cast<uint64_t>(id.generation) << 32 | id.index);
		}
	};
}

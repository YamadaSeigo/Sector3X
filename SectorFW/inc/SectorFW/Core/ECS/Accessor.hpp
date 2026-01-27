/*****************************************************************//**
 * @file   Accessor.h
 * @brief コンポーネントにアクセスするためのアクセサークラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include "AccessInfo.h"
#include "../../Util/TypeChecker.hpp"

namespace SFW
{
	namespace ECS
	{
		/**
		 * @brief コンポーネントアクセスの型を定義するテンプレート
		 * @tparam AccessTypes アクセスするコンポーネントの型リスト(AccessInfo.h参照)
		 */
		template<typename... AccessTypes>
		struct ComponentAccess {
			/**
			 * @brief アクセス情報を取得するための型
			 */
			using Tuple = std::tuple<AccessTypes...>;
			/**
			 * @brief アクセス情報を取得するための関数
			 * @return AccessInfo アクセス情報
			 */
			static constexpr AccessInfo GetAccessInfo() {
				AccessInfo info;
				(RegisterAccess<AccessTypes>(info), ...);
				return info;
			}
		private:
			/**
			 * @brief 特定のアクセス型に対して、アクセス情報を登録する関数
			 * @param info アクセス情報
			 */
			template<typename T>
			static void RegisterAccess(AccessInfo& info) {
				if constexpr (std::is_base_of_v<Read<typename T::Type>, T>) {
					info.read.insert(ComponentTypeRegistry::GetID<typename T::Type>());
				}
				if constexpr (std::is_base_of_v<Write<typename T::Type>, T>) {
					info.write.insert(ComponentTypeRegistry::GetID<typename T::Type>());
				}
			}
		};
		/**
		 * @brief コンポーネントアクセスのポリシーを定義するテンプレート
		 */
		template<typename AccessType>
		struct AccessPolicy;
		/**
		 * @brief 読み取りアクセスのポリシーを定義するテンプレート
		 */
		template<typename T>
		struct AccessPolicy<Read<T>> {
			using ComponentType = T;
			using PointerType = const typename SoAPtr<T>::type;
		};
		/**
		 * @brief 書き込みアクセスのポリシーを定義するテンプレート
		 */
		template<typename T>
		struct AccessPolicy<Write<T>> {
			using ComponentType = T;
			using PointerType = typename SoAPtr<T>::type;
		};

		// コンポーネントアクセサーの基底クラス
		class ComponentAccessorBase
		{
			// 判定テンプレート
			template <typename, typename = std::void_t<>>
			struct IsToPtr : std::false_type {};
			// ToPtrTagを持つ型はtrue
			template <typename T>
			struct IsToPtr<T, std::void_t<typename T::ToPtrTag>> : std::true_type {};
		public:
			/**
			 * @brief コンストラクタ
			 * @param chunk アクセスするアーキタイプチャンク
			 */
			explicit ComponentAccessorBase(ArchetypeChunk* chunk) noexcept : chunk(chunk) {}

			/**
			 * @brief SoAコンポーネントをAoSコンポーネントに変換する関数
			 * @tparam T SoAコンポーネントの型
			 * @param index 変換するインデックス
			 * @return T AoSコンポーネントの値
			 */
			template<typename T>
				requires ECS::IsSoAComponent<T>
			static T ConvertSoAToAoSComponent(const typename T::ToPtr& p, size_t index) noexcept
			{
				T value{};
				StoreSoAToAoSImpl<T>(
					p, index, value,
					std::make_index_sequence<std::tuple_size_v<decltype(T::member_ptr_tuple)>>{}
				);
				return value;
			}

			/**
			 * @brief チャンクの容量を取得する関数
			 * @return size_t チャンクの容量
			 */
			size_t GetCapacity() const noexcept {
				return chunk->GetCapacity();
			}

		protected:

			template<typename AccessType, typename PtrType, typename ComponentType>
			std::optional<typename AccessPolicy<AccessType>::PointerType> GetComponent()
			{
				auto column = chunk->GetColumn<ComponentType>();
				if (!column) [[unlikely]] return std::nullopt;
				if constexpr (IsSoAComponent<ComponentType>) {
					size_t offset = 0;
					size_t capacity = chunk->GetCapacity();

					//BufferType がちゃんと構築済み＋アラインもOKの前提で変換
					BufferType* base = reinterpret_cast<BufferType*>(column.value());
					PtrType soaPtr;
					GetMemberStartPtr<PtrType>(base, capacity, offset, soaPtr,
						std::make_index_sequence<std::tuple_size_v<decltype(PtrType::ptr_tuple)>>{});
					return soaPtr;
				}
				else {
					return column;
				}
			}

			/**
			 * @brief SoAコンポーネントのメンバーの開始ポインタを取得する関数の実装
			 * @param base SoAコンポーネントのベースポインタ
			 * @param capacity SoAコンポーネントの容量
			 * @param offset 現在のオフセット
			 * @param value SoAコンポーネントのポインタ
			 */
			template<typename PtrType, size_t Index>
			void GetMemberStartPtrImpl(BufferType* base, size_t capacity, size_t& offset, PtrType& value) noexcept
			{
				auto memPtr = std::get<Index>(PtrType::ptr_tuple);
				static_assert(std::is_member_object_pointer_v<decltype(memPtr)>);	// メンバーオブジェクトポインタであることを確認
				auto& member = value.*memPtr;
				using MemberRawType = std::remove_reference_t<decltype(member)>;
				using MemberType = std::remove_pointer_t<MemberRawType>;

				if constexpr (IsToPtr<MemberType>::value) {
					GetMemberStartPtr<MemberType>(
						base, capacity, offset, member,
						std::make_index_sequence<std::tuple_size_v<decltype(MemberType::ptr_tuple)>>{}
					);
				}
				else {
					offset = AlignTo(offset, alignof(MemberType));
					if constexpr (std::is_pointer_v<MemberRawType>) {
						// メンバがポインタ → reinterpret_castで代入可能
						member = reinterpret_cast<MemberRawType>(base + offset);
					}
					else {
						// メンバが値型（floatなど）→ 代入不能 → コンパイルエラーを防ぐためstatic_assert
						static_assert(std::is_pointer_v<MemberRawType>, "Member must be a pointer type");
					}
					offset += sizeof(MemberType) * capacity;
				}
			}
			/**
			 * @brief SoAコンポーネントのメンバーの開始ポインタを取得する関数
			 * @param base SoAコンポーネントのベースポインタ
			 * @param capacity SoAコンポーネントの容量
			 * @param offset 現在のオフセット
			 * @param value SoAコンポーネントのポインタ
			 * @param Is インデックスシーケンス
			 */
			template<typename PtrType, size_t... Is>
			void GetMemberStartPtr(BufferType* base, size_t capacity, size_t& offset, PtrType& value, std::index_sequence<Is...>) noexcept
			{
				(GetMemberStartPtrImpl<PtrType, Is>(base, capacity, offset, value), ...);
			}

			template<typename T, std::size_t... Is>
			static void StoreSoAToAoSImpl(const typename T::ToPtr& p, size_t index, T& out, std::index_sequence<Is...>) noexcept {
				auto& aos_tuple = T::member_ptr_tuple;			// AoSメンバ変数ポインタ
				auto& soa_tuple = p.ptr_tuple;					// SoAメンバ変数ポインタ

				((out.*(std::get<Is>(aos_tuple)) =
					(p.*std::get<Is>(soa_tuple))[index]), ...);
			}
		private:
			// アーキタイプチャンクへのポインタ
			ArchetypeChunk* chunk;
		};

		/**
		 * @brief 指定したアクセス型に基づいて、コンポーネントにアクセスするためのクラス
		 */
		template<typename... AccessTypes>
		class ComponentAccessor : public ComponentAccessorBase {
		public:
			/**
			 * @brief コンストラクタ
			 * @param chunk アクセスするアーキタイプチャンク
			 */
			explicit ComponentAccessor(ArchetypeChunk* chunk) noexcept : ComponentAccessorBase(chunk) {}
			/**
			 * @brief 指定したアクセス型に対して、コンポーネントを取得する関数
			 * @tparam AccessType アクセスするコンポーネントの型(AccessInfo.h参照)
			 * @return　std::optional<PointerType> コンポーネントのポインタ
			 */
			template<typename AccessType>
				requires OneOf<AccessType, AccessTypes...>
			std::optional<typename AccessPolicy<AccessType>::PointerType> Get() noexcept {
				using ComponentType = typename AccessPolicy<AccessType>::ComponentType;
				using PtrType = SoAPtr<ComponentType>::type;

				return GetComponent<AccessType, PtrType, ComponentType>();
			}
			/**
			 * @brief requiresに一致しなかったときのフォールバック定義
			 * @return std::optional<void> 空のオプション
			 */
			template<typename AccessType>
			std::optional<void> Get() noexcept {
				static_assert(OneOf<AccessType, AccessTypes...>,
					"Get<AccessType>: AccessType must be one of the AccessTypes... used in this system.");
				return std::nullopt;
			}
		};
	}
}
/*****************************************************************//**
 * @file   ISystem.h
 * @brief システムのインターフェースを定義するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

#include <typeinfo>
#include <string>

#include "Accessor.hpp"
#include "ServiceLocator.h"
#include "Util/Grid.hpp"

inline std::string demangle_msvc(const char* decorated) {
	char buf[1024];
	if (UnDecorateSymbolName(decorated, buf, sizeof(buf),
		UNDNAME_NAME_ONLY | UNDNAME_NO_ARGUMENTS | UNDNAME_NO_MS_KEYWORDS)) {
		return buf;
	}
	return decorated; // 失敗時はそのまま
}

namespace SectorFW
{
	namespace ECS
	{
		//前方定義
		class EntityManager;
		class SpatialChunk;

		/**
		 * @brief システムのインターフェース
		 * @detail 分割のクラスを指定する
		 * @tparam Partition パーティションの型
		 */
		template<typename Partition>
		class ISystem {
		public:
			/**
			 * @brief システムの開始関数
			 * @param partition 対象のパーティション
			 * @param serviceLocator サービスロケーター
			 */
			virtual void Start(const ServiceLocator& serviceLocator) {}
			/**
			 * @brief システムの更新関数
			 * @param partition 対象のパーティション
			 * @param serviceLocator サービズロケーター
			 */
			virtual void Update(Partition& partition, const ServiceLocator& serviceLocator) = 0;
			/**
			 * @brief アクセス情報の取得関数
			 * @return AccessInfo アクセス情報
			 */
			virtual AccessInfo GetAccessInfo() const noexcept = 0;
			/**
			 * @brief デストラクタ
			 */
			virtual ~ISystem() = default;
			/**
			 * @brief 継承先のクラス名を取得する
			 * @return 継承クラス名
			 */
			std::string derived_name() const {
#ifdef _WIN32
				return demangle_msvc(typeid(*this).name());
#else
				return typeid(*this).name();
#endif
			}
		};
	}
}
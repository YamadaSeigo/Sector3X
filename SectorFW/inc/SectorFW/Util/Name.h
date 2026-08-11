/*****************************************************************//**
 * \file   Name.h
 * \brief  名前の構造体を定義するヘッダーファイル
 * \author lenov
 * \date   July 2026
 *********************************************************************/


#pragma once

namespace SFW
{
	/**
	 * @brief 名前を表す構造体
	 */
	struct Name
	{
		std::string value;

		Name(std::string name) : value(std::move(name)) {
			hash = static_cast<uint32_t>(std::hash<std::string>{}(value));
		}
		
		bool operator==(const Name& other) const noexcept {
			return hash == other.hash;
		}

		const uint32_t GetHash() const noexcept {
			return hash;
		}

	private :
		uint32_t hash;
	};
}
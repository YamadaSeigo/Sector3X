/*****************************************************************//**
 * @file   convert.hpp
 * @brief 型変換を行うためのヘッダーファイル
 * @author seigo_t03b63m
 * @date   July 2025
 *********************************************************************/

#pragma once

#include <bit>
#include <type_traits>
#include <cassert>
#include <numbers>

namespace SectorFW
{
	namespace Math
	{
		template<typename T>
		T Deg2Rad(T degrees) {
			static constexpr T DEG_TO_RAD = std::numbers::pi_v<T> / static_cast<T>(180);
			return degrees * DEG_TO_RAD;
		}

		template<typename T>
		T Rad2Deg(T radians) {
			static constexpr T RAD_TO_DEG = static_cast<T>(180) / std::numbers::pi_v<T>;
			return radians * RAD_TO_DEG;
		}

		template<typename To, typename From>
		To Convert(const From& from); // 定義なし：特殊化のみ許可

		template<typename To, typename From>
		To Convert(const From& from) { return To(); }

		template<typename To, typename From>
		concept BitCastable =
			sizeof(To) == sizeof(From) &&
			std::is_trivially_copyable_v<To> &&
			std::is_trivially_copyable_v<From>;

		template<typename To, typename From>
			requires BitCastable<To, From>
		constexpr To BitCast(const From& from) {
			return std::bit_cast<To>(from);
		}
	}
}
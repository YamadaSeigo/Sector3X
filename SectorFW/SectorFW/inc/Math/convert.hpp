#pragma once

#include <bit>
#include <type_traits>
#include <cassert>

namespace SectorFW
{
	namespace Math
	{
		template<typename To, typename From>
		To Convert(const From& from); // ’è‹`‚È‚µF“Áê‰»‚Ì‚İ‹–‰Â

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
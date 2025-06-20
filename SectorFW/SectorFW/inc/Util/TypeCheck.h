#pragma once

#include <type_traits>

namespace SectorFW
{
    // ”CˆÓ‚ÌŒ^Arg‚Æ‘g‚İ‡‚í‚¹‚Ä Base<T, Arg> ‚ğ’T‚·
    template <typename T, template <typename, typename> class BaseTemplate>
    struct is_crtp_base_of {
    private:
        template <typename Arg>
        static std::true_type test(BaseTemplate<T, Arg>*) {}

        static std::false_type test(...) {}

    public:
        static constexpr bool value = decltype(test(std::declval<T*>()))::value;
    };
}

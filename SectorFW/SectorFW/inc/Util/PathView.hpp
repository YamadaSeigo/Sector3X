#pragma once
#include <filesystem>
#include <string_view>
#include <type_traits>

namespace SectorFW
{
    class path_view {
    public:
        using path_type = std::filesystem::path;
        using value_type = typename path_type::value_type;
        using string_type = typename path_type::string_type;

        using view_type = std::conditional_t<
            std::is_same_v<value_type, char>,
            std::string_view,
            std::wstring_view
        >;

        path_view() = default;

        // Construct from std::filesystem::path (zero-copy view)
        explicit path_view(const path_type& path)
            : view_(path.native().data(), path.native().size()) {
        }

        // Construct from path-like string types
        explicit path_view(const string_type& str)
            : view_(str.data(), str.size()) {
        }

        // Construct from raw pointer and size
        path_view(const value_type* ptr, size_t len)
            : view_(ptr, len) {
        }

        // Get the underlying string_view
        view_type view() const noexcept { return view_; }

        // Implicit conversion
        operator view_type() const noexcept { return view_; }

        // Accessors
        const value_type* data() const noexcept { return view_.data(); }
        size_t size() const noexcept { return view_.size(); }
        bool empty() const noexcept { return view_.empty(); }

        // Optional: convert to std::filesystem::path
        path_type to_path() const { return path_type(view_); }

    private:
        view_type view_{};
    };
}

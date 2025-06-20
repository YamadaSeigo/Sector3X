#pragma once

#include <vector>

namespace SectorFW
{
    template<typename T, typename Size = size_t>
    class Grid2D {
    public:
        explicit Grid2D(Size width, Size height) noexcept
            : m_width(width), m_height(height), m_data(width* height) {
        }

        T& operator()(Size x, Size y) {
            return m_data[y * m_width + x];
        }

        const T& operator()(Size x, Size y) const {
            return m_data[y * m_width + x];
        }

        Size width() const noexcept { return m_width; }
        Size height() const noexcept { return m_height; }

        // foreach ‘Î‰ž
        auto begin() noexcept { return m_data.begin(); }
        auto end() noexcept { return m_data.end(); }
        auto begin() const noexcept { return m_data.begin(); }
        auto end() const noexcept { return m_data.end(); }

        Size size() const noexcept { return m_width * m_height; }

    private:
        Size m_width, m_height;
        std::vector<T> m_data;
    };
}

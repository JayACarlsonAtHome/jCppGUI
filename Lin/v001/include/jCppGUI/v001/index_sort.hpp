// File: include/jCppGUI/v001/index_sort.hpp
// Purpose: Sorting through a vector of indices, so that what is sorted is never copied or moved. std::stable_sort is not
//          constexpr in C++23, so this is a merge sort of its own, and it runs in static_assert.
#pragma once

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "geometry.hpp"

namespace jgui::inline v001 {

// 0, 1, 2 and so on: the items in the order they are held.
[[nodiscard]] constexpr std::vector<std::uint32_t> identity_order(std::uint32_t count) {
    std::vector<std::uint32_t> order(count);
    std::iota(order.begin(), order.end(), std::uint32_t{0});
    return order;
}

// Sorts the indices by what less says of the two items they stand for. less takes two indices. Stable: indices that
// compare equal keep the order they had.
template <class Less>
constexpr void stable_sort_indices(std::vector<std::uint32_t>& order, Less less) {
    const std::size_t count = order.size();
    std::vector<std::uint32_t> spare(count);
    for (std::size_t run = 1; run < count; run = run > count / 2 ? count : run * 2) {
        for (std::size_t start = 0; start < count;) {
            const std::size_t middle = count - start > run ? start + run : count;
            const std::size_t end = count - middle > run ? middle + run : count;
            std::size_t left = start;
            std::size_t right = middle;
            for (std::size_t out = start; out < end; ++out) {
                // Equal items take the left one first, which is what keeps the sort stable.
                const bool take_right = left == middle || (right < end && less(order[right], order[left]));
                spare[out] = order[take_right ? right++ : left++];
            }
            start = end;
        }
        order.swap(spare);
    }
}

// Where an index stands in an order. Empty when it is not there.
[[nodiscard]] constexpr std::optional<std::size_t> position_of_index(std::span<const std::uint32_t> order, std::uint32_t index) noexcept {
    for (std::size_t position = 0; position < order.size(); ++position)
        if (order[position] == index) return position;
    return std::nullopt;
}

}  // namespace jgui::inline v001

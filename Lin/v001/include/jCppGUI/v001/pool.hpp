// File: include/jCppGUI/v001/pool.hpp
// Purpose: Pool<T> and Handle<T>: typed storage for items that come and go, with generations and
//          deferred destruction. Nothing here is behind a base pointer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "error.hpp"

namespace jgui::inline v001 {

// Slots is std::deque in every real pool, because references to items must survive growth. std::deque
// cannot be used in constant evaluation, so static_assert tests use std::vector to run this same logic.
template <class T, template <class...> class Slots = std::deque>
class Pool;

namespace detail {
// Vector slots move their items as they grow, so a pool of them is for compile time only: see Pool::emplace.
template <template <class...> class Slots>
inline constexpr bool compile_time_slots = false;
template <>
inline constexpr bool compile_time_slots<std::vector> = true;
}  // namespace detail

// A reference to one item of one Pool<T>. Only that pool can make one, it has no default value, and
// a handle for one type cannot be passed to a pool of another.
template <class T>
class Handle {
public:
    friend constexpr bool operator==(const Handle&, const Handle&) = default;

private:
    friend class Pool<T, std::deque>;
    friend class Pool<T, std::vector>;
    constexpr Handle(std::uint32_t index, std::uint32_t generation) noexcept
        : index_{index}, generation_{generation} {}

    std::uint32_t index_;
    std::uint32_t generation_;
};

template <class T, template <class...> class Slots>
class Pool {
public:
    // The highest generation a slot may reach before it is retired.
    static constexpr std::uint32_t default_max_generation = std::numeric_limits<std::uint32_t>::max();

    constexpr Pool() = default;
    constexpr explicit Pool(std::uint32_t max_generation) : max_generation_{max_generation} {}

    template <class... A>
        requires(!detail::compile_time_slots<Slots>)
    [[nodiscard]] constexpr std::expected<Handle<T>, Error> emplace(A&&... args) {
        return place(std::forward<A>(args)...);
    }

    // consteval, so using a vector-backed pool at run time does not compile. Any function template that calls
    // this, such as Dynamic::create and everything above it, becomes immediate too, under C++23's rules.
    template <class... A>
        requires detail::compile_time_slots<Slots>
    [[nodiscard]] consteval std::expected<Handle<T>, Error> emplace(A&&... args) {
        return place(std::forward<A>(args)...);
    }

    // Empty for a stale or out-of-range handle, and for an item already marked for destruction.
    [[nodiscard]] constexpr T* find(Handle<T> handle) noexcept {
        Slot* slot = live_slot(handle);
        return slot != nullptr ? &*slot->value : nullptr;
    }
    [[nodiscard]] constexpr const T* find(Handle<T> handle) const noexcept {
        const Slot* slot = live_slot(handle);
        return slot != nullptr ? &*slot->value : nullptr;
    }

    // Marks the item. Lookups fail at once, and its storage stays valid until collect(), so a
    // running callback that belongs to the item can finish safely.
    constexpr void destroy(Handle<T> handle) {
        Slot* slot = live_slot(handle);
        if (slot == nullptr) return;
        slot->dying = true;
        dying_.push_back(handle.index_);
    }

    // Frees everything marked by destroy(). Called when the outermost dispatch ends.
    constexpr void collect() {
        for (const std::uint32_t index : dying_) {
            Slot& slot = slots_[index];
            slot.value.reset();
            slot.dying = false;
            if (slot.generation >= max_generation_) {
                ++retired_;
            } else {
                ++slot.generation;
                free_.push_back(index);
            }
        }
        dying_.clear();
    }

    [[nodiscard]] constexpr std::size_t slot_count() const noexcept { return slots_.size(); }
    [[nodiscard]] constexpr std::size_t retired_count() const noexcept { return retired_; }

private:
    struct Slot {
        std::optional<T> value{};
        std::uint32_t generation{};
        bool dying{};
    };

    template <class... A>
    constexpr std::expected<Handle<T>, Error> place(A&&... args) {
        std::uint32_t index{};
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            if (slots_.size() >= std::size_t{std::numeric_limits<std::uint32_t>::max()})
                return make_error(ErrorCode::PoolExhausted, "the pool has no slot index left");
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[index];
        slot.value.emplace(std::forward<A>(args)...);
        slot.dying = false;
        return Handle<T>{index, slot.generation};
    }

    template <class Self>
    [[nodiscard]] constexpr auto* live_slot(this Self& self, Handle<T> handle) noexcept {
        decltype(&self.slots_[0]) slot = nullptr;
        if (handle.index_ < self.slots_.size()) {
            auto& candidate = self.slots_[handle.index_];
            if (candidate.generation == handle.generation_ && !candidate.dying && candidate.value.has_value())
                slot = &candidate;
        }
        return slot;
    }

    Slots<Slot> slots_{};
    std::vector<std::uint32_t> free_{};
    std::vector<std::uint32_t> dying_{};
    std::uint32_t max_generation_{default_max_generation};
    std::size_t retired_{};
};

}  // namespace jgui::inline v001

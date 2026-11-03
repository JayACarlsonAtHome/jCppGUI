// File: include/jCppGUI/v001/tree.hpp
// Purpose: The typed tree. A window is one composite type built from tuples, tags are looked up at
//          compile time, and Dynamic is the tree node that holds items that come and go.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <deque>
#include <expected>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "message.hpp"
#include "pool.hpp"
#include "redraw_points.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

namespace detail {

template <class Node>
concept Composite = requires { typename Node::children_type; };

template <class Tag, class Node>
consteval std::size_t tag_count();

template <class Tag, class... C>
consteval std::size_t tuple_tag_count(std::type_identity<std::tuple<C...>>) {
    return (std::size_t{0} + ... + tag_count<Tag, C>());
}

template <class Tag, class Node>
consteval std::size_t tag_count() {
    std::size_t count = std::same_as<typename Node::tag, Tag> ? 1 : 0;
    if constexpr (Composite<Node>)
        count += tuple_tag_count<Tag>(std::type_identity<typename Node::children_type>{});
    return count;
}

// True when each tag anywhere below appears once in the tuple that holds the whole tree.
template <class Whole, class Node>
consteval bool node_tags_unique() {
    bool unique = tuple_tag_count<typename Node::tag>(std::type_identity<Whole>{}) == 1;
    if constexpr (Composite<Node>) {
        unique = unique && []<class... C>(std::type_identity<std::tuple<C...>>) {
            return (node_tags_unique<Whole, C>() && ...);
        }(std::type_identity<typename Node::children_type>{});
    }
    return unique;
}

template <class... C>
consteval bool tags_unique() {
    return (node_tags_unique<std::tuple<C...>, C>() && ...);
}

template <class Tag, class... C>
consteval std::size_t first_child_holding(std::type_identity<std::tuple<C...>>) {
    const std::array<bool, sizeof...(C)> holds{(tag_count<Tag, C>() > 0)...};
    for (std::size_t i = 0; i < holds.size(); ++i)
        if (holds[i]) return i;
    return holds.size();
}

template <class Tag, class Node>
constexpr auto& find_in_node(Node& node);

template <class Tag, class Tuple>
constexpr auto& find_in_tuple(Tuple& children) {
    constexpr std::size_t index = first_child_holding<Tag>(std::type_identity<std::remove_const_t<Tuple>>{});
    return find_in_node<Tag>(std::get<index>(children));
}

template <class Tag, class Node>
constexpr auto& find_in_node(Node& node) {
    if constexpr (std::same_as<typename std::remove_const_t<Node>::tag, Tag>) {
        return node;
    } else {
        return find_in_tuple<Tag>(node.children());
    }
}

// One node of the walk for a monitor, and everything below it. A node's position counts every node before it in
// depth-first order, damaged or not, so it stays the same from frame to frame. A composite that can say so answers for
// itself alone. For one that cannot, a child's damage hides its own, so it is named only when no child is damaged.
template <class Node, class U, class Monitor>
constexpr void report_node_damage(Node& node, const TreeKey& key, U& ui, std::size_t& position, Monitor& monitor) {
    const std::size_t here = ++position;
    bool damaged = false;
    if constexpr (requires { { node.own_damage() } -> std::same_as<bool>; }) {
        damaged = node.own_damage();
    } else if constexpr (Composite<Node>) {
        damaged = node.handle(key, ui, QueryDamage{}) && !broadcast(node.children(), key, ui, QueryDamage{});
    } else {
        damaged = node.handle(key, ui, QueryDamage{});
    }
    if (damaged) monitor.widget_damaged(DamagedWidget{class_name_of<Node>(), type_name_of<typename Node::tag>(), here});
    if constexpr (Composite<Node>) {
        std::apply([&](auto&... child) { (report_node_damage(child, key, ui, position, monitor), ...); }, node.children());
    }
}

}  // namespace detail

// A tree node holding items of one concrete type. Several kinds of item use several nodes. Capture
// and focus are typed handles, so they go inert when their item dies.
template <class Tag, Widget T, template <class...> class Slots = std::deque>
class Dynamic {
public:
    using tag = Tag;

    constexpr Dynamic() = default;
    // Assigning over a node could free the item whose callback is running, so a node can only be moved into place.
    Dynamic(const Dynamic&) = delete;
    Dynamic& operator=(const Dynamic&) = delete;
    constexpr Dynamic(Dynamic&&) = default;
    Dynamic& operator=(Dynamic&&) = delete;
    constexpr ~Dynamic() = default;

    template <class... A>
    constexpr std::expected<Handle<T>, Error> create(A&&... args) {
        auto handle = pool_.emplace(std::forward<A>(args)...);
        if (handle) {
            live_.push_back(*handle);
            damaged_ = true;
        }
        return handle;
    }

    constexpr void destroy(Handle<T> handle) {
        if (pool_.find(handle) != nullptr) damaged_ = true;
        pool_.destroy(handle);
    }

    [[nodiscard]] constexpr T* find(Handle<T> handle) noexcept { return pool_.find(handle); }

    // The one-item form used by toggles. A failure is kept and reported by the next layout, because
    // a void callback has nowhere to return it.
    template <class... A>
    constexpr void show(A&&... args) {
        hide();
        if (auto handle = create(std::forward<A>(args)...); !handle) deferred_error_ = std::move(handle.error());
    }

    constexpr void hide() {
        for (std::size_t i = 0; i < live_.size(); ++i) destroy(live_[i]);
    }

    [[nodiscard]] constexpr bool shown() const noexcept {
        for (const Handle<T>& handle : live_)
            if (pool_.find(handle) != nullptr) return true;
        return false;
    }

    // The item holding the pointer or the focus. Empty when none does, or when it has died.
    [[nodiscard]] constexpr T* captured() noexcept { return capture_ ? pool_.find(*capture_) : nullptr; }
    [[nodiscard]] constexpr T* focused() noexcept { return focus_ ? pool_.find(*focus_) : nullptr; }

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }

    constexpr void paint(const TreeKey& key, Canvas& canvas) {
        for_each_live([&](T& item, Handle<T>) {
            item.paint(key, canvas);
            return false;
        });
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        std::expected<Rect, Error> remaining{area};
        if (deferred_error_) {
            remaining = std::unexpected(std::move(*deferred_error_));
            deferred_error_.reset();
            return remaining;
        }
        bounds_ = area;
        for_each_live([&](T& item, Handle<T>) {
            remaining = item.layout(key, *remaining, text);
            return !remaining.has_value();
        });
        return remaining;
    }

    // Topmost first. The item that takes the press holds the pointer and the focus.
    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerPress& event) {
        return for_each_live_reversed([&](T& item, Handle<T> handle) {
            if (!item.handle(key, ui, event)) return false;
            capture_.emplace(handle);
            focus_.emplace(handle);
            return true;
        });
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerRelease& event) {
        const std::optional<Handle<T>> holder = std::exchange(capture_, std::nullopt);
        T* item = holder ? pool_.find(*holder) : nullptr;
        return item != nullptr && item->handle(key, ui, event);
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerMove& event) {
        if (T* holder = captured(); holder != nullptr) return holder->handle(key, ui, event);
        bool hit = false;
        for_each_live_reversed([&](T& item, Handle<T>) {
            if (hit) {
                item.notify(key, ui, PointerLeave{});
            } else {
                hit = item.handle(key, ui, event);
            }
            return false;
        });
        return hit;
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const PointerLeave& event) {
        capture_.reset();
        notify_each(key, ui, event);
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const KeyEvent& event) {
        T* item = focused();
        return item != nullptr && item->handle(key, ui, event);
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearFocus& event) {
        focus_.reset();
        notify_each(key, ui, event);
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const HoldsCapture& event) {
        T* item = captured();
        return item != nullptr && item->handle(key, ui, event);
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const QueryDamage& event) {
        return handle_all(key, ui, event) || damaged_;
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearDamage& event) {
        damaged_ = false;
        notify_each(key, ui, event);
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const CollectDead& event) {
        notify_each(key, ui, event);
        pool_.collect();
        std::erase_if(live_, [&](const Handle<T>& handle) { return pool_.find(handle) == nullptr; });
    }

private:
    // Goes by position, because a callback may add items while this runs. Nothing is removed from
    // live_ until CollectDead, so the positions stay valid, and find() skips the dying.
    template <class F>
    constexpr bool for_each_live(F&& visit) {
        for (std::size_t i = 0; i < live_.size(); ++i) {
            const Handle<T> handle = live_[i];
            if (T* item = pool_.find(handle); item != nullptr && visit(*item, handle)) return true;
        }
        return false;
    }

    template <class F>
    constexpr bool for_each_live_reversed(F&& visit) {
        for (std::size_t i = live_.size(); i-- > 0;) {
            const Handle<T> handle = live_[i];
            if (T* item = pool_.find(handle); item != nullptr && visit(*item, handle)) return true;
        }
        return false;
    }

    template <class U, class E>
    constexpr bool handle_all(const TreeKey& key, U& ui, const E& event) {
        bool any = false;
        for_each_live([&](T& item, Handle<T>) {
            any = item.handle(key, ui, event) || any;
            return false;
        });
        return any;
    }

    template <class U, class E>
    constexpr void notify_each(const TreeKey& key, U& ui, const E& event) {
        for_each_live([&](T& item, Handle<T>) {
            item.notify(key, ui, event);
            return false;
        });
    }

    Pool<T, Slots> pool_{};
    std::vector<Handle<T>> live_{};
    std::optional<Handle<T>> capture_{};
    std::optional<Handle<T>> focus_{};
    std::optional<Error> deferred_error_{};
    Rect bounds_{};
    bool damaged_{};
};

template <class Tag, class T>
[[nodiscard]] constexpr Dynamic<Tag, T> dynamic() {
    return Dynamic<Tag, T>{};
}

template <class B, class Root, class Monitor>
class ApplicationCore;

// What widgets asked of the window system during a dispatch. ApplicationCore carries it out through
// the backend once the dispatch has returned.
struct ResizeRequest {
    Serial serial;
    ResizeEdge edge;
};

struct WindowRequests {
    std::optional<Serial> move;  // the serial of the press that started it
    std::optional<ResizeRequest> resize;
    std::optional<CursorShape> cursor{};
    bool close{};
};

// The root of a tree. Callbacks receive it as their "u", and can reach only what is public: widgets by tag,
// messages, the size and the four requests.
template <Widget... C>
class Window {
public:
    using children_type = std::tuple<C...>;
    static_assert(detail::tags_unique<C...>(), "duplicate widget tag in one tree");

    constexpr explicit Window(C... children) : children_{std::move(children)...} {}

    // A window moves into its application and stays there. Moving one out from under a running callback would
    // leave its tree empty, so that aborts, and a copy or an assignment does not compile.
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    constexpr Window(Window&& other)
        : children_{std::move(other.children_)}, dispatch_depth_{other.dispatch_depth_}, requests_{other.requests_}, size_{other.size_} {
        require(other.dispatch_depth_ == 0, "a Window moved while its tree is delivering an event");
    }
    Window& operator=(Window&&) = delete;
    constexpr ~Window() = default;

    template <class Tag>
    [[nodiscard]] static consteval bool has() {
        return detail::tuple_tag_count<Tag>(std::type_identity<children_type>{}) > 0;
    }

    template <class Tag, class Self>
    [[nodiscard]] constexpr auto& get(this Self& self) {
        static_assert(has<Tag>(), "no widget with this tag in this tree");
        return detail::find_in_tuple<Tag>(self.children_);
    }

    // Immediate delivery to each receiver the message names, in order, with one response each.
    template <class M, class... A>
    constexpr auto send(const A&... values) {
        const DispatchScope scope{*this};
        return detail::SendOp<M>::run(*this, values...);
    }

    // The window's current size. ApplicationCore sets it, after validating it, as soon as a resize
    // arrives, so events that follow are judged against it even before the next layout.
    [[nodiscard]] constexpr Size size() const noexcept { return size_; }

    constexpr void request_move(Serial serial) noexcept { requests_.move = serial; }
    constexpr void request_resize(Serial serial, ResizeEdge edge) noexcept {
        requests_.resize = ResizeRequest{serial, edge};
    }
    constexpr void request_close() noexcept { requests_.close = true; }
    constexpr void request_cursor(CursorShape shape) noexcept { requests_.cursor = shape; }

private:
    template <class B, class Root, class Monitor>
    friend class ApplicationCore;
    friend struct detail::TestAccess;

    // Only the application, and tests, run the tree. A callback, which holds the Window, cannot.
    template <class E>
    constexpr bool dispatch(const E& event) {
        const DispatchScope scope{*this};
        const TreeKey key{};
        return detail::deliver(children_, key, *this, event);
    }

    template <class E>
    constexpr void notify(const E& event) {
        const DispatchScope scope{*this};
        const TreeKey key{};
        detail::notify_all(children_, key, *this, event);
    }

    [[nodiscard]] constexpr bool capture_active() {
        const TreeKey key{};
        return detail::broadcast(children_, key, *this, HoldsCapture{});
    }
    [[nodiscard]] constexpr bool damaged() {
        const TreeKey key{};
        return detail::broadcast(children_, key, *this, QueryDamage{});
    }
    constexpr void clear_damage() {
        const TreeKey key{};
        detail::notify_all(children_, key, *this, ClearDamage{});
    }

    // Tells a monitor which widgets need a redraw, depth first. Only an application with a monitor instantiates this.
    template <class Monitor>
    constexpr void report_damage(Monitor& monitor) {
        const TreeKey key{};
        std::size_t position = 0;
        std::apply([&](auto&... child) { (detail::report_node_damage(child, key, *this, position, monitor), ...); }, children_);
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(Rect area, const Shaper& text) {
        const TreeKey key{};
        return detail::layout_in_order(children_, key, area, text);
    }

    constexpr void paint(Canvas& canvas) {
        const TreeKey key{};
        detail::paint_in_order(children_, key, canvas);
    }

    [[nodiscard]] constexpr bool delivering() const noexcept { return dispatch_depth_ != 0; }
    constexpr void set_size(Size size) noexcept { size_ = size; }
    [[nodiscard]] constexpr WindowRequests take_requests() noexcept { return std::exchange(requests_, WindowRequests{}); }

    class DispatchScope {
    public:
        constexpr explicit DispatchScope(Window& window) noexcept : window_{window} { ++window_.dispatch_depth_; }
        DispatchScope(const DispatchScope&) = delete;
        DispatchScope& operator=(const DispatchScope&) = delete;
        constexpr ~DispatchScope() {
            if (--window_.dispatch_depth_ != 0) return;
            const TreeKey key{};
            detail::notify_all(window_.children_, key, window_, CollectDead{});
        }

    private:
        Window& window_;
    };

    children_type children_;
    std::size_t dispatch_depth_{};
    WindowRequests requests_{};
    Size size_{};
};

template <class... C>
[[nodiscard]] constexpr Window<C...> window(C... children) {
    return Window<C...>{std::move(children)...};
}

}  // namespace jgui::inline v001

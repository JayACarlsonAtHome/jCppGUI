// File: include/jCppGUI/v001/tree.hpp
// Purpose: The typed tree. A window is one composite type built from tuples, tags are looked up at
//          compile time, and Dynamic is the tree node that holds items that come and go.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "clipboard_text.hpp"
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
        for_each_child(node.children(), [&](auto& child) { report_node_damage(child, key, ui, position, monitor); });
    }
}

// A node that can answer for items with no place in the compile-time tree: a Dynamic, or a widget holding one.
template <class Node>
concept HoldsItems = requires { requires Node::holds_items; };

// How many items the compile-time tree has from this node down, counting the node. A Dynamic is one, however many it holds.
template <class Node>
consteval std::uint64_t fixed_item_count();

template <class... C>
consteval std::uint64_t fixed_items_in_tuple(std::type_identity<std::tuple<C...>>) {
    return (std::uint64_t{0} + ... + fixed_item_count<C>());
}

template <class Node>
consteval std::uint64_t fixed_item_count() {
    if constexpr (Composite<Node>) {
        return 1 + fixed_items_in_tuple(std::type_identity<typename Node::children_type>{});
    } else {
        return 1;
    }
}

// False for a child its parent is not showing now, so not laying out. Only a parent that can show one child at a time has an answer.
template <class Node>
[[nodiscard]] constexpr bool child_is_shown(const Node& node, std::size_t index) noexcept {
    if constexpr (requires { { node.shown() } -> std::same_as<std::optional<std::size_t>>; }) {
        return node.shown() == index;
    } else {
        return true;
    }
}

// A run of nodes the compiler knows. In the tree itself, scope is 0 and the position is the ID. Below an item of a Dynamic, scope is that
// item's ID and the recorder gives each its number, since they come and go with the item.
struct ItemWalk {
    std::uint64_t scope{};
    std::uint64_t position{};
};

// One node and everything below it. A widget not shown says so with an empty rectangle at the corner: the one it last had would be
// out of date. An item the recorder has no room for is left out with all that is in it.
template <class Node, class Recorder>
constexpr void record_node(Node& node, const TreeKey& key, Recorder& recorder, ItemWalk& walk, std::uint64_t parent_id, bool shown);

template <class Node, class Recorder>
constexpr void record_node_as(std::uint64_t id, Node& node, const TreeKey& key, Recorder& recorder, ItemWalk& walk, std::uint64_t parent_id, bool shown) {
    recorder.item_found(ItemFound{id, parent_id, class_name_of<Node>(), type_name_of<typename Node::tag>(), shown ? node.bounds() : Rect{}});
    if constexpr (Composite<Node>) {
        std::size_t index = 0;
        const auto visit_child = [&](auto& child) {
            record_node(child, key, recorder, walk, id, shown && child_is_shown(node, index));
            ++index;
        };
        for_each_child(node.children(), visit_child);
    }
    if constexpr (HoldsItems<Node>) {
        node.visit_items(key, [&](auto& item, ItemPlace place) -> void {
            const std::uint64_t item_id = recorder.item_id(ItemKey{id, place.index, place.generation});
            ItemWalk inside{item_id, 0};
            if (item_id != 0) record_node_as(item_id, item, key, recorder, inside, id, shown);
        });
    }
}

template <class Node, class Recorder>
constexpr void record_node(Node& node, const TreeKey& key, Recorder& recorder, ItemWalk& walk, std::uint64_t parent_id, bool shown) {
    ++walk.position;
    const std::uint64_t id = walk.scope == 0 ? walk.position : recorder.item_id(ItemKey{walk.scope, walk.position, 0});
    if (id != 0) record_node_as(id, node, key, recorder, walk, parent_id, shown);
}

}  // namespace detail

// A tree node holding items of one concrete type. Several kinds of item use several nodes. Capture
// and focus are typed handles, so they go inert when their item dies.
template <class Tag, Widget T, template <class...> class Slots = std::deque>
class Dynamic {
public:
    using tag = Tag;
    static constexpr bool holds_items = true;

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

    // Each live item and where it stands in the pool, for a walk that writes the tree down. Only the tree holds a key.
    template <class Visit>
    constexpr void visit_items(const TreeKey&, Visit&& visit) {
        for_each_live([&](T& item, Handle<T> handle) {
            visit(item, pool_.place_of(handle));
            return false;
        });
    }

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
        const std::optional<Handle<T>> holder = std::exchange(capture_, {});
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
    constexpr bool handle(const TreeKey& key, U& ui, const PointerScroll& event) {
        return for_each_live_reversed([&](T& item, Handle<T>) { return item.handle(key, ui, event); });
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const PointerLeave& event) {
        capture_ = {};
        notify_each(key, ui, event);
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const KeyEvent& event) {
        T* item = focused();
        return item != nullptr && item->handle(key, ui, event);
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearFocus& event) {
        focus_ = {};
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
    constexpr void notify(const TreeKey& key, U& ui, const TimersDue& event) {
        notify_each(key, ui, event);
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const CollectDead& event) {
        notify_each(key, ui, event);
        pool_.collect();
        std::erase_if(live_, [&](const Handle<T>& handle) noexcept { return pool_.find(handle) == nullptr; });
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
    // Emptied with = {} and not reset(): clang cannot copy, in a constant expression, an optional of a trivially copyable type
    // that reset() emptied under libstdc++ 15, and one assigned empty copies.
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

// Text for the clipboard, which has been through the sanitizer, and the serial of a press, without which a compositor refuses.
struct CopyRequest {
    Serial serial;
    SafeClipboardText text;
};

struct WindowRequests {
    std::optional<Serial> move;  // the serial of the press that started it
    std::optional<ResizeRequest> resize;
    std::optional<CursorShape> cursor{};
    bool close{};
    std::optional<CopyRequest> copy{};
    bool picture{};
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
    // Sends text through the sanitizer. Clean, it goes to the clipboard, and this answers true. Otherwise nothing goes, this answers false, and it waits as a question for a popup to take. In a window with no such popup it never goes.
    [[nodiscard]] constexpr bool propose_copy_text(Serial serial, std::string_view text) {
        ClipboardInspection inspected = inspect_clipboard_text(text);
        if (inspected.found.none()) {
            requests_.copy = CopyRequest{serial, std::move(inspected.text)};
            return true;
        }
        clipboard_question_ = ClipboardQuestion{std::move(inspected.text), inspected.found};
        return false;
    }
    [[nodiscard]] constexpr std::optional<ClipboardQuestion> take_clipboard_question() { return std::exchange(clipboard_question_, std::nullopt); }
    // What a person said to go on with, and only that: it can only be text that has been through the sanitizer.
    constexpr void copy_after_question(Serial serial, SafeClipboardText text) { requests_.copy = CopyRequest{serial, std::move(text)}; }
    // Asks for a copy of the next frame as it is painted. It is ready for take_window_picture on the turn after that frame, and in a window where nothing takes it, it is only memory until the next one.
    constexpr void request_picture() noexcept { requests_.picture = true; }
    [[nodiscard]] constexpr std::optional<WindowPicture> take_window_picture() { return std::exchange(picture_, std::nullopt); }
    constexpr void request_cursor(CursorShape shape) noexcept { requests_.cursor = shape; }
    // The same, unless something has already asked during this delivery.
    constexpr void suggest_cursor(CursorShape shape) noexcept {
        if (!requests_.cursor) requests_.cursor = shape;
    }
    // For a widget told TimersDue: when it next wants to act. The earliest asked for is when the loop wakes.
    constexpr void wake_at(MonotonicTime when) noexcept {
        if (!wake_ || when < *wake_) wake_ = when;
    }

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

    // Every widget, shown or not, acts on what has come due. The answer is when the earliest of them next wants to.
    [[nodiscard]] constexpr std::optional<MonotonicTime> run_timers(MonotonicTime now) {
        wake_ = {};
        notify(TimersDue{now});
        return std::exchange(wake_, {});
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
        detail::for_each_child(children_, [&](auto& child) { detail::report_node_damage(child, key, *this, position, monitor); });
    }

    // Writes the tree down for a recorder: the window itself as the first item, then the rest, depth first. Only an application with a
    // recorder instantiates this.
    template <class Recorder>
    constexpr void record_items(Recorder& recorder) {
        static constexpr std::uint64_t window_id = 1;
        const TreeKey key{};
        detail::ItemWalk walk{0, window_id};
        recorder.items_begin(window_id + detail::fixed_items_in_tuple(std::type_identity<children_type>{}));
        recorder.item_found(ItemFound{window_id, 0, detail::class_name_of<Window>(), {}, Rect{Point{}, size_}});
        detail::for_each_child(children_, [&](auto& child) { detail::record_node(child, key, recorder, walk, window_id, true); });
        recorder.items_end();
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
    constexpr void keep_picture(WindowPicture picture) { picture_ = std::move(picture); }

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
    std::optional<ClipboardQuestion> clipboard_question_{};
    std::optional<WindowPicture> picture_{};
    // Emptied with = {} and not reset(), as capture_ is.
    std::optional<MonotonicTime> wake_{};
    Size size_{};
};

template <class... C>
[[nodiscard]] constexpr Window<C...> window(C... children) {
    return Window<C...>{std::move(children)...};
}

}  // namespace jgui::inline v001

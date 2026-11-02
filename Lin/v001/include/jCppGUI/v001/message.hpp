// File: include/jCppGUI/v001/message.hpp
// Purpose: Messages: a name, the tags of the widgets that accept it, and ordinary typed values.
//          Routing is resolved at compile time and delivery is immediate.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace jgui::inline v001 {

// The name is what keeps two messages with the same receivers and values distinct types.
template <std::size_t N>
struct MessageName {
    char text[N]{};
    consteval MessageName(const char (&name)[N]) { std::copy_n(name, N, text); }
};

template <class... Receivers>
struct To {};

// The slot in a response tuple for a handler that returns void.
struct NoReply {};

namespace detail {
template <class List>
inline constexpr bool has_receivers = false;
template <class First, class... Rest>
inline constexpr bool has_receivers<To<First, Rest...>> = true;
}  // namespace detail

template <MessageName Name, class Receivers, class... Values>
    requires detail::has_receivers<Receivers>
struct Message {};

namespace detail {

template <class M>
struct SendOp;

template <MessageName Name, class... Receivers, class... Values>
struct SendOp<Message<Name, To<Receivers...>, Values...>> {
    using M = Message<Name, To<Receivers...>, Values...>;

    template <class Tag, class Ui, class... A>
    static constexpr auto deliver_to(Ui& ui, const A&... values) {
        static_assert(Ui::template has<Tag>(), "message receiver is not in this tree");
        auto& node = ui.template get<Tag>();
        using Node = std::remove_cvref_t<decltype(node)>;
        static_assert(requires(Node& n) { n.on(M{}, values...); },
                      "message receiver has no handler for this message");
        using Reply = decltype(node.on(M{}, values...));
        if constexpr (std::is_void_v<Reply>) {
            node.on(M{}, values...);
            return NoReply{};
        } else {
            return node.on(M{}, values...);
        }
    }

    template <class Ui, class... A>
    static constexpr auto run(Ui& ui, const A&... values) {
        static_assert(sizeof...(A) == sizeof...(Values), "message sent with the wrong number of values");
        if constexpr (sizeof...(A) == sizeof...(Values)) {
            static_assert((std::same_as<A, Values> && ...), "message value has the wrong type");
        }
        // Braces guarantee left-to-right evaluation, so responses arrive in receiver order.
        return std::tuple<decltype(deliver_to<Receivers>(ui, values...))...>{
            deliver_to<Receivers>(ui, values...)...};
    }
};

}  // namespace detail

}  // namespace jgui::inline v001

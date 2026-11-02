// File: src/wayland/trampoline.hpp
// Purpose: The one place the libwayland C boundary is crossed, and the two casts back from an untyped pointer in
//          our code. libwayland stores an untyped pointer with each listener and hands it back to a C function,
//          and it returns a new global untyped. Each is tied by type to the one type it can really be, and the
//          pointer goes no further. This exception ends when our own Wayland client replaces libwayland.
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

#include <wayland-client.h>

namespace jgui::inline v001::detail {

// The C function for one noexcept member function. Requiring noexcept in the pattern means that a
// handler which might let an exception cross into libwayland does not compile.
template <auto Method>
struct Trampoline;

template <class Owner, class... Args, void (Owner::*Method)(Args...) noexcept>
struct Trampoline<Method> {
    using owner = Owner;
    static void call(void* data, Args... args) noexcept { (static_cast<Owner*>(data)->*Method)(args...); }
};

// A generated C listener table together with the one type its functions cast back to. Each entry comes from
// on<Method>, which does not compile for another type's method, so the table cannot mix owners.
template <class Owner, class Table>
struct OwnedListener {
    template <auto Method>
        requires std::same_as<typename Trampoline<Method>::owner, Owner>
    static constexpr auto on = &Trampoline<Method>::call;

    consteval explicit OwnedListener(const Table& entries) noexcept : table{entries} {}

    // For a table that grows as its protocol does: only the entries given are set, and the rest stay empty.
    template <class Fill>
        requires std::invocable<Fill, Table&>
    consteval explicit OwnedListener(Fill fill) noexcept : table{} {
        fill(table);
    }

    Table table;
};

// Registers a listener with the owner it will be cast back to. Add is the generated <interface>_add_listener
// function, so the proxy and table types are checked by the compiler, and the owner must be exactly the listener's
// own, so passing the wrong object does not compile. False when libwayland refused it, which leaves the
// object deaf to its events.
template <auto Add, class Proxy, class Owner, class Table>
[[nodiscard]] bool listen(Proxy* proxy, const OwnedListener<Owner, Table>& listener, std::type_identity_t<Owner>& owner) noexcept {
    return Add(proxy, &listener.table, &owner) == 0;
}

// Which interface description goes with a generated proxy type. Each use defines its own.
template <class Global>
struct GlobalInterface;

// wl_registry_bind returns the new object untyped. The interface it is given decides its real type, and that
// interface comes from the type asked for, so the two cannot disagree.
template <class Global>
[[nodiscard]] Global* bind_global(wl_registry* registry, std::uint32_t name, std::uint32_t version) noexcept {
    return static_cast<Global*>(wl_registry_bind(registry, name, GlobalInterface<Global>::value, version));
}

}  // namespace jgui::inline v001::detail

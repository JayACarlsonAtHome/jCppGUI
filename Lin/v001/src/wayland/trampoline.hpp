// File: src/wayland/trampoline.hpp
// Purpose: The one place the libwayland C boundary is crossed. libwayland stores an untyped pointer
//          with each listener and hands it back to a C function; this file casts it straight back to
//          the exact type it was registered with, and the pointer goes no further. This exception
//          ends when our own Wayland client replaces libwayland.
#pragma once

#include <cstdint>

#include <wayland-client.h>

namespace jgui::inline v001::detail {

// The C function for one noexcept member function. Requiring noexcept in the pattern means that a
// handler which might let an exception cross into libwayland does not compile.
template <auto Method>
struct Trampoline;

template <class Owner, class... Args, void (Owner::*Method)(Args...) noexcept>
struct Trampoline<Method> {
    static void call(void* data, Args... args) noexcept { (static_cast<Owner*>(data)->*Method)(args...); }
};

// Registers a listener with the owner it will be cast back to. Add is the generated
// <interface>_add_listener function, so the proxy and listener types are checked by the compiler.
// False when libwayland refused it, which leaves the object deaf to its events.
template <auto Add, class Proxy, class Listener, class Owner>
[[nodiscard]] bool listen(Proxy* proxy, const Listener& listener, Owner& owner) noexcept {
    return Add(proxy, &listener, &owner) == 0;
}

// wl_registry_bind returns the new object untyped. The interface argument decides its real type.
template <class Global>
[[nodiscard]] Global* bind_global(wl_registry* registry, std::uint32_t name, const wl_interface* interface,
                                  std::uint32_t version) noexcept {
    return static_cast<Global*>(wl_registry_bind(registry, name, interface, version));
}

}  // namespace jgui::inline v001::detail

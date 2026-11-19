// File: src/compat/rhel9/memfd_flags.hpp
// Purpose: MFD_NOEXEC_SEAL for Red Hat Enterprise Linux 9 alone. Its kernel headers (5.14) are older than the flag,
//          which came with Linux 6.3, so this gives the value <linux/memfd.h> gives. CMakeLists.txt puts this folder on
//          the include path on RHEL 9 and src/compat/system on every other OS, where the system's own value is used.
//          RHEL 9's kernel refuses the flag with EINVAL, so there create_buffer_file always takes the path for an old
//          kernel: this value lets the code build, and the kernel never accepts it.
#pragma once

namespace jgui::inline v001::detail {

inline constexpr unsigned int memfd_noexec_seal = 0x0008U;

}  // namespace jgui::inline v001::detail

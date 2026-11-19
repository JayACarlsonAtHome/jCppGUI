// File: src/compat/system/memfd_flags.hpp
// Purpose: MFD_NOEXEC_SEAL from the system's own headers, on every OS but Red Hat Enterprise Linux 9, whose headers are
//          older than the flag and which gets src/compat/rhel9 instead. CMakeLists.txt chooses the folder.
#pragma once

#include <sys/mman.h>  // for MFD_NOEXEC_SEAL

namespace jgui::inline v001::detail {

inline constexpr unsigned int memfd_noexec_seal = MFD_NOEXEC_SEAL;
static_assert(memfd_noexec_seal == 0x0008U, "src/compat/rhel9/memfd_flags.hpp gives Red Hat Enterprise Linux 9 this same value");

}  // namespace jgui::inline v001::detail

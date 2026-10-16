#pragma once
#include <atomic>
namespace RiscNvs {
namespace detail {
// Boot-lifetime state shared with background PHY startup. Production exposes
// no reset; tests simulate a new process only after reopening raw flash.
inline std::atomic<bool> mutationQuarantined{false};
}
inline bool quarantined(){return detail::mutationQuarantined.load();}
void quarantine();
}

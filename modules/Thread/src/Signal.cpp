#include "TK4CPP/ThreadPool/Signal.h"

namespace TK4CPP::TP::detail {

std::vector<const void*>& activeSlotsOnThisThread() {
    thread_local std::vector<const void*> active;
    return active;
}

} // namespace TK4CPP::TP::detail

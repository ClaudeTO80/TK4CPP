#include "TK4CPP/Logging/LogContext.h"

namespace TK4CPP::Logging {

namespace {
spdlog::source_loc toSourceLoc(const std::source_location& w) {
    return spdlog::source_loc{w.file_name(), static_cast<int>(w.line()), w.function_name()};
}
} // namespace

ScopedLogContext::ScopedLogContext(std::string context,
                                   std::string_view loggerName,
                                   std::source_location where)
    : _logger(LogManager::instance().getLogger(loggerName))
    , _context(std::move(context))
    , _where(where) {
    _logger->log(toSourceLoc(_where), spdlog::level::info, ">>> BEGIN [{}]", _context);
}

ScopedLogContext::~ScopedLogContext() {
    _logger->log(toSourceLoc(_where), spdlog::level::info, "<<< END   [{}] elapsed={}ms",
                 _context, elapsed().count());
}

std::chrono::milliseconds ScopedLogContext::elapsed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _start);
}

} // namespace TK4CPP::Logging

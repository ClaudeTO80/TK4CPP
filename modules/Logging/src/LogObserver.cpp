#include "TK4CPP/Logging/LogObserver.h"

#include "TK4CPP/Logging/LogManager.h"

namespace TK4CPP::Logging {

LogObserver::LogObserver(LogManager& manager) : _manager(manager) {}

LogObserver::~LogObserver() { unsubscribeAll(); }

LogObserver& LogObserver::onLog(LogHandler handler, Level minLevel, std::string loggerFilter) {
    _ids.push_back(_manager.subscribeLog(std::move(handler), minLevel, std::move(loggerFilter)));
    return *this;
}

LogObserver& LogObserver::onSystem(SystemHandler handler) {
    _ids.push_back(_manager.subscribeSystem(std::move(handler)));
    return *this;
}

void LogObserver::unsubscribeAll() noexcept {
    // Copy first: a callback running on this thread may re-enter and call us again.
    const auto ids = std::move(_ids);
    _ids.clear();
    for (const auto id : ids) _manager.unsubscribe(id);
}

} // namespace TK4CPP::Logging

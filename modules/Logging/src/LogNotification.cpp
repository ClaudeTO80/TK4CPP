#include "TK4CPP/Logging/LogNotification.h"

namespace TK4CPP::Logging {

LogNotification::LogNotification(Level level,
                                 std::string logger,
                                 std::string message,
                                 std::string file,
                                 int line,
                                 std::string func,
                                 std::chrono::system_clock::time_point timestamp)
    : _level(level)
    , _loggerName(std::move(logger))
    , _message(std::move(message))
    , _file(std::move(file))
    , _line(line)
    , _func(std::move(func))
    , _timestamp(timestamp) {}

Level LogNotification::level() const noexcept { return _level; }
const std::string& LogNotification::loggerName() const noexcept { return _loggerName; }
const std::string& LogNotification::message() const noexcept { return _message; }
const std::string& LogNotification::file() const noexcept { return _file; }
int LogNotification::line() const noexcept { return _line; }
const std::string& LogNotification::func() const noexcept { return _func; }
std::chrono::system_clock::time_point LogNotification::timestamp() const noexcept { return _timestamp; }

LogSystemNotification::LogSystemNotification(Event event, std::string detail)
    : _event(event), _detail(std::move(detail)) {}

LogSystemNotification::Event LogSystemNotification::event() const noexcept { return _event; }
const std::string& LogSystemNotification::detail() const noexcept { return _detail; }

} // namespace TK4CPP::Logging

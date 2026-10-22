#include "TK4CPP/Logging/LogConfig.h"

namespace TK4CPP::Logging {

LogConfig LogConfig::consoleOnly(Level level) {
    return LogConfig{}
        .withLevel(level)
        .addSink(SinkConfig{SinkConfig::Type::Console}
                     .withName("console")
                     .withLevel(level));
}

LogConfig LogConfig::fileAndConsole(const std::string& filePath, Level level) {
    return LogConfig{}
        .withLevel(level)
        .addSink(SinkConfig{SinkConfig::Type::Console}
                     .withName("console")
                     .withLevel(Level::Debug))
        .addSink(SinkConfig{SinkConfig::Type::RotatingFile}
                     .withName("file")
                     .withFile(filePath)
                     .withLevel(level)
                     .withMaxSize(20 * 1024 * 1024)
                     .withMaxFiles(7));
}

LogConfig LogConfig::production(const std::string& logDir, const std::string& appName) {
    return LogConfig{}
        .withLevel(Level::Info)
        .withAsync(16384)
        .withNotifications(true)
        .addSink(SinkConfig{SinkConfig::Type::Console}
                     .withName("console")
                     .withLevel(Level::Warn)
                     .withPattern("[%H:%M:%S] [%^%l%$] %v"))
        .addSink(SinkConfig{SinkConfig::Type::DailyFile}
                     .withName("daily")
                     .withFile(logDir + "/" + appName + ".log")
                     .withLevel(Level::Info))
        .addSink(SinkConfig{SinkConfig::Type::RotatingFile}
                     .withName("errors")
                     .withFile(logDir + "/" + appName + "_errors.log")
                     .withLevel(Level::Error)
                     .withMaxSize(50 * 1024 * 1024)
                     .withMaxFiles(10));
}

LogConfig LogConfig::development() {
    return LogConfig{}
        .withLevel(Level::Trace)
        .withPattern("[%H:%M:%S.%e] [%n] [%^%l%$] [%s:%#@%!] %v")
        .addSink(SinkConfig{SinkConfig::Type::Console}
                     .withName("console")
                     .withLevel(Level::Trace));
}

} // namespace TK4CPP::Logging

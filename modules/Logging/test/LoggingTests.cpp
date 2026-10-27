/**
 * @file LoggingTests.cpp
 * @brief Unit tests of the TK4CPP logging module.
 */

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>

#include "TK4CPP/Logging/LogLayer.h"

using namespace TK4CPP::Logging;
namespace fs = std::filesystem;

namespace {

/// Records everything delivered to a log callback (copies, because notifications are transient).
struct Collector {
    struct Rec {
        Level level;
        std::string logger, message, file, func;
        int line;
    };
    std::mutex mutex;
    std::vector<Rec> records;

    LogHandler handler() {
        return [this](const LogNotification& n) {
            std::lock_guard lock(mutex);
            records.push_back({n.level(), n.loggerName(), n.message(), n.file(), n.func(), n.line()});
        };
    }
    std::size_t size() {
        std::lock_guard lock(mutex);
        return records.size();
    }
};

/// Config with no output sinks: only observers see the records.
LogConfig observersOnly(Level level = Level::Trace) {
    return LogConfig{}.withLevel(level);
}

/// A sink writing to a string, handy to check formatting.
struct StringSink {
    /// The sink references #stream, so the manager must release it before the stream dies.
    ~StringSink() { LogManager::instance().shutdown(); }

    std::ostringstream stream;
    std::shared_ptr<spdlog::sinks::sink> sink =
        std::make_shared<spdlog::sinks::ostream_sink_mt>(stream, true);
    /// @brief Captured text with line endings normalised to "\n".
    /// @note spdlog terminates records with "\r\n" on Windows and "\n" elsewhere.
    std::string str() const {
        std::string text = stream.str();
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::size_t countLines(const std::string& s) {
    return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n'));
}

class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        LogManager::instance().shutdown();
        dir = fs::temp_directory_path() / ("tk4cpp_logging_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                                           "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override {
        LogManager::instance().shutdown();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    LogManager& mgr() { return LogManager::instance(); }
    fs::path dir;
};

} // namespace

// ── Observers ────────────────────────────────────────────────────────────────

TEST_F(LoggingTest, ObserverReceivesRecordWithMetadata) {
    mgr().init(observersOnly());
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog(c.handler());

    const int expectedLine = __LINE__ + 1;
    LL_WARN("core", "hello {} {}", 42, "world");

    ASSERT_EQ(c.size(), 1u);
    const auto& r = c.records[0];
    EXPECT_EQ(r.level, Level::Warn);
    EXPECT_EQ(r.logger, "core");
    EXPECT_EQ(r.message, "hello 42 world");
    EXPECT_EQ(r.line, expectedLine);
    EXPECT_NE(r.file.find("LoggingTests.cpp"), std::string::npos) << r.file;
    EXPECT_FALSE(r.func.empty());
}

TEST_F(LoggingTest, SuppressedLevelsAreNeitherNotifiedNorEvaluated) {
    mgr().init(observersOnly(Level::Info));
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog(c.handler());

    int evaluations = 0;
    LL_DEBUG("core", "value {}", ++evaluations);
    EXPECT_EQ(c.size(), 0u);
    EXPECT_EQ(evaluations, 0);

    LL_INFO("core", "value {}", ++evaluations);
    EXPECT_EQ(c.size(), 1u);
    EXPECT_EQ(evaluations, 1) << "arguments must be evaluated exactly once";
}

TEST_F(LoggingTest, ObserverReportsTheRealLevel) {
    mgr().init(observersOnly());
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog(c.handler());

    LL_TRACE("a", "t");
    LL_DEBUG("a", "d");
    LL_INFO("a", "i");
    LL_WARN("a", "w");
    LL_ERROR("a", "e");
    LL_CRITICAL("a", "c");

    ASSERT_EQ(c.size(), 6u);
    const Level expected[] = {Level::Trace, Level::Debug, Level::Info, Level::Warn, Level::Error, Level::Critical};
    for (std::size_t i = 0; i < 6; ++i)
        EXPECT_EQ(c.records[i].level, expected[i]) << i;
}

TEST_F(LoggingTest, MinLevelAndLoggerFilter) {
    mgr().init(observersOnly());
    Collector all, errorsOnly, onlyNet;
    auto obs = mgr().makeObserver();
    obs->onLog(all.handler())
        .onLog(errorsOnly.handler(), Level::Error)
        .onLog(onlyNet.handler(), Level::Trace, "net");

    LL_INFO("net", "a");
    LL_ERROR("net", "b");
    LL_ERROR("db", "c");

    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(errorsOnly.size(), 2u);
    ASSERT_EQ(onlyNet.size(), 2u);
    EXPECT_EQ(onlyNet.records[0].message, "a");
    EXPECT_EQ(onlyNet.records[1].message, "b");
}

TEST_F(LoggingTest, UnsubscribeStopsDelivery) {
    mgr().init(observersOnly());
    Collector c;
    {
        auto obs = mgr().makeObserver();
        obs->onLog(c.handler());
        EXPECT_TRUE(mgr().hasObservers());
        LL_INFO("x", "one");
        obs->unsubscribeAll();
        LL_INFO("x", "two");
        EXPECT_FALSE(mgr().hasObservers());
        obs->onLog(c.handler());
        LL_INFO("x", "three");
    } // destructor unsubscribes
    LL_INFO("x", "four");

    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c.records[0].message, "one");
    EXPECT_EQ(c.records[1].message, "three");
    EXPECT_FALSE(mgr().hasObservers());
}

TEST_F(LoggingTest, UnsubscribeUnknownIdReturnsFalse) {
    mgr().init(observersOnly());
    EXPECT_FALSE(mgr().unsubscribe(987654));
    const auto id = mgr().subscribeLog([](const LogNotification&) {});
    EXPECT_TRUE(mgr().unsubscribe(id));
    EXPECT_FALSE(mgr().unsubscribe(id));
}

TEST_F(LoggingTest, HandlerLoggingDoesNotRecurse) {
    mgr().init(observersOnly());
    int calls = 0;
    auto obs = mgr().makeObserver();
    obs->onLog([&](const LogNotification&) {
        ++calls;
        LL_INFO("core", "logged from the handler"); // would recurse forever without the guard
    });

    LL_INFO("core", "trigger");
    EXPECT_EQ(calls, 1);
}

TEST_F(LoggingTest, HandlerCanUnsubscribeItself) {
    mgr().init(observersOnly());
    int calls = 0;
    auto obs = mgr().makeObserver();
    obs->onLog([&](const LogNotification&) {
        ++calls;
        obs->unsubscribeAll();
    });

    LL_INFO("core", "first");
    LL_INFO("core", "second");
    EXPECT_EQ(calls, 1);
}

TEST_F(LoggingTest, ThrowingHandlerDoesNotAffectOthers) {
    mgr().init(observersOnly());
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog([](const LogNotification&) { throw std::runtime_error("boom"); });
    obs->onLog(c.handler());

    EXPECT_NO_THROW(LL_INFO("core", "msg"));
    EXPECT_EQ(c.size(), 1u);
}

TEST_F(LoggingTest, NotificationsCanBeDisabled) {
    mgr().init(observersOnly().withNotifications(false));
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog(c.handler());
    LL_INFO("core", "msg");
    mgr().notify(LogNotification(Level::Info, "core", "manual"));
    EXPECT_EQ(c.size(), 0u);
}

TEST_F(LoggingTest, ManualNotifyReachesObservers) {
    mgr().init(observersOnly());
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog(c.handler());
    mgr().notify(LogNotification(Level::Error, "ext", "injected", "f.cpp", 7, "fn"));
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c.records[0].message, "injected");
    EXPECT_EQ(c.records[0].line, 7);
}

TEST_F(LoggingTest, ConcurrentLoggingWithConcurrentSubscriptionChanges) {
    mgr().init(observersOnly());
    std::atomic<std::size_t> stable{0};
    auto stableObs = mgr().makeObserver();
    stableObs->onLog([&](const LogNotification&) { stable.fetch_add(1, std::memory_order_relaxed); });

    constexpr int threads = 8;
    constexpr int perThread = 2000;
    std::atomic<bool> stop{false};
    std::thread churn([&] {
        while (!stop) {
            auto o = mgr().makeObserver();
            o->onLog([](const LogNotification&) {});
        }
    });

    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([t] {
            for (int i = 0; i < perThread; ++i)
                LL_INFO("worker" + std::to_string(t % 3), "msg {}", i);
        });
    for (auto& w : workers) w.join();
    stop = true;
    churn.join();

    EXPECT_EQ(stable.load(), static_cast<std::size_t>(threads) * perThread);
}

// ── System notifications ─────────────────────────────────────────────────────

TEST_F(LoggingTest, SystemEventsAreEmittedInOrder) {
    using E = LogSystemNotification::Event;
    std::vector<std::pair<E, std::string>> events;
    const auto id = mgr().subscribeSystem([&](const LogSystemNotification& n) {
        events.emplace_back(n.event(), n.detail());
    });

    mgr().init(LogConfig{}.addSink(SinkConfig{}.withName("console").withLevel(Level::Off)));
    mgr().getLogger("core");
    mgr().setLevel("core", Level::Debug);
    mgr().setGlobalLevel(Level::Warn);
    mgr().flushAll();
    mgr().shutdown();
    mgr().unsubscribe(id);

    const std::vector<std::pair<E, std::string>> expected = {
        {E::SinkAdded, "console"},   {E::Initialized, "LogManager"}, {E::LoggerCreated, "core"},
        {E::LevelChanged, "core"},   {E::LevelChanged, "*"},         {E::FlushAll, ""},
        {E::LoggerDestroyed, "core"}, {E::Shutdown, "LogManager"}};
    EXPECT_EQ(events, expected);
}

// ── Sinks, patterns, loggers ─────────────────────────────────────────────────

TEST_F(LoggingTest, SinkPatternIsRespectedAndIncludesSourceLocation) {
    StringSink out;
    mgr().init(LogConfig{}.addSink(
        SinkConfig{}.withCustomSink(out.sink).withName("str").withPattern("%l|%s:%#|%v")));

    const int line = __LINE__ + 1;
    LL_INFO("core", "payload");
    mgr().flushAll();

    const std::string expected = "info|LoggingTests.cpp:" + std::to_string(line) + "|payload";
    EXPECT_NE(out.str().find(expected), std::string::npos) << out.str();
}

TEST_F(LoggingTest, PerSinkPatternsDiffer) {
    StringSink a, b;
    mgr().init(LogConfig{}
                   .addSink(SinkConfig{}.withCustomSink(a.sink).withPattern("A:%v"))
                   .addSink(SinkConfig{}.withCustomSink(b.sink).withPattern("B:%v")));
    LL_INFO("core", "x");
    mgr().flushAll();
    EXPECT_EQ(a.str(), "A:x\n");
    EXPECT_EQ(b.str(), "B:x\n");
}

TEST_F(LoggingTest, SinkLevelFiltersIndependentlyOfLogger) {
    StringSink warnOnly, everything;
    mgr().init(LogConfig{}
                   .withLevel(Level::Trace)
                   .addSink(SinkConfig{}.withCustomSink(warnOnly.sink).withLevel(Level::Warn).withPattern("%v"))
                   .addSink(SinkConfig{}.withCustomSink(everything.sink).withPattern("%v")));
    LL_INFO("core", "i");
    LL_ERROR("core", "e");
    mgr().flushAll();
    EXPECT_EQ(warnOnly.str(), "e\n");
    EXPECT_EQ(everything.str(), "i\ne\n");
}

TEST_F(LoggingTest, LoggersShareOneFileSink) {
    const auto path = dir / "shared.log";
    mgr().init(LogConfig{}.addSink(
        SinkConfig{SinkConfig::Type::RotatingFile}.withFile(path.string()).withPattern("%n:%v")));
    LL_INFO("a", "one");
    LL_INFO("b", "two");
    LL_INFO("a", "three");
    mgr().flushAll();

    EXPECT_EQ(slurp(path), "a:one\nb:two\na:three\n");
}

TEST_F(LoggingTest, FileAndDailyFileSinksWrite) {
    const auto plain = dir / "plain.log";
    const auto daily = dir / "daily.log";
    mgr().init(LogConfig{}
                   .addSink(SinkConfig{SinkConfig::Type::File}.withFile(plain.string()).withPattern("%v"))
                   .addSink(SinkConfig{SinkConfig::Type::DailyFile}.withFile(daily.string()).withPattern("%v")));
    LL_INFO("core", "hello");
    mgr().shutdown();

    EXPECT_EQ(slurp(plain), "hello\n");
    std::size_t dailyFiles = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind("daily_", 0) == 0) {
            ++dailyFiles;
            EXPECT_EQ(slurp(e.path()), "hello\n");
        }
    EXPECT_EQ(dailyFiles, 1u);
}

TEST_F(LoggingTest, FlushLevelWritesImmediately) {
    const auto path = dir / "flush.log";
    mgr().init(LogConfig{}
                   .withFlushLevel(Level::Error)
                   .addSink(SinkConfig{SinkConfig::Type::File}.withFile(path.string()).withPattern("%v")));
    LL_ERROR("core", "urgent");
    EXPECT_EQ(slurp(path), "urgent\n") << "an Error record must be on disk without an explicit flush";
}

TEST_F(LoggingTest, SetLevelAffectsOnlyThatLogger) {
    mgr().init(observersOnly(Level::Info));
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog(c.handler());

    mgr().setLevel("verbose", Level::Trace);
    LL_TRACE("verbose", "yes");
    LL_TRACE("quiet", "no");
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c.records[0].logger, "verbose");

    mgr().setGlobalLevel(Level::Error);
    LL_WARN("verbose", "no");
    LL_ERROR("quiet", "yes");
    EXPECT_EQ(c.size(), 2u);
}

TEST_F(LoggingTest, GetLoggerReturnsSameInstance) {
    mgr().init(observersOnly());
    EXPECT_EQ(mgr().getLogger("x"), mgr().getLogger("x"));
    EXPECT_NE(mgr().getLogger("x"), mgr().getLogger("y"));
    EXPECT_EQ(mgr().getLogger("x")->name(), "x");
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

TEST_F(LoggingTest, InitTwiceThrows) {
    mgr().init(observersOnly());
    EXPECT_THROW(mgr().init(observersOnly()), std::runtime_error);
}

TEST_F(LoggingTest, GetLoggerBeforeInitInitialisesConsoleAndAllowsExplicitInit) {
    EXPECT_FALSE(mgr().isInitialized());
    auto early = mgr().getLogger("early");
    EXPECT_TRUE(mgr().isInitialized());
    ASSERT_NE(early, nullptr);
    EXPECT_FALSE(early->sinks().empty());

    StringSink out;
    EXPECT_NO_THROW(mgr().init(LogConfig{}.addSink(SinkConfig{}.withCustomSink(out.sink).withPattern("%v"))));
    LL_INFO("early", "after init");
    mgr().flushAll();
    EXPECT_EQ(out.str(), "after init\n");
}

TEST_F(LoggingTest, ReinitialisationAfterShutdownWorksAndKeepsSubscriptions) {
    mgr().init(observersOnly());
    Collector c;
    auto obs = mgr().makeObserver();
    obs->onLog(c.handler());
    LL_INFO("core", "first");
    mgr().shutdown();
    EXPECT_FALSE(mgr().isInitialized());

    mgr().init(observersOnly());
    LL_INFO("core", "second");
    EXPECT_EQ(c.size(), 2u);
}

TEST_F(LoggingTest, FailedInitLeavesManagerUntouched) {
    EXPECT_THROW(mgr().init(LogConfig{}.addSink(SinkConfig{SinkConfig::Type::File})), std::runtime_error);
    EXPECT_FALSE(mgr().isInitialized());
    EXPECT_THROW(mgr().init(LogConfig{}.addSink(SinkConfig{SinkConfig::Type::Custom})), std::runtime_error);
    EXPECT_NO_THROW(mgr().init(observersOnly()));
}

#if defined(__cpp_lib_expected)
TEST_F(LoggingTest, TryInitReportsErrorsWithoutThrowing) {
    auto bad = mgr().tryInit(LogConfig{}.addSink(SinkConfig{SinkConfig::Type::File}.withName("f")));
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, LogError::Code::InvalidConfig);
    EXPECT_FALSE(mgr().isInitialized());

    {
        std::ofstream(dir / "file_in_the_way") << "x"; // a regular file where a directory is needed
    }
    auto unopenable = mgr().tryInit(LogConfig{}.addSink(
        SinkConfig{SinkConfig::Type::File}.withFile((dir / "file_in_the_way" / "y.log").string())));
    ASSERT_FALSE(unopenable.has_value());
    EXPECT_EQ(unopenable.error().code, LogError::Code::SinkCreationFailed);

    EXPECT_TRUE(mgr().tryInit(observersOnly()).has_value());
    auto again = mgr().tryInit(observersOnly());
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, LogError::Code::AlreadyInitialized);
}
#endif

// ── Async ────────────────────────────────────────────────────────────────────

TEST_F(LoggingTest, AsyncDeliversEverythingToSinksAndObserversByShutdown) {
    const auto path = dir / "async.log";
    mgr().init(LogConfig{}
                   .withAsync(256)
                   .addSink(SinkConfig{SinkConfig::Type::File}.withFile(path.string()).withPattern("%v")));
    std::atomic<std::size_t> observed{0};
    std::atomic<bool> allOnWorkerThread{true};
    const auto mainThread = std::this_thread::get_id();
    auto obs = mgr().makeObserver();
    obs->onLog([&](const LogNotification&) {
        ++observed;
        if (std::this_thread::get_id() == mainThread) allOnWorkerThread = false;
    });

    constexpr int n = 5000; // far more than the queue capacity: exercises the blocking policy
    for (int i = 0; i < n; ++i)
        LL_INFO("core", "line {}", i);
    mgr().shutdown();

    EXPECT_EQ(countLines(slurp(path)), static_cast<std::size_t>(n));
    EXPECT_EQ(observed.load(), static_cast<std::size_t>(n));
    EXPECT_TRUE(allOnWorkerThread.load());
}

// ── Scoped context ───────────────────────────────────────────────────────────

TEST_F(LoggingTest, ScopedContextLogsBeginAndEndAtTheCallSite) {
    StringSink out;
    mgr().init(LogConfig{}.addSink(SinkConfig{}.withCustomSink(out.sink).withPattern("%s:%# %v")));
    int line = 0;
    {
        line = __LINE__ + 1;
        LL_SCOPE("request-17");
        LL_INFO("default", "inside");
    }
    mgr().flushAll();

    const std::string prefix = "LoggingTests.cpp:" + std::to_string(line) + " ";
    const auto s = out.str();
    EXPECT_NE(s.find(prefix + ">>> BEGIN [request-17]"), std::string::npos) << s;
    EXPECT_NE(s.find(prefix + "<<< END   [request-17] elapsed="), std::string::npos) << s;
    EXPECT_LT(s.find("BEGIN"), s.find("inside"));
    EXPECT_LT(s.find("inside"), s.find("END"));
}

TEST_F(LoggingTest, TwoScopesInTheSameBlockCompile) {
    mgr().init(observersOnly());
    LL_SCOPE("outer");
    LL_SCOPE("inner", "other");
    SUCCEED();
}

// ── Configuration helpers ────────────────────────────────────────────────────

TEST(LogConfigFactories, ProduceExpectedSinks) {
    EXPECT_EQ(LogConfig::consoleOnly().sinks.size(), 1u);
    EXPECT_EQ(LogConfig::fileAndConsole("x.log").sinks.size(), 2u);
    const auto prod = LogConfig::production("/tmp", "app");
    EXPECT_EQ(prod.sinks.size(), 3u);
    EXPECT_TRUE(prod.asyncQueue);
    EXPECT_EQ(LogConfig::development().globalLevel, Level::Trace);
}

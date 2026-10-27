#include <TK4CPP/Scheduler/SqliteJobStore.h>

#include <sqlite3.h>

#include <initializer_list>
#include <span>
#include <variant>

namespace TK4CPP::Scheduler {
namespace {

using Param = std::variant<std::nullptr_t, std::int64_t, std::string_view>;

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

Param asInt(std::int64_t v) { return v; }
Param asText(std::string_view v) { return v; }
Param asOptMs(const std::optional<TimePoint>& tp) {
    return tp ? Param{toEpochMillis(*tp)} : Param{nullptr};
}

Error dbError(sqlite3* db, std::string_view context) {
    return Error{ErrorCode::StoreFailure, std::string(context) + ": " + sqlite3_errmsg(db)};
}

/// RAII prepared statement.
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        prepareRc_ = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] sqlite3_stmt* get() const noexcept { return stmt_; }

    [[nodiscard]] Result<void> bind(std::span<const Param> params) {
        if (prepareRc_ != SQLITE_OK) {
            return std::unexpected(dbError(db_, "prepare"));
        }
        int index = 1;
        for (const auto& p : params) {
            const int rc = std::visit(
                Overloaded{[&](std::nullptr_t) { return sqlite3_bind_null(stmt_, index); },
                           [&](std::int64_t v) { return sqlite3_bind_int64(stmt_, index, v); },
                           [&](std::string_view v) {
                               return sqlite3_bind_text(stmt_, index, v.empty() ? "" : v.data(),
                                                        static_cast<int>(v.size()), SQLITE_TRANSIENT);
                           }},
                p);
            if (rc != SQLITE_OK) {
                return std::unexpected(dbError(db_, "bind"));
            }
            ++index;
        }
        return {};
    }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_{nullptr};
    int prepareRc_{SQLITE_OK};
};

template <class RowFn>
Result<void> query(sqlite3* db, const char* sql, std::initializer_list<Param> params, RowFn&& onRow) {
    Statement st(db, sql);
    if (auto r = st.bind(std::span<const Param>(params.begin(), params.size())); !r) {
        return r;
    }
    for (;;) {
        const int rc = sqlite3_step(st.get());
        if (rc == SQLITE_ROW) {
            onRow(st.get());
        } else if (rc == SQLITE_DONE) {
            return {};
        } else {
            return std::unexpected(dbError(db, "step"));
        }
    }
}

Result<void> exec(sqlite3* db, const char* sql, std::initializer_list<Param> params = {}) {
    return query(db, sql, params, [](sqlite3_stmt*) {});
}

std::string colText(sqlite3_stmt* st, int col) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st, col));
    return p != nullptr ? std::string(p) : std::string();
}

std::optional<TimePoint> colOptMs(sqlite3_stmt* st, int col) {
    if (sqlite3_column_type(st, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return fromEpochMillis(sqlite3_column_int64(st, col));
}

/// RAII write transaction (BEGIN IMMEDIATE ... COMMIT, rollback on scope exit if not committed).
class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db), begin_(exec(db, "BEGIN IMMEDIATE")) {}
    ~Transaction() {
        if (begin_ && !done_) {
            (void)exec(db_, "ROLLBACK");
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    [[nodiscard]] const Result<void>& begun() const noexcept { return begin_; }
    [[nodiscard]] Result<void> commit() {
        auto r = exec(db_, "COMMIT");
        if (r) {
            done_ = true;
        }
        return r;
    }

private:
    sqlite3* db_;
    Result<void> begin_;
    bool done_{false};
};

Result<void> upsertTrigger(sqlite3* db, const Trigger& t) {
    const auto [kind, spec] = encodeSpec(t.spec);
    return exec(db,
                "INSERT INTO triggers(id, job_id, kind, spec, misfire, state, next_fire, last_fire, remaining) "
                "VALUES(?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(id) DO UPDATE SET job_id=excluded.job_id, kind=excluded.kind, "
                "spec=excluded.spec, misfire=excluded.misfire, state=excluded.state, "
                "next_fire=excluded.next_fire, last_fire=excluded.last_fire, remaining=excluded.remaining",
                {asText(t.id), asText(t.jobId), asText(kind), asText(spec),
                 asInt(static_cast<int>(t.misfire)), asInt(static_cast<int>(t.state)), asOptMs(t.nextFire),
                 asOptMs(t.lastFire), asInt(t.remaining)});
}

} // namespace

Result<std::shared_ptr<SqliteJobStore>> SqliteJobStore::open(const std::filesystem::path& path) {
    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(path.string().c_str(), &db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        auto err = dbError(db, "cannot open '" + path.string() + "'");
        sqlite3_close(db);
        return std::unexpected(std::move(err));
    }
    std::shared_ptr<SqliteJobStore> store(new SqliteJobStore(db));
    if (auto r = store->initSchema(); !r) {
        return std::unexpected(r.error());
    }
    return store;
}

SqliteJobStore::~SqliteJobStore() {
    sqlite3_close(db_);
}

Result<void> SqliteJobStore::initSchema() {
    sqlite3_busy_timeout(db_, 5000);
    for (const char* pragma : {"PRAGMA journal_mode=WAL", "PRAGMA synchronous=FULL", "PRAGMA foreign_keys=ON"}) {
        if (auto r = exec(db_, pragma); !r) {
            return r;
        }
    }
    static constexpr const char* kSchema[] = {
        "CREATE TABLE IF NOT EXISTS jobs("
        " id TEXT PRIMARY KEY, type TEXT NOT NULL, recovery INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS job_data("
        " job_id TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,"
        " k TEXT NOT NULL, v TEXT NOT NULL, PRIMARY KEY(job_id, k))",
        "CREATE TABLE IF NOT EXISTS triggers("
        " id TEXT PRIMARY KEY, job_id TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,"
        " kind TEXT NOT NULL, spec TEXT NOT NULL, misfire INTEGER NOT NULL, state INTEGER NOT NULL,"
        " next_fire INTEGER, last_fire INTEGER, remaining INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS inflight("
        " fire_id TEXT PRIMARY KEY, trigger_id TEXT NOT NULL, job_id TEXT NOT NULL, scheduled INTEGER NOT NULL)",
        "PRAGMA user_version=1"};
    std::lock_guard lock(mutex_);
    for (const char* sql : kSchema) {
        if (auto r = exec(db_, sql); !r) {
            return r;
        }
    }
    return {};
}

Result<StoreSnapshot> SqliteJobStore::load() {
    std::lock_guard lock(mutex_);
    StoreSnapshot snap;
    std::map<std::string, std::size_t> jobIndex;

    if (auto r = query(db_, "SELECT id, type, recovery FROM jobs ORDER BY id", {},
                       [&](sqlite3_stmt* st) {
                           JobDetail j;
                           j.id = colText(st, 0);
                           j.type = colText(st, 1);
                           j.requestsRecovery = sqlite3_column_int(st, 2) != 0;
                           jobIndex[j.id] = snap.jobs.size();
                           snap.jobs.push_back(std::move(j));
                       });
        !r) {
        return std::unexpected(r.error());
    }
    if (auto r = query(db_, "SELECT job_id, k, v FROM job_data", {},
                       [&](sqlite3_stmt* st) {
                           if (const auto it = jobIndex.find(colText(st, 0)); it != jobIndex.end()) {
                               snap.jobs[it->second].data[colText(st, 1)] = colText(st, 2);
                           }
                       });
        !r) {
        return std::unexpected(r.error());
    }

    std::optional<Error> decodeFailure;
    if (auto r = query(db_,
                       "SELECT id, job_id, kind, spec, misfire, state, next_fire, last_fire, remaining "
                       "FROM triggers ORDER BY id",
                       {},
                       [&](sqlite3_stmt* st) {
                           Trigger t;
                           t.id = colText(st, 0);
                           t.jobId = colText(st, 1);
                           auto spec = decodeSpec(colText(st, 2), colText(st, 3));
                           if (!spec) {
                               decodeFailure = spec.error();
                               return;
                           }
                           t.spec = std::move(*spec);
                           t.misfire = static_cast<MisfirePolicy>(sqlite3_column_int(st, 4));
                           t.state = static_cast<TriggerState>(sqlite3_column_int(st, 5));
                           t.nextFire = colOptMs(st, 6);
                           t.lastFire = colOptMs(st, 7);
                           t.remaining = sqlite3_column_int64(st, 8);
                           snap.triggers.push_back(std::move(t));
                       });
        !r) {
        return std::unexpected(r.error());
    }
    if (decodeFailure) {
        return std::unexpected(*decodeFailure);
    }

    if (auto r = query(db_, "SELECT fire_id, trigger_id, job_id, scheduled FROM inflight ORDER BY scheduled", {},
                       [&](sqlite3_stmt* st) {
                           snap.inflight.push_back(InflightFire{colText(st, 0), colText(st, 1), colText(st, 2),
                                                                fromEpochMillis(sqlite3_column_int64(st, 3))});
                       });
        !r) {
        return std::unexpected(r.error());
    }
    return snap;
}

Result<void> SqliteJobStore::putJob(const JobDetail& job) {
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    if (!tx.begun()) {
        return tx.begun();
    }
    // Upsert (not INSERT OR REPLACE): replacing the row would cascade-delete the job's triggers.
    if (auto r = exec(db_,
                      "INSERT INTO jobs(id, type, recovery) VALUES(?,?,?) "
                      "ON CONFLICT(id) DO UPDATE SET type=excluded.type, recovery=excluded.recovery",
                      {asText(job.id), asText(job.type), asInt(job.requestsRecovery ? 1 : 0)});
        !r) {
        return r;
    }
    if (auto r = exec(db_, "DELETE FROM job_data WHERE job_id=?", {asText(job.id)}); !r) {
        return r;
    }
    for (const auto& [key, value] : job.data) {
        if (auto r = exec(db_, "INSERT INTO job_data(job_id, k, v) VALUES(?,?,?)",
                          {asText(job.id), asText(key), asText(value)});
            !r) {
            return r;
        }
    }
    return tx.commit();
}

Result<void> SqliteJobStore::eraseJob(std::string_view jobId) {
    std::lock_guard lock(mutex_);
    return exec(db_, "DELETE FROM jobs WHERE id=?", {asText(jobId)}); // triggers/data cascade
}

Result<void> SqliteJobStore::putTrigger(const Trigger& trigger) {
    std::lock_guard lock(mutex_);
    return upsertTrigger(db_, trigger);
}

Result<void> SqliteJobStore::eraseTrigger(std::string_view triggerId) {
    std::lock_guard lock(mutex_);
    return exec(db_, "DELETE FROM triggers WHERE id=?", {asText(triggerId)});
}

Result<void> SqliteJobStore::beginFire(const std::optional<Trigger>& trigger,
                                       const std::optional<InflightFire>& inflight) {
    std::lock_guard lock(mutex_);
    Transaction tx(db_);
    if (!tx.begun()) {
        return tx.begun();
    }
    if (trigger) {
        if (auto r = upsertTrigger(db_, *trigger); !r) {
            return r;
        }
    }
    if (inflight) {
        if (auto r = exec(db_,
                          "INSERT OR REPLACE INTO inflight(fire_id, trigger_id, job_id, scheduled) VALUES(?,?,?,?)",
                          {asText(inflight->fireId), asText(inflight->triggerId), asText(inflight->jobId),
                           asInt(toEpochMillis(inflight->scheduled))});
            !r) {
            return r;
        }
    }
    return tx.commit();
}

Result<void> SqliteJobStore::endFire(std::string_view fireId) {
    std::lock_guard lock(mutex_);
    return exec(db_, "DELETE FROM inflight WHERE fire_id=?", {asText(fireId)});
}

} // namespace TK4CPP::Scheduler

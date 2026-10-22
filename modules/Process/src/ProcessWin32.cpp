/**
 * @file ProcessWin32.cpp
 * @brief Windows back-end: CreateProcessW + Job Object + explicit handle inheritance list.
 *
 * Design:
 *  - the executable is resolved explicitly (CreateProcessW does not search PATH when an
 *    application name is given), arguments are quoted with CommandLineToArgvW rules;
 *  - only the three standard handles are inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), so
 *    concurrent launches cannot leak each other's pipes;
 *  - the child is created suspended, put in a Job Object and only then resumed, so no
 *    descendant can escape the tree;
 *  - blocked reads/writes are interrupted with CancelIoEx when a descendant keeps a pipe open.
 */
#ifdef _WIN32

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ProcessImpl.hpp"

#include <algorithm>
#include <cwctype>

namespace TK4CPP::Process {
namespace {

using detail::fail;

/** @brief Move-only RAII Win32 handle (nullptr and INVALID_HANDLE_VALUE are both "empty"). */
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) noexcept : h_(h) {}
    Handle(Handle&& o) noexcept : h_(o.release()) {}
    Handle& operator=(Handle&& o) noexcept { if (this != &o) reset(o.release()); return *this; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    [[nodiscard]] HANDLE get() const noexcept { return h_; }
    [[nodiscard]] bool valid() const noexcept { return h_ && h_ != INVALID_HANDLE_VALUE; }
    explicit operator bool() const noexcept { return valid(); }
    HANDLE release() noexcept { HANDLE h = h_; h_ = nullptr; return h; }
    void reset(HANDLE h = nullptr) noexcept { if (valid()) CloseHandle(h_); h_ = h; }
private:
    HANDLE h_ = nullptr;
};

std::string win_error_text(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string result = "Windows error " + std::to_string(code);
    if (n && buffer) {
        std::wstring w(buffer, n);
        while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ')) w.pop_back();
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string u(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), u.data(), bytes, nullptr, nullptr);
            result += ": " + u;
        }
    }
    if (buffer) LocalFree(buffer);
    return result;
}

ErrorCode classify(DWORD e) {
    switch (e) {
        case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME: return ErrorCode::ExecutableNotFound;
        case ERROR_ACCESS_DENIED: case ERROR_ELEVATION_REQUIRED:                        return ErrorCode::PermissionDenied;
        case ERROR_DIRECTORY:                                                           return ErrorCode::WorkingDirectoryFailed;
        case ERROR_FILENAME_EXCED_RANGE: case ERROR_BAD_LENGTH:                         return ErrorCode::InvalidArgument;
        default:                                                                        return ErrorCode::SpawnFailed;
    }
}

Expected<std::wstring> to_wide(std::string_view s, const char* what) {
    if (s.empty()) return std::wstring{};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return fail(ErrorCode::InvalidArgument, 0, std::string(what) + " is not valid UTF-8");
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

/** @brief Quotes one argv element following the CommandLineToArgvW / MSVCRT rules. */
std::wstring quote_arg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += c; slashes = 0; continue; }
        out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}

bool ieq(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool is_file(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool has_extension(const std::wstring& p) {
    const auto slash = p.find_last_of(L"\\/");
    const auto dot = p.find_last_of(L'.');
    return dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
}

/** @brief Resolves the program like a shell would for ".exe": explicit path, else PATH lookup. */
Expected<std::wstring> find_executable(const std::wstring& exe) {
    auto try_one = [](const std::wstring& base) -> std::wstring {
        if (is_file(base)) return base;
        if (!has_extension(base) && is_file(base + L".exe")) return base + L".exe";
        return {};
    };
    if (exe.find_first_of(L"\\/:") != std::wstring::npos) {
        if (auto r = try_one(exe); !r.empty()) return r;
        return fail(ErrorCode::ExecutableNotFound, ERROR_FILE_NOT_FOUND, "Executable not found");
    }
    const DWORD need = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring path;
    if (need > 0) {
        path.resize(need);
        const DWORD got = GetEnvironmentVariableW(L"PATH", path.data(), need);
        path.resize(got);
    }
    std::size_t pos = 0;
    while (pos <= path.size()) {
        std::size_t end = path.find(L';', pos);
        if (end == std::wstring::npos) end = path.size();
        std::wstring dir = path.substr(pos, end - pos);
        pos = end + 1;
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
        if (dir.empty()) continue;
        if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
        if (auto r = try_one(dir + exe); !r.empty()) return r;
    }
    // Last resort: standard locations (application directory, system directories).
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = SearchPathW(nullptr, exe.c_str(), L".exe", static_cast<DWORD>(std::size(buf)), buf, nullptr);
    if (n > 0 && n < std::size(buf)) return std::wstring(buf, n);
    return fail(ErrorCode::ExecutableNotFound, ERROR_FILE_NOT_FOUND, "Executable not found in PATH");
}

bool is_batch_file(const std::wstring& p) {
    if (p.size() < 4) return false;
    const std::wstring_view ext(p.data() + p.size() - 4, 4);
    return ieq(ext, L".bat") || ieq(ext, L".cmd");
}

/** @brief Builds the sorted UTF-16 environment block (base + overrides - unsets). */
Expected<std::vector<wchar_t>> build_environment(const Options& o) {
    std::vector<std::wstring> entries;
    if (o.inherit_environment) {
        if (LPWCH env = GetEnvironmentStringsW()) {
            for (const wchar_t* p = env; *p; p += std::wcslen(p) + 1) entries.emplace_back(p);
            FreeEnvironmentStringsW(env);
        }
    }
    auto matches = [](const std::wstring& e, const std::wstring& name) {
        return e.size() > name.size() && e[name.size()] == L'=' && ieq(std::wstring_view(e.data(), name.size()), name);
    };
    for (const auto& u : o.unset_environment) {
        auto name = to_wide(u, "Environment variable name");
        if (!name) return std::unexpected(name.error());
        std::erase_if(entries, [&](const std::wstring& e) { return matches(e, *name); });
    }
    for (const auto& kv : o.environment) {
        const auto eq = kv.find('=');
        auto name = to_wide(kv.substr(0, eq), "Environment variable name");
        auto value = to_wide(kv.substr(eq + 1), "Environment variable value");
        if (!name) return std::unexpected(name.error());
        if (!value) return std::unexpected(value.error());
        auto it = std::find_if(entries.begin(), entries.end(), [&](const std::wstring& e) { return matches(e, *name); });
        const std::wstring full = *name + L"=" + *value;
        if (it == entries.end()) entries.push_back(full); else *it = full;
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> block;
    for (const auto& e : entries) { block.insert(block.end(), e.begin(), e.end()); block.push_back(L'\0'); }
    if (block.empty()) block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

struct AttrList {
    std::vector<char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    bool initialised = false;
    ~AttrList() { if (initialised) DeleteProcThreadAttributeList(list); }
};

} // namespace

// ============================================================================================
// FileSink
// ============================================================================================
namespace detail {

Expected<void> FileSink::open(const std::string& path, bool append) {
    close();
    auto w = to_wide(path, "Output file path");
    if (!w) return std::unexpected(w.error());
    HANDLE h = CreateFileW(w->c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        return fail(ErrorCode::OutputFileFailed, static_cast<int>(e), "Cannot open '" + path + "': " + win_error_text(e));
    }
    if (append) {
        LARGE_INTEGER zero{};
        SetFilePointerEx(h, zero, nullptr, FILE_END);
    }
    handle_ = h;
    return {};
}
bool FileSink::is_open() const noexcept { return handle_ != nullptr; }
bool FileSink::write(std::string_view data) noexcept {
    while (!data.empty()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size(), 1u << 20));
        if (!WriteFile(static_cast<HANDLE>(handle_), data.data(), chunk, &written, nullptr) || written == 0) return false;
        data.remove_prefix(written);
    }
    return true;
}
void FileSink::close() noexcept {
    if (handle_) { CloseHandle(static_cast<HANDLE>(handle_)); handle_ = nullptr; }
}

} // namespace detail

// ============================================================================================
// Native state
// ============================================================================================

struct Process::Impl::Native {
    Handle process, job, out_r, err_r;
    std::mutex in_mu;           ///< Guards in_w (closed by the writer, cancelled by the monitor).
    Handle in_w;
    DWORD pid = 0;
    std::thread out_t, err_t, in_t;
    std::atomic<bool> stop{false};
    detail::Latch io_done;
};

namespace {

void reader_main(Process::Impl& self, Process::Impl::Native& n, HANDLE h, bool is_err, detail::Latch& latch) {
    struct Done { detail::Latch& l; ~Done() { l.count_down(); } } done{latch};
    detail::tl_worker_owner = &self;
    std::vector<char> buf(64 * 1024);
    while (!n.stop.load()) {
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
            const DWORD e = GetLastError();
            if (e != ERROR_BROKEN_PIPE && e != ERROR_OPERATION_ABORTED && e != ERROR_HANDLE_EOF && e != ERROR_NO_DATA)
                self.note_error({ErrorCode::PipeFailed, static_cast<int>(e), "ReadFile: " + win_error_text(e)});
            break;
        }
        if (got) self.consume(std::string_view(buf.data(), got), is_err);
    }
    // Abandoned (stop) or finished: pick up whatever is still buffered in the pipe.
    DWORD avail = 0;
    while (n.stop.load() && PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), std::min<DWORD>(avail, static_cast<DWORD>(buf.size())), &got, nullptr) || got == 0) break;
        self.consume(std::string_view(buf.data(), got), is_err);
    }
}

void writer_main(Process::Impl& self, Process::Impl::Native& n) {
    struct Done { detail::Latch& l; ~Done() { l.count_down(); } } done{n.io_done};
    detail::tl_worker_owner = &self;
    std::string chunk;
    bool broken = false;
    while (!broken && self.stdin_queue.wait_take(chunk)) {
        std::size_t off = 0;
        while (off < chunk.size()) {
            DWORD w = 0;
            const DWORD len = static_cast<DWORD>(std::min<std::size_t>(chunk.size() - off, 64 * 1024));
            if (!WriteFile(n.in_w.get(), chunk.data() + off, len, &w, nullptr) || w == 0) { broken = true; break; }
            off += w;
            self.stdin_queue.note_sent(w);
        }
        chunk.clear();
    }
    if (broken) self.stdin_queue.shut();
    std::lock_guard<std::mutex> l(n.in_mu);
    n.in_w.reset();                       // child sees EOF
}

struct CloseCtx { const std::vector<DWORD>* pids; int posted; };
BOOL CALLBACK close_windows_proc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<CloseCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (std::find(ctx->pids->begin(), ctx->pids->end(), pid) != ctx->pids->end())
        if (PostMessageW(hwnd, WM_CLOSE, 0, 0)) ++ctx->posted;
    return TRUE;
}

} // namespace

// ============================================================================================
// Impl hooks
// ============================================================================================

Expected<void> Process::Impl::platform_start() {
    const Options& o = options_;
    auto nat = std::make_unique<Native>();
    Native& n = *nat;

    // ---- resolve and convert everything first: nothing to clean up on early failure --------------
    auto exe_w = to_wide(executable_, "Executable path");
    if (!exe_w) return std::unexpected(exe_w.error());
    auto resolved = find_executable(*exe_w);
    if (!resolved) return std::unexpected(resolved.error());
    std::vector<std::wstring> args_w;
    for (const auto& a : arguments_) {
        auto w = to_wide(a, "Argument");
        if (!w) return std::unexpected(w.error());
        args_w.push_back(std::move(*w));
    }
    if (is_batch_file(*resolved)) {
        // cmd.exe re-parses the command line: refuse anything that could be interpreted as syntax.
        for (const auto& a : args_w)
            if (a.find_first_of(L"\"%^&|<>\r\n") != std::wstring::npos)
                return fail(ErrorCode::InvalidArgument, 0,
                            "Argument contains characters that cannot be passed safely to a .bat/.cmd file");
    }
    std::wstring command = quote_arg(*exe_w);
    for (const auto& a : args_w) { command += L' '; command += quote_arg(a); }
    if (command.size() >= 32767) return fail(ErrorCode::InvalidArgument, 0, "Command line is too long (limit 32766 characters)");
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    std::wstring cwd;
    if (!o.working_directory.empty()) {
        auto w = to_wide(o.working_directory, "Working directory");
        if (!w) return std::unexpected(w.error());
        const DWORD attr = GetFileAttributesW(w->c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
            return fail(ErrorCode::WorkingDirectoryFailed, static_cast<int>(GetLastError()),
                        "Working directory '" + o.working_directory + "' is not an accessible directory");
        cwd = std::move(*w);
    }
    std::vector<wchar_t> env_block;
    const bool custom_env = !o.inherit_environment || !o.environment.empty() || !o.unset_environment.empty();
    if (custom_env) {
        auto b = build_environment(o);
        if (!b) return std::unexpected(b.error());
        env_block = std::move(*b);
    }

    // ---- pipes -----------------------------------------------------------------------------------
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    auto pipe_fail = [&](const char* what) {
        const DWORD e = GetLastError();
        return fail(ErrorCode::PipeFailed, static_cast<int>(e), std::string("Cannot create ") + what + " pipe: " + win_error_text(e));
    };
    Handle out_w, err_w, in_r;
    {
        HANDLE r = nullptr, w = nullptr;
        if (!CreatePipe(&r, &w, &sa, 64 * 1024)) return pipe_fail("stdout");
        n.out_r.reset(r); out_w.reset(w);
        if (!SetHandleInformation(n.out_r.get(), HANDLE_FLAG_INHERIT, 0)) return pipe_fail("stdout");
    }
    if (!o.merge_stderr_to_stdout) {
        HANDLE r = nullptr, w = nullptr;
        if (!CreatePipe(&r, &w, &sa, 64 * 1024)) return pipe_fail("stderr");
        n.err_r.reset(r); err_w.reset(w);
        if (!SetHandleInformation(n.err_r.get(), HANDLE_FLAG_INHERIT, 0)) return pipe_fail("stderr");
    }
    const StdinMode in_mode = wants_stdin_pipe() ? StdinMode::Pipe : o.stdin_mode;
    if (in_mode == StdinMode::Pipe) {
        HANDLE r = nullptr, w = nullptr;
        if (!CreatePipe(&r, &w, &sa, 64 * 1024)) return pipe_fail("stdin");
        in_r.reset(r); n.in_w.reset(w);
        if (!SetHandleInformation(n.in_w.get(), HANDLE_FLAG_INHERIT, 0)) return pipe_fail("stdin");
    } else if (in_mode == StdinMode::Inherit) {
        HANDLE cur = GetStdHandle(STD_INPUT_HANDLE), dup = nullptr;
        if (cur && cur != INVALID_HANDLE_VALUE &&
            DuplicateHandle(GetCurrentProcess(), cur, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS))
            in_r.reset(dup);
    }
    if (!in_r) {   // Null mode, or no usable parent stdin
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nul == INVALID_HANDLE_VALUE) return pipe_fail("NUL");
        in_r.reset(nul);
    }
    HANDLE child_err = o.merge_stderr_to_stdout ? out_w.get() : err_w.get();

    // ---- startup info with an explicit inheritance list -------------------------------------------
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in_r.get();
    si.StartupInfo.hStdOutput = out_w.get();
    si.StartupInfo.hStdError = child_err;
    AttrList attrs;
    HANDLE inherit_list[3];
    DWORD inherit_count = 0;
    DWORD flags = CREATE_SUSPENDED | (o.create_no_window ? CREATE_NO_WINDOW : 0) | (custom_env ? CREATE_UNICODE_ENVIRONMENT : 0);
    if (o.close_inherited_handles) {
        inherit_list[inherit_count++] = in_r.get();
        inherit_list[inherit_count++] = out_w.get();
        if (child_err != out_w.get()) inherit_list[inherit_count++] = child_err;
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attrs.storage.resize(size);
        attrs.list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.storage.data());
        if (!InitializeProcThreadAttributeList(attrs.list, 1, 0, &size)) {
            const DWORD e = GetLastError();
            return fail(ErrorCode::SpawnFailed, static_cast<int>(e), "InitializeProcThreadAttributeList: " + win_error_text(e));
        }
        attrs.initialised = true;
        if (!UpdateProcThreadAttribute(attrs.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit_list,
                                       inherit_count * sizeof(HANDLE), nullptr, nullptr)) {
            const DWORD e = GetLastError();
            return fail(ErrorCode::SpawnFailed, static_cast<int>(e), "UpdateProcThreadAttribute: " + win_error_text(e));
        }
        si.lpAttributeList = attrs.list;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    // ---- job object (created before the process so a failure costs nothing) -----------------------
    const bool want_tree = o.tree_policy != TreePolicy::None;
    if (want_tree) {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            n.job.reset(j);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
            if (o.kill_tree_on_exit) lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(n.job.get(), JobObjectExtendedLimitInformation, &lim, sizeof lim)) n.job.reset();
        }
        if (!n.job && o.tree_policy == TreePolicy::Required) {
            const DWORD e = GetLastError();
            return fail(ErrorCode::ProcessTreeFailed, static_cast<int>(e), "Cannot create Job Object: " + win_error_text(e));
        }
    }

    // ---- launch ---------------------------------------------------------------------------------------
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(resolved->c_str(), mutable_command.data(), nullptr, nullptr, TRUE, flags,
                                   custom_env ? env_block.data() : nullptr, cwd.empty() ? nullptr : cwd.c_str(),
                                   &si.StartupInfo, &pi);
    const DWORD launch_error = GetLastError();
    out_w.reset(); err_w.reset(); in_r.reset();      // child owns its ends now
    if (!ok)
        return fail(classify(launch_error), static_cast<int>(launch_error),
                    "Cannot execute '" + executable_ + "': " + win_error_text(launch_error));
    n.process.reset(pi.hProcess);
    Handle main_thread(pi.hThread);
    n.pid = pi.dwProcessId;

    auto abort_child = [&](ErrorCode code, DWORD e, const std::string& msg) {
        TerminateProcess(n.process.get(), 1);
        WaitForSingleObject(n.process.get(), 5000);
        return fail(code, static_cast<int>(e), msg);
    };
    bool tree = false;
    if (n.job) {
        if (AssignProcessToJobObject(n.job.get(), n.process.get())) tree = true;
        else {
            const DWORD e = GetLastError();
            if (o.tree_policy == TreePolicy::Required)
                return abort_child(ErrorCode::ProcessTreeFailed, e, "Cannot assign process to Job Object: " + win_error_text(e));
            n.job.reset();   // degrade to direct-child control (e.g. Windows 7 nested-job restrictions)
        }
    }
    if (ResumeThread(main_thread.get()) == static_cast<DWORD>(-1)) {
        const DWORD e = GetLastError();
        return abort_child(ErrorCode::SpawnFailed, e, "ResumeThread: " + win_error_text(e));
    }

    pid_ = static_cast<long long>(n.pid);
    tree_managed_ = tree;

    try {
        n.io_done.add(1 + (o.merge_stderr_to_stdout ? 0 : 1) + (n.in_w ? 1 : 0));
        n.out_t = std::thread([this, &n] { reader_main(*this, n, n.out_r.get(), false, n.io_done); });
        if (n.err_r) n.err_t = std::thread([this, &n] { reader_main(*this, n, n.err_r.get(), true, n.io_done); });
        if (n.in_w) n.in_t = std::thread([this, &n] { writer_main(*this, n); });
    } catch (const std::exception& ex) {
        n.stop = true;
        if (n.job) TerminateJobObject(n.job.get(), 1); else TerminateProcess(n.process.get(), 1);
        WaitForSingleObject(n.process.get(), 5000);
        CancelIoEx(n.out_r.get(), nullptr);
        if (n.err_r) CancelIoEx(n.err_r.get(), nullptr);
        for (std::thread* t : {&n.out_t, &n.err_t, &n.in_t}) if (t->joinable()) t->join();
        return fail(ErrorCode::SpawnFailed, 0, std::string("Cannot create I/O thread: ") + ex.what());
    }
    native_ = nat.release();
    return {};
}

void Process::Impl::platform_wait_exit() {
    WaitForSingleObject(native_->process.get(), INFINITE);
}

bool Process::Impl::platform_terminate_locked() {
    Native& n = *native_;
    std::vector<DWORD> pids{n.pid};
    if (n.job) {
        std::vector<char> buf(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 511 * sizeof(ULONG_PTR));
        auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
        if (QueryInformationJobObject(n.job.get(), JobObjectBasicProcessIdList, list, static_cast<DWORD>(buf.size()), nullptr) ||
            GetLastError() == ERROR_MORE_DATA) {
            pids.clear();
            for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) pids.push_back(static_cast<DWORD>(list->ProcessIdList[i]));
        }
    }
    CloseCtx ctx{&pids, 0};
    EnumWindows(close_windows_proc, reinterpret_cast<LPARAM>(&ctx));
    // Console/windowless programs have no portable graceful signal: report "not possible" so the
    // caller escalates immediately instead of idling for the whole grace period.
    return ctx.posted > 0;
}

void Process::Impl::platform_kill_locked() {
    Native& n = *native_;
    const BOOL ok = n.job ? TerminateJobObject(n.job.get(), 1) : TerminateProcess(n.process.get(), 1);
    if (!ok) {
        const DWORD e = GetLastError();
        if (e != ERROR_ACCESS_DENIED)    // access denied == already exiting
            note_error({ErrorCode::SpawnFailed, static_cast<int>(e), "Terminate: " + win_error_text(e)});
    }
}

ExitStatus Process::Impl::platform_reap_locked() {
    ExitStatus es;
    DWORD code = 0;
    if (GetExitCodeProcess(native_->process.get(), &code)) {
        es.kind = ExitStatus::Kind::Exited;
        es.code = static_cast<int>(code);    // NTSTATUS crash codes (0xC0000005 ...) appear as negative ints
    } else {
        const DWORD e = GetLastError();
        note_error({ErrorCode::WaitFailed, static_cast<int>(e), "GetExitCodeProcess: " + win_error_text(e)});
    }
    return es;
}

bool Process::Impl::platform_drain(std::chrono::milliseconds timeout) {
    if (!native_) return true;
    Native& n = *native_;
    bool clean = n.io_done.wait_for(timeout);
    if (!clean) {
        n.stop.store(true);
        for (int i = 0; i < 100; ++i) {      // repeat: a cancel can race with a read that has not started yet
            CancelIoEx(n.out_r.get(), nullptr);
            if (n.err_r) CancelIoEx(n.err_r.get(), nullptr);
            {
                std::lock_guard<std::mutex> l(n.in_mu);
                if (n.in_w) CancelIoEx(n.in_w.get(), nullptr);
            }
            if (n.io_done.wait_for(std::chrono::milliseconds(50))) break;
        }
    }
    for (std::thread* t : {&n.out_t, &n.err_t, &n.in_t}) if (t->joinable()) t->join();
    return clean;
}

void Process::Impl::platform_wake_stdin() {}   // the writer thread waits on the queue itself

void Process::Impl::platform_release() {
    if (!native_) return;
    Native& n = *native_;
    if (n.out_t.joinable() || n.err_t.joinable() || n.in_t.joinable()) return;   // monitor releases after joining
    n.out_r.reset();
    n.err_r.reset();
    {
        std::lock_guard<std::mutex> l(n.in_mu);
        n.in_w.reset();
    }
    n.process.reset();
    n.job.reset();                       // KILL_ON_JOB_CLOSE (if enabled) sweeps leftover descendants
}

void Process::Impl::platform_destroy_native() {
    delete native_;
    native_ = nullptr;
}

} // namespace TK4CPP::Process

#endif // _WIN32

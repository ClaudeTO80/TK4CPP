#include <TK4CPP/Utility/FolderHandler.hpp>

#include <algorithm>
#include <format>
#include <future>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

/// Report a message through the log callback; arguments are evaluated only if it will be delivered.
#define FH_LOG(level, ...)                                                        \
    do {                                                                          \
        if (m_log && (level) >= m_logMinLevel)                                    \
            m_log((level), std::format(__VA_ARGS__));                             \
    } while (0)

namespace TK4CPP::Utility {

namespace fs = std::filesystem;

namespace {

/**
 * @brief Minimal glob matcher supporting '*' and '?' (portable replacement for POSIX fnmatch).
 * @param pattern Glob pattern.
 * @param str     String to test.
 * @return True if @p str matches @p pattern.
 */
bool wildcardMatch(std::string const& pattern, std::string const& str)
{
    std::size_t p = 0, s = 0, star = std::string::npos, match = 0;
    while (s < str.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s])) {
            ++p; ++s;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star  = p++;
            match = s;
        } else if (star != std::string::npos) {
            p = star + 1;
            s = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

/// True if the file name of @p p starts with '.'.
bool isHidden(fs::path const& p)
{
    auto const n = p.filename().string();
    return !n.empty() && n[0] == '.';
}

/// True if any component of the relative path @p rel is a hidden name.
bool hasHiddenComponent(fs::path const& rel)
{
    for (auto const& comp : rel) {
        auto const s = comp.string();
        if (s.size() > 1 && s[0] == '.' && s != "..")
            return true;
    }
    return false;
}

/// True if the traversal may go below @p depth given the filter's maxDepth.
bool canDescend(FolderFilter const& f, int depth)
{
    return f.maxDepth < 0 || depth < f.maxDepth;
}

/**
 * @brief Copy a single non-directory entry.
 * @param skipExisting If true an existing target is silently kept when @p overwrite is false;
 *                     otherwise it is reported as std::errc::file_exists.
 * @param followSymlinks If false a symlink is recreated as a symlink, otherwise its target is copied.
 */
void copyOne(fs::directory_entry const& entry, fs::path const& target,
             bool overwrite, bool followSymlinks, bool skipExisting, std::error_code& ec)
{
    ec.clear();
    bool const targetExists = fs::exists(fs::symlink_status(target, ec));
    ec.clear();

    if (targetExists && !overwrite) {
        if (!skipExisting)
            ec = std::make_error_code(std::errc::file_exists);
        return;
    }

    if (!followSymlinks && entry.is_symlink(ec)) {
        if (targetExists) {
            fs::remove(target, ec);
            if (ec) return;
        }
        fs::copy_symlink(entry.path(), target, ec);
    } else {
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
    }
}

/// Throw std::logic_error if the root has not been set.
void requireRoot(fs::path const& root)
{
    if (root.empty())
        throw std::logic_error("FolderHandler: m_root not set. "
                               "Use setRoot() or FolderHandler::at().");
}

} // namespace

// ===========================================================================
// Construction / configuration
// ===========================================================================

FolderHandler::FolderHandler(fs::path root, bool throwOnError)
    : m_root(std::move(root))
    , m_throwOnError(throwOnError)
{
}

FolderHandler FolderHandler::at(fs::path root)
{
    return FolderHandler(std::move(root));
}

void     FolderHandler::setLogCallback (LogCallback cb, LogLevel minLevel)
{ m_log = std::move(cb); m_logMinLevel = minLevel; }
void     FolderHandler::setThrowOnError(bool value)   { m_throwOnError = value; }
void     FolderHandler::setRoot        (fs::path root){ m_root         = std::move(root); }
fs::path FolderHandler::root() const                  { return m_root; }

// ===========================================================================
// Internal helpers
// ===========================================================================

bool FolderHandler::reportError(std::error_code const& ec, std::string const& context) const
{
    if (!ec) return true;

    FH_LOG(LogLevel::Error, "{}: {}", context, ec.message());

    if (m_throwOnError)
        throw fs::filesystem_error(context, ec);

    return false;
}

bool FolderHandler::matchFilter(fs::path const& path, FolderFilter const& filter, int depth)
{
    if (depth < filter.minDepth)
        return false;
    if (filter.maxDepth >= 0 && depth > filter.maxDepth)
        return false;

    std::string const filename = path.filename().string();

    if (filter.excludeHidden && !filename.empty() && filename[0] == '.')
        return false;

    if (!filter.namePattern.empty() && !wildcardMatch(filter.namePattern, filename))
        return false;

    if (filter.predicate && !filter.predicate(path))
        return false;

    return true;
}

// ===========================================================================
// Creation / deletion
// ===========================================================================

bool FolderHandler::create(fs::path const& path, bool createParents)
{
    std::error_code ec;
    FH_LOG(LogLevel::Debug, "create: {}", path.string());

    if (createParents)
        fs::create_directories(path, ec);
    else
        fs::create_directory(path, ec);

    return reportError(ec, "create(" + path.string() + ")");
}

bool FolderHandler::remove(fs::path const& path, bool recursive)
{
    std::error_code ec;
    FH_LOG(LogLevel::Debug, "remove: {}{}", path.string(), recursive ? " [recursive]" : "");

    if (!fs::exists(path, ec))
        return true;   // already absent: success

    if (!fs::is_directory(path, ec)) {
        ec = std::make_error_code(std::errc::not_a_directory);
        return reportError(ec, "remove(" + path.string() + ")");
    }

    if (recursive)
        fs::remove_all(path, ec);
    else
        fs::remove(path, ec);

    return reportError(ec, "remove(" + path.string() + ")");
}

bool FolderHandler::clear(fs::path const& path)
{
    std::error_code ec;
    FH_LOG(LogLevel::Debug, "clear: {}", path.string());

    if (!fs::is_directory(path, ec)) {
        ec = std::make_error_code(std::errc::not_a_directory);
        return reportError(ec, "clear(" + path.string() + ")");
    }

    for (auto const& entry : fs::directory_iterator(path, ec)) {
        fs::remove_all(entry.path(), ec);
        if (ec)
            return reportError(ec, "clear/remove_all(" + entry.path().string() + ")");
    }
    return reportError(ec, "clear/iterate(" + path.string() + ")");
}

bool FolderHandler::ensureExists(fs::path const& path)
{
    std::error_code ec;
    if (fs::exists(path, ec) && fs::is_directory(path, ec))
        return true;
    return create(path, /*createParents=*/true);
}

// ===========================================================================
// Copy / move
// ===========================================================================

bool FolderHandler::copyRecursive(fs::path const& src,
                                  fs::path const& dst,
                                  CopyOptions const& opts) const
{
    std::error_code ec;

    auto const dirOpts = opts.followSymlinks ? fs::directory_options::follow_directory_symlink
                                             : fs::directory_options::none;
    fs::recursive_directory_iterator const end;

    for (fs::recursive_directory_iterator it(src, dirOpts, ec); !ec && it != end; it.increment(ec)) {
        auto const& entry = *it;

        // Depth 1 = direct children of src (same convention as walk())
        auto rel   = entry.path().lexically_relative(src);
        int  depth = static_cast<int>(std::distance(rel.begin(), rel.end()));

        if (opts.filter) {
            if (opts.filter->excludeHidden && hasHiddenComponent(rel)) continue;
            if (!matchFilter(entry.path(), *opts.filter, depth))       continue;
        }

        fs::path target     = dst / rel;
        bool     copiedFile = false;

        if (!opts.followSymlinks && entry.is_symlink(ec)) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) copyOne(entry, target, opts.overwrite, false, /*skipExisting=*/false, ec);
        } else if (entry.is_directory(ec)) {
            fs::create_directories(target, ec);
        } else {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) copyOne(entry, target, opts.overwrite, true, /*skipExisting=*/false, ec);
            copiedFile = !ec;
        }

        if (ec) return reportError(ec, "copyRecursive/copy(" + target.string() + ")");

        if (opts.preserveTimestamps && copiedFile) {
            auto t = fs::last_write_time(entry.path(), ec);
            if (!ec) fs::last_write_time(target, t, ec);
        }
    }
    return reportError(ec, "copyRecursive/iterate(" + src.string() + ")");
}

bool FolderHandler::copy(fs::path const& src, fs::path const& dst, CopyOptions const& opts)
{
    std::error_code ec;
    FH_LOG(LogLevel::Debug, "copy: {} -> {}", src.string(), dst.string());

    if (!fs::exists(src, ec)) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return reportError(ec, "copy: source not found");
    }

    fs::create_directories(dst, ec);
    if (ec) return reportError(ec, "copy: cannot create dst");

    if (!opts.recursive) {
        // Direct children only
        for (auto const& entry : fs::directory_iterator(src, ec)) {
            if (opts.filter && !matchFilter(entry.path(), *opts.filter, 1))
                continue;
            fs::copy_options fsCopyOpts = opts.overwrite
                ? fs::copy_options::overwrite_existing
                : fs::copy_options::none;
            if (!opts.followSymlinks)
                fsCopyOpts |= fs::copy_options::copy_symlinks;
            fs::copy(entry.path(), dst / entry.path().filename(), fsCopyOpts, ec);
            if (ec) return reportError(ec, "copy/file(" + entry.path().string() + ")");
        }
        return true;
    }

    return copyRecursive(src, dst, opts);
}

bool FolderHandler::move(fs::path const& src, fs::path const& dst, bool overwrite)
{
    std::error_code ec;
    FH_LOG(LogLevel::Debug, "move: {} -> {}", src.string(), dst.string());

    if (fs::exists(dst, ec)) {
        if (!overwrite) {
            ec = std::make_error_code(std::errc::file_exists);
            return reportError(ec, "move: destination exists");
        }
        fs::remove_all(dst, ec);
        if (ec) return reportError(ec, "move: cannot remove dst");
    }

    fs::create_directories(dst.parent_path(), ec);
    fs::rename(src, dst, ec);
    return reportError(ec, "move(" + src.string() + " -> " + dst.string() + ")");
}

bool FolderHandler::merge(fs::path const& src, fs::path const& dst, CopyOptions const& opts)
{
    std::error_code ec;
    FH_LOG(LogLevel::Debug, "merge: {} -> {}", src.string(), dst.string());

    if (!fs::is_directory(src, ec)) {
        ec = std::make_error_code(std::errc::not_a_directory);
        return reportError(ec, "merge: src is not a directory");
    }

    fs::create_directories(dst, ec);
    if (ec) return reportError(ec, "merge: cannot create dst");

    auto const dirOpts = opts.followSymlinks ? fs::directory_options::follow_directory_symlink
                                             : fs::directory_options::none;
    fs::recursive_directory_iterator const end;

    for (fs::recursive_directory_iterator it(src, dirOpts, ec); !ec && it != end; it.increment(ec)) {
        auto const& entry = *it;

        auto     rel    = entry.path().lexically_relative(src);
        int      depth  = static_cast<int>(std::distance(rel.begin(), rel.end()));
        fs::path target = dst / rel;

        if (opts.filter) {
            if (opts.filter->excludeHidden && hasHiddenComponent(rel)) continue;
            if (!matchFilter(entry.path(), *opts.filter, depth))       continue;
        }

        if (!opts.followSymlinks && entry.is_symlink(ec)) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) copyOne(entry, target, opts.overwrite, false, /*skipExisting=*/true, ec);
        } else if (entry.is_directory(ec)) {
            fs::create_directories(target, ec);
        } else {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) copyOne(entry, target, opts.overwrite, true, /*skipExisting=*/true, ec);
        }
        if (ec) return reportError(ec, "merge/copy(" + target.string() + ")");
    }
    return reportError(ec, "merge/iterate(" + src.string() + ")");
}

// ===========================================================================
// Analysis / query
// ===========================================================================

bool FolderHandler::exists(fs::path const& path) const
{
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

bool FolderHandler::isEmpty(fs::path const& path) const
{
    std::error_code ec;
    return fs::is_empty(path, ec) && !ec;
}

std::vector<fs::path> FolderHandler::listFolders(fs::path const& path) const
{
    FH_LOG(LogLevel::Debug, "listFolders: {}", path.string());

    std::vector<fs::path> result;
    std::error_code ec;

    if (!fs::is_directory(path, ec))
        return result;

    fs::directory_iterator const end;
    for (fs::directory_iterator it(path, ec); !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_directory(entryEc))
            result.push_back(it->path().filename());
    }

    reportError(ec, "listFolders/iterate(" + path.string() + ")");
    return result;
}

void FolderHandler::collectStats(fs::path const& path, FolderStats& out,
                                 bool recursive, int depth) const
{
    std::error_code ec;
    bool hasChildren = false;

    for (auto const& entry : fs::directory_iterator(path, ec)) {
        if (ec) { reportError(ec, "stats/iterate"); break; }

        hasChildren = true;

        if (entry.is_directory(ec)) {
            ++out.folderCount;
            if (depth > out.maxDepth) out.maxDepth = depth;

            if (recursive)
                collectStats(entry.path(), out, recursive, depth + 1);
        } else if (entry.is_regular_file(ec)) {
            ++out.fileCount;
            auto sz = entry.file_size(ec);
            if (!ec) out.totalSize += sz;

            auto t = entry.last_write_time(ec);
            if (!ec && t > out.lastModified) out.lastModified = t;
        }
    }

    if (!hasChildren) ++out.emptyFolders;
}

FolderStats FolderHandler::stats(fs::path const& path, bool recursive) const
{
    FolderStats out;
    FH_LOG(LogLevel::Debug, "stats: {}", path.string());
    collectStats(path, out, recursive, 1);
    return out;
}

FolderEntry FolderHandler::buildTree(fs::path const& path, FolderFilter const& filter,
                                     int depth) const
{
    FolderEntry node;
    node.path  = path;
    node.name  = path.filename().string();
    node.depth = depth;

    std::error_code ec;
    node.isSymlink    = fs::is_symlink(path, ec);
    node.lastModified = fs::last_write_time(path, ec);
    node.size         = fs::is_regular_file(path, ec) ? fs::file_size(path, ec) : 0;

    if (!fs::is_directory(path, ec) || node.isSymlink || !canDescend(filter, depth))
        return node;

    for (auto const& entry : fs::directory_iterator(path, ec)) {
        if (ec) { reportError(ec, "tree/iterate"); break; }

        if (filter.excludeHidden && isHidden(entry.path()))
            continue;

        auto child = buildTree(entry.path(), filter, depth + 1);

        // Keep an entry if it matches, or if it is a directory that contains matches
        if (!matchFilter(entry.path(), filter, depth + 1) && child.children.empty())
            continue;

        node.size += child.size;   // directories aggregate the size of their kept content
        node.children.push_back(std::move(child));
    }

    return node;
}

FolderEntry FolderHandler::tree(fs::path const& path, FolderFilter const& filter) const
{
    FH_LOG(LogLevel::Debug, "tree: {}", path.string());
    return buildTree(path, filter, 0);
}

void FolderHandler::walkImpl(fs::path const& path, Visitor const& visitor, bool recursive,
                             FolderFilter const& filter, int depth) const
{
    std::error_code ec;

    for (auto const& entry : fs::directory_iterator(path, ec)) {
        if (ec) { reportError(ec, "walk/iterate"); break; }

        // Hidden entries (and their subtrees) are pruned entirely
        if (filter.excludeHidden && isHidden(entry.path()))
            continue;

        bool const isDir = entry.is_directory(ec) && !entry.is_symlink(ec);

        // The filter selects what is reported; it does not stop the descent
        if (matchFilter(entry.path(), filter, depth)) {
            FolderEntry fe;
            fe.path         = entry.path();
            fe.name         = entry.path().filename().string();
            fe.depth        = depth;
            fe.isSymlink    = entry.is_symlink(ec);
            fe.lastModified = entry.last_write_time(ec);
            fe.size         = entry.is_regular_file(ec) ? entry.file_size(ec) : 0;
            visitor(fe);
        }

        if (recursive && isDir && canDescend(filter, depth))
            walkImpl(entry.path(), visitor, recursive, filter, depth + 1);
    }
}

void FolderHandler::walk(fs::path const& path, Visitor visitor, bool recursive,
                         FolderFilter const& filter) const
{
    FH_LOG(LogLevel::Debug, "walk: {}", path.string());
    walkImpl(path, visitor, recursive, filter, 1);
}

void FolderHandler::walkParallel(fs::path const& path, Visitor visitor, unsigned int nThreads,
                                 FolderFilter const& filter) const
{
    if (nThreads == 0)
        nThreads = std::max(1u, std::thread::hardware_concurrency());

    FH_LOG(LogLevel::Debug, "walkParallel: {} nThreads={}", path.string(), nThreads);

    // Visit matching first-level entries immediately; collect directories to descend into
    std::vector<fs::path> roots;
    std::error_code ec;
    for (auto const& entry : fs::directory_iterator(path, ec)) {
        if (filter.excludeHidden && isHidden(entry.path()))
            continue;

        bool const isDir = entry.is_directory(ec) && !entry.is_symlink(ec);

        if (matchFilter(entry.path(), filter, 1)) {
            FolderEntry fe;
            fe.path         = entry.path();
            fe.name         = entry.path().filename().string();
            fe.depth        = 1;
            fe.isSymlink    = entry.is_symlink(ec);
            fe.lastModified = entry.last_write_time(ec);
            fe.size         = entry.is_regular_file(ec) ? entry.file_size(ec) : 0;
            visitor(fe);
        }

        if (isDir && canDescend(filter, 1))
            roots.push_back(entry.path());
    }

    // Serialise visitor calls across threads
    std::mutex visitorMtx;
    auto safeVisitor = [&](FolderEntry const& fe) {
        std::lock_guard<std::mutex> lk(visitorMtx);
        visitor(fe);
    };

    std::vector<std::future<void>> futures;
    futures.reserve(roots.size());

    for (auto const& r : roots) {
        futures.push_back(std::async(std::launch::async, [&, r]() {
            walkImpl(r, safeVisitor, /*recursive=*/true, filter, 2);
        }));

        // Bound the number of tasks in flight
        if (futures.size() >= nThreads) {
            futures.front().wait();
            futures.erase(futures.begin());
        }
    }

    for (auto& f : futures) f.wait();
}

std::vector<fs::path> FolderHandler::list(fs::path const& path, bool recursive,
                                          FolderFilter const& filter) const
{
    std::vector<fs::path> result;
    walk(path, [&](FolderEntry const& fe) { result.push_back(fe.path); }, recursive, filter);
    return result;
}

std::vector<fs::path> FolderHandler::find(fs::path const& path, FolderFilter const& filter) const
{
    FH_LOG(LogLevel::Debug, "find: {} pattern={}", path.string(), filter.namePattern);
    return list(path, /*recursive=*/true, filter);
}

std::map<std::string, std::vector<fs::path>>
FolderHandler::findDuplicates(fs::path const& path) const
{
    FH_LOG(LogLevel::Debug, "findDuplicates: {}", path.string());

    std::map<std::string, std::vector<fs::path>> index;
    std::error_code ec;

    for (auto const& entry : fs::recursive_directory_iterator(path, ec)) {
        if (!entry.is_regular_file(ec)) continue;

        auto sz  = entry.file_size(ec);
        auto key = entry.path().filename().string() + ":" + std::to_string(sz);
        index[key].push_back(entry.path());
    }

    // Drop unique entries (not duplicates)
    for (auto it = index.begin(); it != index.end(); ) {
        if (it->second.size() < 2)
            it = index.erase(it);
        else
            ++it;
    }

    return index;
}

FolderDiff FolderHandler::diff(fs::path const& pathA, fs::path const& pathB) const
{
    FH_LOG(LogLevel::Debug, "diff: {} vs {}", pathA.string(), pathB.string());

    FolderDiff result;
    std::error_code ec;

    std::map<fs::path, std::uintmax_t> setA, setB;

    for (auto const& e : fs::recursive_directory_iterator(pathA, ec))
        if (e.is_regular_file(ec))
            setA[e.path().lexically_relative(pathA)] = e.file_size(ec);

    for (auto const& e : fs::recursive_directory_iterator(pathB, ec))
        if (e.is_regular_file(ec))
            setB[e.path().lexically_relative(pathB)] = e.file_size(ec);

    for (auto const& [rel, szA] : setA) {
        auto it = setB.find(rel);
        if (it == setB.end())
            result.onlyInA.push_back(rel);
        else if (it->second != szA)
            result.modified.push_back(rel);
    }

    for (auto const& [rel, szB] : setB)
        if (setA.find(rel) == setA.end())
            result.onlyInB.push_back(rel);

    return result;
}

// ===========================================================================
// Contextual overloads (use m_root)
// ===========================================================================

bool FolderHandler::create      (bool createParents) { requireRoot(m_root); return create(m_root, createParents); }
bool FolderHandler::remove      (bool recursive)     { requireRoot(m_root); return remove(m_root, recursive); }
bool FolderHandler::clear       ()                   { requireRoot(m_root); return clear(m_root); }
bool FolderHandler::ensureExists()                   { requireRoot(m_root); return ensureExists(m_root); }

FolderStats FolderHandler::stats(bool recursive) const        { requireRoot(m_root); return stats(m_root, recursive); }
FolderEntry FolderHandler::tree (FolderFilter const& f) const { requireRoot(m_root); return tree(m_root, f); }

std::vector<fs::path> FolderHandler::list(bool recursive, FolderFilter const& f) const
{ requireRoot(m_root); return list(m_root, recursive, f); }

std::vector<fs::path> FolderHandler::find(FolderFilter const& f) const
{ requireRoot(m_root); return find(m_root, f); }

FolderDiff FolderHandler::diff(fs::path const& other) const
{ requireRoot(m_root); return diff(m_root, other); }

std::vector<fs::path> FolderHandler::listFolders() const
{ requireRoot(m_root); return listFolders(m_root); }

void FolderHandler::walk(Visitor v, bool recursive, FolderFilter const& f) const
{ requireRoot(m_root); walk(m_root, std::move(v), recursive, f); }

void FolderHandler::walkParallel(Visitor v, unsigned int n, FolderFilter const& f) const
{ requireRoot(m_root); walkParallel(m_root, std::move(v), n, f); }

// ===========================================================================
// Static utilities
// ===========================================================================

bool FolderHandler::isFolder(fs::path const& path)
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

fs::path FolderHandler::normalize(fs::path const& path)
{
    std::error_code ec;
    auto canonical = fs::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

fs::path FolderHandler::relative(fs::path const& from, fs::path const& to)
{
    std::error_code ec;
    return fs::relative(to, from, ec);
}

fs::path FolderHandler::commonAncestor(std::vector<fs::path> const& paths)
{
    if (paths.empty()) return {};
    if (paths.size() == 1) return paths[0].parent_path();

    std::vector<fs::path> norm;
    norm.reserve(paths.size());
    for (auto const& p : paths)
        norm.push_back(normalize(p));

    // Start from the first path and erode until it is a prefix of all the others
    fs::path ancestor = norm[0].has_parent_path() ? norm[0] : norm[0].parent_path();

    for (std::size_t i = 1; i < norm.size(); ++i) {
        while (!isSubPath(ancestor, norm[i])) {
            if (!ancestor.has_parent_path() || ancestor == ancestor.parent_path())
                return ancestor.root_path();
            ancestor = ancestor.parent_path();
        }
    }
    return ancestor;
}

bool FolderHandler::isSubPath(fs::path const& parent, fs::path const& child)
{
    auto normParent = normalize(parent);
    auto normChild  = normalize(child);

    auto [it, end] = std::mismatch(normParent.begin(), normParent.end(),
                                   normChild.begin(),  normChild.end());
    return it == normParent.end();
}

std::string FolderHandler::formatSize(std::uintmax_t bytes)
{
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    constexpr double TB = GB * 1024.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if      (bytes >= static_cast<std::uintmax_t>(TB)) oss << bytes / TB << " TB";
    else if (bytes >= static_cast<std::uintmax_t>(GB)) oss << bytes / GB << " GB";
    else if (bytes >= static_cast<std::uintmax_t>(MB)) oss << bytes / MB << " MB";
    else if (bytes >= static_cast<std::uintmax_t>(KB)) oss << bytes / KB << " KB";
    else                                                oss << bytes     << " B";

    return oss.str();
}

} // namespace TK4CPP::Utility

#undef FH_LOG

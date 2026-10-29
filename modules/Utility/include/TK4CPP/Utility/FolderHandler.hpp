#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <vector>

#include "TK4CPP/Utility/Export.h"
#include "FolderEntry.hpp"

namespace TK4CPP::Utility {

namespace fs = std::filesystem;

/**
 * @brief Utility class for manipulating and analysing folders.
 *
 * Can be used either:
 *  - as a context object bound to a root folder via at() or the constructor, or
 *  - as a plain utility by passing explicit paths.
 *
 * Error handling:
 *  - throwOnError == false: the error is reported and the method returns false (default).
 *  - throwOnError == true : std::filesystem::filesystem_error is thrown.
 *
 * Logging: the class has no dependency on any logging library. Every operation is
 * reported through an optional callback (see setLogCallback()); with no callback set
 * nothing is formatted or emitted.
 *
 * @par Bridging to another logger
 * @code
 * folder.setLogCallback([](FolderHandler::LogLevel lvl, std::string const& msg) {
 *     switch (lvl) {
 *         case FolderHandler::LogLevel::Debug: LL_DEBUG("folder", "{}", msg); break;
 *         case FolderHandler::LogLevel::Info:  LL_INFO ("folder", "{}", msg); break;
 *         case FolderHandler::LogLevel::Warn:  LL_WARN ("folder", "{}", msg); break;
 *         case FolderHandler::LogLevel::Error: LL_ERROR("folder", "{}", msg); break;
 *     }
 * });
 * @endcode
 *
 * @par Thread safety
 * walkParallel() may report errors from several worker threads at once, so the
 * log callback must be thread-safe if that method is used.
 */
class TK4CPP_UTILITY_EXPORT FolderHandler {
public:
    /// Severity of a message reported through the log callback.
    enum class LogLevel {
        Debug,  ///< Operation trace (one line per public call).
        Info,   ///< Reserved for notable events.
        Warn,   ///< Recoverable anomaly.
        Error   ///< An operation failed (always followed by return false or a throw).
    };

    /// Callback receiving the messages of this class.
    using LogCallback = std::function<void(LogLevel, std::string const&)>;

    /// Visitor invoked for every entry during walk().
    using Visitor = std::function<void(FolderEntry const&)>;

    /// @name Construction
    /// @{
    FolderHandler() = default;

    /**
     * @brief Construct a handler bound to a root folder.
     * @param root          Root folder used by the contextual overloads.
     * @param throwOnError  Throw instead of returning false on errors.
     */
    explicit FolderHandler(fs::path root, bool throwOnError = false);

    /// Named constructor, fluent style: `auto fh = FolderHandler::at("/var/data");`
    [[nodiscard]] static FolderHandler at(fs::path root);
    /// @}

    /// @name Configuration
    /// @{
    /**
     * @brief Install the log callback (an empty function disables logging).
     * @param callback  Receives every message at or above @p minLevel.
     * @param minLevel  Messages below this level are neither formatted nor delivered.
     */
    void setLogCallback (LogCallback callback, LogLevel minLevel = LogLevel::Debug);
    void setThrowOnError(bool value);
    void setRoot        (fs::path root);
    [[nodiscard]] fs::path root() const;
    /// @}

    /// @name Creation / deletion
    /// @{

    /// Create the folder (optionally with all missing parents). Succeeds if the folder already exists.
    bool create(fs::path const& path, bool createParents = true);
    bool create(char const* path, bool createParents = true) { return create(fs::path{path}, createParents); }

    /**
     * @brief Remove the folder.
     * @param path       Folder to remove. Succeeds if it does not exist.
     * @param recursive  If false (safe default) the call fails when the folder is not empty.
     * @note Fails with std::errc::not_a_directory if @p path exists but is not a folder
     *       (files are never removed by this method).
     */
    bool remove(fs::path const& path, bool recursive = false);
    bool remove(char const* path, bool recursive = false)    { return remove(fs::path{path}, recursive); }

    /// Delete the folder content without removing the folder itself.
    bool clear(fs::path const& path);

    /// Create the folder if missing, do nothing otherwise (idempotent).
    bool ensureExists(fs::path const& path);
    /// @}

    /// @name Copy / move
    /// @{

    /// Copy src to dst using the given options.
    bool copy(fs::path const& src, fs::path const& dst, CopyOptions const& opts = {});

    /// Move/rename src to dst.
    bool move(fs::path const& src, fs::path const& dst, bool overwrite = false);

    /// Merge the content of src into dst. Conflicting files are governed by opts.overwrite.
    bool merge(fs::path const& src, fs::path const& dst, CopyOptions const& opts = {});
    /// @}

    /// @name Analysis / query
    /// @{

    [[nodiscard]] bool exists (fs::path const& path) const;
    [[nodiscard]] bool isEmpty(fs::path const& path) const;

    /**
     * @brief List the direct subfolders of a folder.
     * @param path  Folder to inspect.
     * @return Names of the subfolders (file name only, not full paths), in directory order.
     *         Empty if @p path does not exist or is not a folder.
     */
    [[nodiscard]] std::vector<fs::path> listFolders(fs::path const& path) const;

    /// Compute aggregate statistics for the folder.
    [[nodiscard]] FolderStats stats(fs::path const& path, bool recursive = true) const;
    [[nodiscard]] FolderStats stats(char const* path, bool recursive = true) const { return stats(fs::path{path}, recursive); }

    /// Build the whole tree as a nested FolderEntry structure.
    [[nodiscard]] FolderEntry tree(fs::path const& path, FolderFilter const& filter = {}) const;

    /// List direct (or recursive) entries with an optional filter.
    [[nodiscard]] std::vector<fs::path> list(fs::path const& path,
                                             bool recursive = false,
                                             FolderFilter const& filter = {}) const;
    [[nodiscard]] std::vector<fs::path> list(char const* path, bool recursive = false,
                                             FolderFilter const& filter = {}) const
    { return list(fs::path{path}, recursive, filter); }

    /// Advanced recursive search (name patterns, size and date ranges, kind, predicate, depth).
    [[nodiscard]] std::vector<fs::path> find(fs::path const& path, FolderFilter const& filter) const;

    /**
     * @brief Cancellable recursive search with a result cap.
     * @param path       Folder to search (symbolic links are never followed).
     * @param filter     Selection criteria, see FolderFilter.
     * @param stop       Checked for every entry; when stop is requested the matches found so far are returned.
     * @param maxResults Stop after this many matches (0 = unlimited).
     * @return Matching paths in directory order.
     */
    [[nodiscard]] std::vector<fs::path> find(fs::path const& path, FolderFilter const& filter,
                                             std::stop_token stop, std::size_t maxResults = 0) const;

    /**
     * @brief Find duplicate files by name and size.
     * @return Map with key `"<name>:<size>"` and value the list of paths sharing that fingerprint.
     */
    [[nodiscard]] std::map<std::string, std::vector<fs::path>>
        findDuplicates(fs::path const& path) const;

    /// Structural difference between two folder trees.
    [[nodiscard]] FolderDiff diff(fs::path const& pathA, fs::path const& pathB) const;
    /// @}

    /// @name Walk / visitor
    /// @{

    /// Visit every node of the tree depth-first, applying @p visitor.
    void walk(fs::path const& path,
              Visitor visitor,
              bool recursive = true,
              FolderFilter const& filter = {}) const;

    /**
     * @brief Parallel variant: visits first-level subtrees on several threads.
     * @param nThreads  0 means std::thread::hardware_concurrency().
     * @note The visitor calls are serialised by an internal mutex.
     */
    void walkParallel(fs::path const& path,
                      Visitor visitor,
                      unsigned int nThreads = 0,
                      FolderFilter const& filter = {}) const;
    /// @}

    /// @name Contextual overloads (operate on the root folder)
    /// @note The `char const*` overloads of create(), remove(), stats() and list() exist so that
    ///       a string literal selects the path version instead of these `bool` ones.
    /// @{
    bool create      (bool createParents = true);
    bool remove      (bool recursive = false);
    bool clear       ();
    bool ensureExists();

    [[nodiscard]] FolderStats           stats(bool recursive = true) const;
    [[nodiscard]] FolderEntry           tree (FolderFilter const& filter = {}) const;
    [[nodiscard]] std::vector<fs::path> list (bool recursive = false,
                                              FolderFilter const& filter = {}) const;
    [[nodiscard]] std::vector<fs::path> find (FolderFilter const& filter) const;
    [[nodiscard]] FolderDiff            diff (fs::path const& other) const;
    [[nodiscard]] std::vector<fs::path> listFolders() const;

    void walk        (Visitor visitor, bool recursive = true,
                      FolderFilter const& filter = {}) const;
    void walkParallel(Visitor visitor, unsigned int nThreads = 0,
                      FolderFilter const& filter = {}) const;
    /// @}

    /// @name Static utilities
    /// @{

    /// True if @p path exists and is a folder (symlinks are followed); false on any error.
    [[nodiscard]] static bool isFolder(fs::path const& path);

    /// Resolve "..", "." and symlinks into a canonical/normalised path.
    [[nodiscard]] static fs::path normalize(fs::path const& path);

    /// Relative path from @p from to @p to.
    [[nodiscard]] static fs::path relative(fs::path const& from, fs::path const& to);

    /// Common ancestor of a set of paths.
    [[nodiscard]] static fs::path commonAncestor(std::vector<fs::path> const& paths);

    /// Check that @p child lies structurally inside @p parent.
    [[nodiscard]] static bool isSubPath(fs::path const& parent, fs::path const& child);

    /// Format a byte count as a human readable string ("1.23 MB", powers of 1024; see formatSize() in SizeFormat.hpp for other units).
    [[nodiscard]] static std::string formatSize(std::uintmax_t bytes);
    /// @}

private:
    fs::path m_root;
    LogCallback m_log;
    LogLevel    m_logMinLevel{LogLevel::Debug};
    bool     m_throwOnError{false};

    /// Log/throw the error; returns true if @p ec is clear, false otherwise (when not throwing).
    bool reportError(std::error_code const& ec, std::string const& context) const;

    /// Check whether @p path satisfies @p filter at the given depth.
    static bool matchFilter(fs::path const& path, FolderFilter const& filter, int depth);

    /// Recursive implementation of the cancellable find().
    void findImpl(fs::path const& path, FolderFilter const& filter, int depth, std::stop_token const& stop,
                  std::size_t maxResults, std::vector<fs::path>& out) const;

    /// Recursive implementation of walk().
    void walkImpl(fs::path const& path, Visitor const& visitor, bool recursive,
                  FolderFilter const& filter, int depth) const;

    /// Recursive implementation of tree().
    FolderEntry buildTree(fs::path const& path, FolderFilter const& filter, int depth) const;

    /// Recursive implementation of stats().
    void collectStats(fs::path const& path, FolderStats& out, bool recursive, int depth) const;

    /// Filtered recursive copy.
    bool copyRecursive(fs::path const& src, fs::path const& dst, CopyOptions const& opts) const;
};

} // namespace TK4CPP::Utility

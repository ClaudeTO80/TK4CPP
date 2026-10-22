// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file test_highlevel.cpp
 * @brief Tests for ZipWrapper/HighLevel.h (no external framework).
 *
 * Tests that need AES print SKIP on a libzip built without a crypto backend;
 * in that case the "refuse, never downgrade" path is tested instead.
 */

#include <ZipWrapper/HighLevel.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>

namespace fs = std::filesystem;
using namespace Zip;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) { ++g_pass; }                                              \
        else {                                                               \
            ++g_fail;                                                        \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << "  " #cond \
                      << '\n';                                               \
        }                                                                    \
    } while (0)

#define CHECK_THROWS_CODE(expr, code)                                        \
    do {                                                                     \
        try {                                                                \
            (void)(expr);                                                    \
            ++g_fail;                                                        \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__              \
                      << "  no exception: " #expr << '\n';                   \
        } catch (const Exception& e) {                                       \
            if (e.zipCode() == (code)) { ++g_pass; }                         \
            else {                                                           \
                ++g_fail;                                                    \
                std::cerr << "FAIL " << __FILE__ << ':' << __LINE__          \
                          << "  wrong code " << e.zipCode() << " (want "     \
                          << (code) << "): " << e.what() << '\n';            \
            }                                                                \
        }                                                                    \
    } while (0)

namespace {

using Bytes = std::vector<std::byte>;

const bool kAes = Archive::encryptionSupported(Encryption::AES256);

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void writeFile(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

/// Deterministic incompressible bytes.
std::string noise(std::size_t n, std::uint32_t seed) {
    std::string s(n, '\0');
    for (auto& c : s) {
        seed = seed * 1664525u + 1013904223u;
        c = static_cast<char>(seed >> 24);
    }
    return s;
}

std::string repeated(std::string_view unit, std::size_t times) {
    std::string s;
    for (std::size_t i = 0; i < times; ++i) s += unit;
    return s;
}

struct Scratch {
    fs::path dir;
    explicit Scratch(const char* name)
        : dir(fs::temp_directory_path() / (std::string("zipwrapper_hl_") + name)) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(dir, ec); }
    fs::path operator/(const char* n) const { return dir / n; }
};

/**
 * @brief Set a file's modification time from a system_clock time point.
 *
 * Portable replacement for file_clock::from_sys(): that function is not
 * available everywhere (the clock behind std::filesystem::file_time_type is
 * platform specific), whereas now() and duration arithmetic always are. The
 * offset between the two clocks is measured with back-to-back now() calls,
 * which is accurate to well below a millisecond; ZIP stores times with 1-2 s
 * resolution, so this is far more precise than the tests need.
 */
void setModificationTime(const fs::path& path, std::chrono::sys_seconds when) {
    const auto systemNow = std::chrono::system_clock::now();
    const auto fileNow = fs::file_time_type::clock::now();
    fs::last_write_time(
        path, fileNow + std::chrono::duration_cast<fs::file_time_type::duration>(when - systemNow));
}

/// The fixture tree used by most tests. Returns the root.
fs::path makeTree(const fs::path& root) {
    writeFile(root / "readme.txt", repeated("hello\n", 1000));
    writeFile(root / "docs/guide.md", repeated("# guide\n", 100));
    writeFile(root / "docs/deep/data.csv", repeated("1,2,3\n", 3000));
    writeFile(root / "src/main.cpp", "int main() {}\n");
    writeFile(root / "src/util/helper.cpp", "// helper\n");
    writeFile(root / "src/util/helper.h", "#pragma once\n");
    writeFile(root / "photo.JPG", noise(3000, 7));
    writeFile(root / "scratch.tmp", "junk");
    writeFile(root / ".git/objects/obj", "git");
    writeFile(root / "node_modules/x/i.js", "js");
    fs::create_directories(root / "empty_dir");
#if !defined(_WIN32)
    fs::create_symlink(root / "readme.txt", root / "link");
#endif
    return root;
}

ArchiveOptions typicalOptions() {
    ArchiveOptions o;
    o.filter.exclude = {".git", "node_modules", "*.tmp"};
    return o;
}

/// Regular files below @p root as {relative name -> content}.
std::map<std::string, std::string> snapshot(const fs::path& root) {
    std::map<std::string, std::string> m;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file())
            m[e.path().lexically_relative(root).generic_string()] = readFile(e.path());
    return m;
}

std::vector<std::string> names(const std::vector<EntryInfo>& v) {
    std::vector<std::string> out;
    for (const auto& e : v) out.push_back(e.name);
    return out;
}

const std::vector<std::string> kFiles{
    "docs/deep/data.csv", "docs/guide.md", "photo.JPG", "readme.txt",
    "src/main.cpp", "src/util/helper.cpp", "src/util/helper.h"};

// ---------------------------------------------------------------------------

void test_patterns() {
    const auto ex = [](std::string p, std::string_view path) {
        PathFilter f; f.exclude = {std::move(p)}; return f.excludes(path);
    };
    const auto in = [](std::string p, std::string_view path, bool dir = false) {
        PathFilter f; f.include = {std::move(p)}; return f.includes(path, dir);
    };

    CHECK(ex("*.tmp", "a/b.tmp"));
    CHECK(!ex("*.tmp", "a/tmpx"));
    CHECK(ex("node_modules", "node_modules/x/i.js"));       // whole subtree
    CHECK(ex("node_modules", "a/node_modules"));            // at any depth
    CHECK(!ex("node_modules", "my_node_modules/x"));
    CHECK(ex("build/cache", "build/cache/x"));
    CHECK(!ex("build/cache", "build/cachex"));
    CHECK(!ex("build/cache", "other/build/cache"));          // anchored: has '/'
    CHECK(ex("./docs/", "docs/guide.md"));                   // ./ and trailing /
    CHECK(ex("file?.txt", "file1.txt"));
    CHECK(!ex("file?.txt", "file10.txt"));

    CHECK(in("*.cpp", "src/main.cpp"));                      // name match, any depth
    CHECK(!in("*.cpp", "main.cpp.bak"));
    CHECK(!in("*.cpp", "src", /*dir=*/true));                // dirs never included
    CHECK(in("src/**/*.cpp", "src/main.cpp"));               // ** matches zero dirs
    CHECK(in("src/**/*.cpp", "src/util/helper.cpp"));
    CHECK(!in("src/**/*.cpp", "srcx/a.cpp"));
    CHECK(!in("src/**/*.cpp", "src/util/helper.h"));
    CHECK(in("docs/**", "docs/deep/data.csv"));
    CHECK(in("**", "any/thing.txt"));

    PathFilter empty;
    CHECK(empty.includes("x", true) && empty.accepts("x/y", false));
}

void test_archive_directory() {
    Scratch s("dir");
    const auto tree = makeTree(s / "tree");

    // Set a known mtime so preservation can be checked.
    const std::chrono::sys_seconds stamp = std::chrono::sys_days{std::chrono::year{2020} / 6 / 15} +
                                           std::chrono::hours{10} + std::chrono::minutes{20};
    setModificationTime(tree / "readme.txt", stamp);

    // The output lives inside the tree being archived.
    const auto out = tree / "backup.zip";
    const auto report = archiveDirectory(tree, out, typicalOptions());

    CHECK(report.files == kFiles.size());
    CHECK(report.directories == 5);  // docs, docs/deep, src, src/util, empty_dir
    CHECK(report.encrypted == 0);
    CHECK(report.archiveBytes == fs::file_size(out));
    CHECK(report.inputBytes > 0);
#if !defined(_WIN32)
    CHECK(std::ranges::any_of(report.skipped, [](const std::string& x) {
        return x.starts_with("link:");
    }));
#endif

    const auto all = listEntries(out);
    const auto n = names(all);
    CHECK(std::ranges::is_sorted(n));                              // deterministic order
    CHECK(std::ranges::find(n, "backup.zip") == n.end());          // never itself
    CHECK(std::ranges::none_of(n, [](const std::string& x) {
        return x.starts_with(".git") || x.starts_with("node_modules") ||
               x.ends_with(".tmp") || x == "link";
    }));
    CHECK(std::ranges::none_of(n, [](const std::string& x) { return x.find('\\') != std::string::npos; }));

    const auto files = listFiles(out);
    CHECK(names(files) == kFiles);

    for (const auto& e : files) {
        if (e.name == "photo.JPG") CHECK(e.method == ZIP_CM_STORE);      // extension policy
        if (e.name == "readme.txt") {
            CHECK(e.method == ZIP_CM_DEFLATE);
            CHECK(e.packedSize < e.size);
            CHECK(std::abs(e.modified - static_cast<std::int64_t>(stamp.time_since_epoch().count())) <= 2);
        }
    }

    // Round trip through the high-level extractor.
    const auto restored = s / "restored";
    const auto ur = extractArchive(out, restored);
    CHECK(ur.ok() && ur.extracted == all.size());
    auto original = snapshot(tree);
    for (auto it = original.begin(); it != original.end();) {
        const bool drop = it->first.starts_with(".git") || it->first.starts_with("node_modules") ||
                          it->first.ends_with(".tmp") || it->first == "link" || it->first == "backup.zip";
        it = drop ? original.erase(it) : std::next(it);
    }
    CHECK(snapshot(restored) == original);
    CHECK(fs::is_directory(restored / "empty_dir"));

    // Re-running the backup: the output now EXISTS inside the tree during the
    // scan and must still be left out (the nightly-backup-into-source case).
    ArchiveOptions again = typicalOptions();
    again.overwriteOutput = true;
    const auto second = archiveDirectory(tree, out, again);
    CHECK(second.files == kFiles.size());
    const auto afterNames = names(listEntries(out));
    CHECK(std::ranges::find(afterNames, "backup.zip") == afterNames.end());
}

void test_filters_and_prefix() {
    Scratch s("filter");
    const auto tree = makeTree(s / "tree");

    ArchiveOptions o = typicalOptions();
    o.filter.include = {"*.cpp"};
    archiveDirectory(tree, s / "cpp.zip", o);
    CHECK(names(listEntries(s / "cpp.zip")) ==
          (std::vector<std::string>{"src/main.cpp", "src/util/helper.cpp"}));
    CHECK(summarize(s / "cpp.zip").directories == 0);   // no directory entries with include

    ArchiveOptions p = typicalOptions();
    p.prefix = "proj";
    archiveDirectory(tree, s / "prefixed.zip", p);
    const auto n = names(listFiles(s / "prefixed.zip"));
    CHECK(!n.empty() && std::ranges::all_of(n, [](const std::string& x) { return x.starts_with("proj/"); }));

    // listing filters
    ListOptions lo;
    lo.filesOnly = true;
    lo.filter.include = {"*.md"};
    CHECK(names(listEntries(s / "prefixed.zip", lo)) == (std::vector<std::string>{"proj/docs/guide.md"}));
    CHECK(names(listFiles(s / "prefixed.zip", "*.h")) == (std::vector<std::string>{"proj/src/util/helper.h"}));
}

void test_archive_files() {
    Scratch s("files");
    const auto tree = makeTree(s / "tree");
    const std::vector<fs::path> paths{tree / "readme.txt", tree / "docs", tree / "readme.txt"};
    const auto report = archiveFiles(paths, tree, s / "sel.zip");
    auto n = names(listEntries(s / "sel.zip"));
    std::ranges::sort(n);
    CHECK(n == (std::vector<std::string>{"docs/", "docs/deep/", "docs/deep/data.csv", "docs/guide.md", "readme.txt"}));
    CHECK(report.files == 3);  // duplicate readme.txt added once

    CHECK_THROWS_CODE(archiveFiles(std::vector<fs::path>{s.dir / "elsewhere.txt"}, tree, s / "x.zip"), ZIP_ER_NOENT);
    writeFile(s / "outside.txt", "x");
    CHECK_THROWS_CODE(archiveFiles(std::vector<fs::path>{s / "outside.txt"}, tree, s / "y.zip"), ZIP_ER_INVAL);
    CHECK(!fs::exists(s / "y.zip"));
}

void test_output_policy_and_errors() {
    Scratch s("policy");
    const auto tree = makeTree(s / "tree");

    archiveDirectory(tree, s / "a.zip", typicalOptions());
    const auto before = readFile(s / "a.zip");

    CHECK_THROWS_CODE(archiveDirectory(tree, s / "a.zip", typicalOptions()), ZIP_ER_EXISTS);
    CHECK(readFile(s / "a.zip") == before);           // untouched

    writeFile(tree / "extra.txt", "new");
    ArchiveOptions o = typicalOptions();
    o.overwriteOutput = true;
    archiveDirectory(tree, s / "a.zip", o);
    CHECK(readFile(s / "a.zip") != before);
    const auto afterNames = names(listFiles(s / "a.zip"));
    CHECK(std::ranges::find(afterNames, "extra.txt") != afterNames.end());

    CHECK_THROWS_CODE(archiveDirectory(s / "missing", s / "b.zip"), ZIP_ER_NOENT);
    CHECK_THROWS_CODE(archiveDirectory(tree / "readme.txt", s / "b.zip"), ZIP_ER_INVAL);
    ArchiveOptions bad;
    bad.level = 10;
    CHECK_THROWS_CODE(archiveDirectory(tree, s / "b.zip", bad), ZIP_ER_INVAL);
    CHECK(!fs::exists(s / "b.zip"));

    // Output in a directory that does not exist yet is created.
    archiveDirectory(tree, s / "new/sub/c.zip", typicalOptions());
    CHECK(fs::exists(s / "new/sub/c.zip"));

    // verifyAfterWrite on a healthy archive passes.
    ArchiveOptions v = typicalOptions();
    v.verifyAfterWrite = true;
    archiveDirectory(tree, s / "v.zip", v);
    CHECK(fs::exists(s / "v.zip"));
    CHECK(!fs::exists(s / "v.zip.partial"));         // moved into place, not copied
    CHECK(verifyArchive(s / "v.zip").ok());

    // Replacing an existing archive through the verified path.
    v.overwriteOutput = true;
    writeFile(tree / "another.txt", "more");
    archiveDirectory(tree, s / "v.zip", v);
    CHECK(!fs::exists(s / "v.zip.partial"));
    const auto replaced = names(listFiles(s / "v.zip"));
    CHECK(std::ranges::find(replaced, "another.txt") != replaced.end());
}

void test_progress_and_cancel() {
    Scratch s("progress");
    const auto tree = s / "tree";
    for (int i = 0; i < 24; ++i)
        writeFile(tree / ("f" + std::to_string(i) + ".bin"), noise(150000, 100 + i));
    ArchiveOptions o;
    o.storeExtensions.clear();  // force real compression work

    std::vector<double> seen;
    o.progress = [&](double p) { seen.push_back(p); return true; };
    archiveDirectory(tree, s / "a.zip", o);
    CHECK(seen.size() >= 3);
    CHECK(std::ranges::is_sorted(seen));
    CHECK(!seen.empty() && seen.back() == 1.0);

    // Cancel: old output untouched, nothing else left behind.
    const auto before = readFile(s / "a.zip");
    o.overwriteOutput = true;
    o.progress = [](double p) { return p < 0.3; };
    CHECK_THROWS_CODE(archiveDirectory(tree, s / "a.zip", o), ZIP_ER_CANCELLED);
    CHECK(readFile(s / "a.zip") == before);
    std::size_t leftovers = 0;
    for (const auto& e : fs::directory_iterator(s.dir))
        if (e.path().filename() != "tree" && e.path().filename() != "a.zip") ++leftovers;
    CHECK(leftovers == 0);

    // Cancelling when there was no previous output leaves no output at all.
    CHECK_THROWS_CODE(archiveDirectory(tree, s / "fresh.zip", o), ZIP_ER_CANCELLED);
    CHECK(!fs::exists(s / "fresh.zip"));

    // A throwing callback surfaces as its own exception type.
    o.progress = [](double) -> bool { throw std::logic_error("from callback"); };
    bool sawLogicError = false;
    try { archiveDirectory(tree, s / "boom.zip", o); }
    catch (const std::logic_error&) { sawLogicError = true; }
    CHECK(sawLogicError);
    CHECK(!fs::exists(s / "boom.zip"));
}

void test_compression_policy() {
    Scratch s("comp");
    const auto tree = makeTree(s / "tree");

    ArchiveOptions o = typicalOptions();
    o.compression = Compression::BZip2;
    o.storeExtensions = {".csv"};
    const auto report = archiveDirectory(tree, s / "a.zip", o);
    const bool bz = Archive::compressionSupported(Compression::BZip2);
    for (const auto& e : listFiles(s / "a.zip")) {
        if (e.name == "docs/deep/data.csv") CHECK(e.method == ZIP_CM_STORE);
        else if (e.name == "readme.txt") CHECK(e.method == (bz ? ZIP_CM_BZIP2 : ZIP_CM_DEFLATE));
    }
    CHECK(report.warnings.empty() == bz);   // fallback is reported, never silent
    CHECK(verifyArchive(s / "a.zip").ok());
}

void test_encryption_transparent() {
    Scratch s("crypt");
    const auto tree = makeTree(s / "tree");
    ArchiveOptions o = typicalOptions();
    o.password = "s3cret!";

    if (!kAes) {
        std::cout << "  SKIP AES tests; checking the refusal path instead\n";
        CHECK_THROWS_CODE(archiveDirectory(tree, s / "a.zip", o), ZIP_ER_ENCRNOTSUPP);
        CHECK(!fs::exists(s / "a.zip"));       // nothing written, no silent downgrade
        return;
    }

    const auto report = archiveDirectory(tree, s / "a.zip", o);
    CHECK(report.encrypted == kFiles.size());

    // Listing needs no password and shows what is protected.
    const auto all = listEntries(s / "a.zip");
    for (const auto& e : all) CHECK(e.encrypted == !e.directory);
    CHECK(summarize(s / "a.zip").needsPassword());
    CHECK(summarize(s / "a.zip").encryptedEntries == kFiles.size());

    // Reading: missing / wrong / right password.
    CHECK_THROWS_CODE(readBytes(s / "a.zip", "readme.txt"), ZIP_ER_NOPASSWD);
    CHECK_THROWS_CODE(readBytes(s / "a.zip", "readme.txt", std::string("nope")), ZIP_ER_WRONGPASSWD);
    CHECK(readText(s / "a.zip", "readme.txt", std::string("s3cret!")) == readFile(tree / "readme.txt"));
    CHECK_THROWS_CODE(readBytes(s / "a.zip", "nope.txt", std::string("s3cret!")), ZIP_ER_NOENT);

    // Extraction: without password, failures are reported per entry.
    UnpackOptions nopw;
    nopw.continueOnError = true;
    const auto bad = extractArchive(s / "a.zip", s / "out_nopw", nopw);
    CHECK(bad.failures.size() == kFiles.size());
    CHECK(std::ranges::all_of(bad.failures, [](const EntryFailure& f) { return f.zipCode == ZIP_ER_NOPASSWD; }));
    CHECK(!fs::exists(s / "out_nopw" / "readme.txt"));      // no empty/partial file left

    UnpackOptions withpw;
    withpw.password = "s3cret!";
    const auto good = extractArchive(s / "a.zip", s / "out", withpw);
    CHECK(good.ok());
    CHECK(readFile(s / "out" / "docs/deep/data.csv") == readFile(tree / "docs/deep/data.csv"));
    CHECK(readFile(s / "out" / "photo.JPG") == readFile(tree / "photo.JPG"));

    // verifyArchive: skipped without password, verified with, flagged with a wrong one.
    const auto v0 = verifyArchive(s / "a.zip");
    CHECK(v0.ok() && v0.skipped == kFiles.size() && v0.checked == 0);
    const auto v1 = verifyArchive(s / "a.zip", std::string("s3cret!"));
    CHECK(v1.ok() && v1.checked == kFiles.size() && v1.skipped == 0);
    const auto v2 = verifyArchive(s / "a.zip", std::string("wrong"));
    CHECK(!v2.ok() && v2.problems.size() == kFiles.size());

    // Transparent: the same call works on a plain archive with or without a password.
    archiveDirectory(tree, s / "plain.zip", typicalOptions());
    CHECK(readText(s / "plain.zip", "readme.txt") == readFile(tree / "readme.txt"));
    CHECK(readText(s / "plain.zip", "readme.txt", std::string("ignored")) == readFile(tree / "readme.txt"));

    // Selective encryption + verification right after writing.
    ArchiveOptions sel = typicalOptions();
    sel.password = "pw";
    sel.encryptOnly = {"*.md", "src/**"};
    sel.verifyAfterWrite = true;
    const auto sr = archiveDirectory(tree, s / "sel.zip", sel);
    CHECK(sr.encrypted == 4);   // guide.md + 3 files under src/
    for (const auto& e : listFiles(s / "sel.zip"))
        CHECK(e.encrypted == (e.name == "docs/guide.md" || e.name.starts_with("src/")));
    CHECK(readText(s / "sel.zip", "readme.txt") == readFile(tree / "readme.txt"));   // plain, no password
}

void test_protection_settings_rejected() {
    Scratch s("reject");
    const auto tree = makeTree(s / "tree");

    ArchiveOptions o = typicalOptions();
    o.password = "pw";
    o.encryption = Encryption::Traditional;
    CHECK_THROWS_CODE(archiveDirectory(tree, s / "a.zip", o), ZIP_ER_INVAL);
    o.encryption = Encryption::None;
    CHECK_THROWS_CODE(archiveDirectory(tree, s / "a.zip", o), ZIP_ER_INVAL);
    o.encryption = Encryption::AES256;
    o.password = "";
    CHECK_THROWS_CODE(archiveDirectory(tree, s / "a.zip", o), ZIP_ER_INVAL);
    CHECK(!fs::exists(s / "a.zip"));
}

void test_verify_detects_corruption() {
    Scratch s("corrupt");
    const auto tree = makeTree(s / "tree");
    archiveDirectory(tree, s / "a.zip", typicalOptions());
    CHECK(verifyArchive(s / "a.zip").ok());

    // photo.JPG is stored (not compressed): its bytes appear verbatim in the file.
    std::string zip = readFile(s / "a.zip");
    const std::string photo = readFile(tree / "photo.JPG");
    const auto pos = zip.find(photo.substr(0, 64));
    CHECK(pos != std::string::npos);
    zip[pos + 1500] = static_cast<char>(zip[pos + 1500] ^ 0xff);
    { std::ofstream(s / "bad.zip", std::ios::binary) << zip; }

    const auto report = verifyArchive(s / "bad.zip");
    CHECK(!report.ok());
    CHECK(report.problems.size() == 1);
    CHECK(!report.problems.empty() && report.problems.front().starts_with("photo.JPG"));
    CHECK(report.checked == kFiles.size() - 1);
}

void test_listing_and_reading() {
    Scratch s("list");
    const auto tree = makeTree(s / "tree");
    archiveDirectory(tree, s / "a.zip", typicalOptions());

    const auto entries = listEntries(s / "a.zip");
    const auto sum = summarize(entries);
    CHECK(sum.entries == entries.size());
    CHECK(sum.files == kFiles.size() && sum.directories == 5);
    CHECK(!sum.needsPassword());

    const std::string table = formatListing(entries);
    CHECK(table.find("guide.md") != std::string::npos);
    CHECK(table.find("name") != std::string::npos);
    CHECK(table.find(std::to_string(entries.size()) + " entries") != std::string::npos);

    CHECK(readText(s / "a.zip", "src/main.cpp") == "int main() {}\n");
    CHECK_THROWS_CODE(readBytes(s / "a.zip", "does/not/exist"), ZIP_ER_NOENT);
}

void test_extract_filters_and_errors() {
    Scratch s("unpack");
    const auto tree = makeTree(s / "tree");
    archiveDirectory(tree, s / "a.zip", typicalOptions());

    UnpackOptions o;
    o.filter.include = {"*.h", "*.md"};
    const auto r = extractArchive(s / "a.zip", s / "part", o);
    CHECK(r.ok() && r.extracted == 2 && r.filtered > 0);
    CHECK(snapshot(s / "part").size() == 2);
    CHECK(fs::exists(s / "part" / "src/util/helper.h"));

    // Existing files: refused unless overwrite; continueOnError reports instead of throwing.
    CHECK_THROWS_CODE(extractArchive(s / "a.zip", s / "part", o), ZIP_ER_EXISTS);
    UnpackOptions keepGoing = o;
    keepGoing.continueOnError = true;
    const auto again = extractArchive(s / "a.zip", s / "part", keepGoing);
    CHECK(again.failures.size() == 2 && again.extracted == 0);
    CHECK(again.failures.front().zipCode == ZIP_ER_EXISTS);
    keepGoing.overwrite = true;
    CHECK(extractArchive(s / "a.zip", s / "part", keepGoing).ok());

    // Progress and cancellation.
    std::size_t lastDone = 0, total = 0;
    UnpackOptions p;
    p.progress = [&](std::size_t done, std::size_t t, std::string_view) { lastDone = done; total = t; return true; };
    extractArchive(s / "a.zip", s / "all", p);
    CHECK(total > 0 && lastDone == total);
    p.progress = [](std::size_t done, std::size_t, std::string_view) { return done < 2; };
    CHECK_THROWS_CODE(extractArchive(s / "a.zip", s / "cancelled", p), ZIP_ER_CANCELLED);

    // Hostile archive: unsafe entries fail, the rest is still extracted.
    {
        auto a = Archive::create(s / "evil.zip");
        const std::string ok = "fine", pwn = "pwn";
        a.add("fine.txt", std::as_bytes(std::span{ok.data(), ok.size()}));
        a.add("../escaped.txt", std::as_bytes(std::span{pwn.data(), pwn.size()}));
        a.add("/abs_escaped.txt", std::as_bytes(std::span{pwn.data(), pwn.size()}));
        a.close();
    }
    CHECK_THROWS_CODE(extractArchive(s / "evil.zip", s / "safe"), ZIP_ER_INVAL);
    UnpackOptions cont;
    cont.continueOnError = true;
    const auto er = extractArchive(s / "evil.zip", s / "safe2", cont);
    CHECK(er.extracted == 1 && er.failures.size() == 2);
    CHECK(std::ranges::all_of(er.failures, [](const EntryFailure& f) { return f.zipCode == ZIP_ER_INVAL; }));
    CHECK(fs::exists(s / "safe2" / "fine.txt"));
    CHECK(!fs::exists(s / "escaped.txt") && !fs::exists("/abs_escaped.txt"));
}

void test_rewrite_preserves_methods() {
    Scratch s("methods");
    const auto tree = makeTree(s / "tree");

    // data.csv is highly compressible but stored on purpose: only an explicit
    // "keep the method" makes the rewritten archive store it again.
    ArchiveOptions o = typicalOptions();
    o.storeExtensions = {".csv"};
    archiveDirectory(tree, s / "src.zip", o);

    changeProtection(s / "src.zip", s / "copy.zip", ProtectOptions{});   // no password: plain copy
    for (const auto& e : listFiles(s / "copy.zip")) {
        if (e.name == "docs/deep/data.csv") CHECK(e.method == ZIP_CM_STORE && e.packedSize == e.size);
        if (e.name == "readme.txt") CHECK(e.method == ZIP_CM_DEFLATE);
    }
    CHECK(names(listEntries(s / "copy.zip")) == names(listEntries(s / "src.zip")));
    CHECK(verifyArchive(s / "copy.zip").ok());
}

void test_change_protection() {
    Scratch s("protect");
    const auto tree = makeTree(s / "tree");
    const std::chrono::sys_seconds stamp = std::chrono::sys_days{std::chrono::year{2021} / 3 / 4} + std::chrono::hours{8};
    setModificationTime(tree / "readme.txt", stamp);
    archiveDirectory(tree, s / "plain.zip", typicalOptions());

    ProtectOptions add;
    add.newPassword = "one";

    if (!kAes) {
        std::cout << "  SKIP AES tests; checking the refusal path instead\n";
        CHECK_THROWS_CODE(changeProtection(s / "plain.zip", s / "p.zip", add), ZIP_ER_ENCRNOTSUPP);
        CHECK(!fs::exists(s / "p.zip"));
        return;
    }

    // plain -> encrypted (new file)
    changeProtection(s / "plain.zip", s / "one.zip", add);
    CHECK(summarize(s / "one.zip").encryptedEntries == kFiles.size());
    CHECK(readText(s / "one.zip", "readme.txt", std::string("one")) == readFile(tree / "readme.txt"));
    CHECK(verifyArchive(s / "one.zip", std::string("one")).ok());
    for (const auto& e : listEntries(s / "one.zip")) {
        if (e.name == "readme.txt")
            CHECK(std::abs(e.modified - static_cast<std::int64_t>(stamp.time_since_epoch().count())) <= 2);
        if (e.name == "photo.JPG") CHECK(e.method == ZIP_CM_STORE);      // method preserved
    }
    CHECK(summarize(s / "one.zip").directories == 5);                     // dirs preserved
    CHECK(fs::exists(s / "plain.zip") && summarize(s / "plain.zip").encryptedEntries == 0);  // input untouched

    // refuses: same path as output, existing output, missing old password, wrong old password
    CHECK_THROWS_CODE(changeProtection(s / "plain.zip", s / "plain.zip", add), ZIP_ER_INVAL);
    CHECK_THROWS_CODE(changeProtection(s / "plain.zip", s / "one.zip", add), ZIP_ER_EXISTS);
    ProtectOptions change;
    change.newPassword = "two";
    CHECK_THROWS_CODE(changeProtection(s / "one.zip", s / "x.zip", change), ZIP_ER_NOPASSWD);
    change.oldPassword = "wrong";
    CHECK_THROWS_CODE(changeProtection(s / "one.zip", s / "y.zip", change), ZIP_ER_WRONGPASSWD);
    CHECK(!fs::exists(s / "x.zip") && !fs::exists(s / "y.zip"));

    // change password in place
    change.oldPassword = "one";
    changeProtection(s / "one.zip", change);
    CHECK_THROWS_CODE(readBytes(s / "one.zip", "readme.txt", std::string("one")), ZIP_ER_WRONGPASSWD);
    CHECK(readText(s / "one.zip", "readme.txt", std::string("two")) == readFile(tree / "readme.txt"));
    CHECK(!fs::exists(s / "one.zip.protect-tmp"));

    // failing in place leaves the original byte-for-byte unchanged
    const auto snapshotBytes = readFile(s / "one.zip");
    ProtectOptions wrong;
    wrong.oldPassword = "not-it";
    wrong.newPassword = "three";
    CHECK_THROWS_CODE(changeProtection(s / "one.zip", wrong), ZIP_ER_WRONGPASSWD);
    CHECK(readFile(s / "one.zip") == snapshotBytes);
    CHECK(!fs::exists(s / "one.zip.protect-tmp"));

    // remove protection
    ProtectOptions strip;
    strip.oldPassword = "two";
    changeProtection(s / "one.zip", strip);
    CHECK(summarize(s / "one.zip").encryptedEntries == 0);
    CHECK(readText(s / "one.zip", "readme.txt") == readFile(tree / "readme.txt"));
    CHECK(verifyArchive(s / "one.zip").ok());

    // selective protection
    ProtectOptions selective;
    selective.newPassword = "sel";
    selective.encryptOnly = {"*.cpp"};
    changeProtection(s / "plain.zip", s / "sel.zip", selective);
    CHECK(summarize(s / "sel.zip").encryptedEntries == 2);
    CHECK(readText(s / "sel.zip", "readme.txt") == readFile(tree / "readme.txt"));

    // weak / invalid settings are rejected up front
    ProtectOptions weak;
    weak.newPassword = "x";
    weak.encryption = Encryption::Traditional;
    CHECK_THROWS_CODE(changeProtection(s / "plain.zip", s / "w.zip", weak), ZIP_ER_INVAL);
    CHECK(!fs::exists(s / "w.zip"));
}

} // namespace

#define RUN(fn)                                                              \
    do {                                                                     \
        std::cout << "[ RUN ] " #fn "\n";                                    \
        try { fn(); }                                                        \
        catch (const std::exception& e) {                                    \
            ++g_fail;                                                        \
            std::cerr << "FAIL " #fn ": unexpected exception: " << e.what() \
                      << '\n';                                               \
        }                                                                    \
    } while (0)

int main() {
    RUN(test_patterns);
    RUN(test_archive_directory);
    RUN(test_filters_and_prefix);
    RUN(test_archive_files);
    RUN(test_output_policy_and_errors);
    RUN(test_progress_and_cancel);
    RUN(test_compression_policy);
    RUN(test_encryption_transparent);
    RUN(test_protection_settings_rejected);
    RUN(test_verify_detects_corruption);
    RUN(test_listing_and_reading);
    RUN(test_extract_filters_and_errors);
    RUN(test_rewrite_preserves_methods);
    RUN(test_change_protection);

    std::cout << "\n" << g_pass << " checks passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}

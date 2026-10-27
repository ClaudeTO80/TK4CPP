// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file test_zip.cpp
 * @brief Self-contained tests for ZipWrapper/Zip.h (no external framework).
 *
 * Every test works in its own scratch directory under the system temp dir.
 * Tests that need an optional libzip backend (AES, bzip2, lzma, zstd) print
 * SKIP when libzip reports the method as unsupported.
 */

#include <ZipWrapper/Zip.h>

#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <iostream>

namespace fs = std::filesystem;

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

/// Expects Zip::Exception with the given ZIP_ER_* code.
#define CHECK_THROWS_CODE(expr, code)                                        \
    do {                                                                     \
        try {                                                                \
            (void)(expr);                                                    \
            ++g_fail;                                                        \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__              \
                      << "  no exception: " #expr << '\n';                   \
        } catch (const Zip::Exception& e) {                                  \
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

Bytes pattern(std::size_t n, unsigned mod = 251) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::byte>(i % mod);
    return b;
}

std::string toString(const Bytes& b) {
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

Bytes toBytes(std::string_view s) {
    Bytes b(s.size());
    std::memcpy(b.data(), s.data(), s.size());
    return b;
}

void writeFile(const fs::path& p, std::string_view content) {
    std::ofstream(p, std::ios::binary) << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

/// Fresh scratch directory, removed on destruction.
struct Scratch {
    fs::path dir;
    explicit Scratch(const char* name)
        : dir(fs::temp_directory_path() / (std::string("zipwrapper_") + name)) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(dir, ec); }
    fs::path operator/(const char* n) const { return dir / n; }
};

// ---------------------------------------------------------------------------

void test_buffer_roundtrip() {
    Scratch s("buffer");
    const auto data = pattern(5000);
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.add("data.bin", data);
        a.add("empty.bin", std::span<const std::byte>{});
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    CHECK(r.size() == 2);
    CHECK(r.entry("data.bin").readAll() == data);
    CHECK(r.entry("empty.bin").readAll().empty());
    CHECK(r.entry("data.bin").size() == 5000);
}

void test_add_file_roundtrip() {
    Scratch s("addfile");
    writeFile(s / "in.txt", "hello file");
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.addFile("in.txt", s / "in.txt");
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    CHECK(toString(r.entry("in.txt").readAll()) == "hello file");
}

void test_open_overload_regression() {
    // Archive::open(path) with the default flags used to be ambiguous with
    // the member open(std::string_view).
    Scratch s("overload");
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.add("x", toBytes("x"));
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip");
    CHECK(r.size() == 1);
}

void test_lookup_and_iteration() {
    Scratch s("iter");
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.mkdir("dir");
        a.add("dir/x.bin", pattern(10));
        a.add("y.bin", pattern(20));
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);

    static_assert(std::input_iterator<Zip::Archive::Iterator>);

    CHECK(r.contains("dir/"));
    CHECK(r.contains("y.bin"));
    CHECK(!r.contains("nope"));
    CHECK(!r.find("nope").has_value());
    CHECK_THROWS_CODE(r.entry("nope"), ZIP_ER_NOENT);
    CHECK_THROWS_CODE(r.entry(std::size_t{99}), ZIP_ER_INVAL);
    CHECK(r.entry("dir/").isDirectory());

    std::size_t count = 0;
    std::uint64_t total = 0;
    for (const auto& e : r) { ++count; total += e.size(); }
    CHECK(count == 3);
    CHECK(total == 30);
}

void test_copy_from() {
    Scratch s("copy");
    const auto data = pattern(5000);
    {
        auto a = Zip::Archive::create(s / "src.zip");
        a.add("d.bin", data);
        a.close();
    }
    auto src = Zip::Archive::open(s / "src.zip", Zip::OpenFlag::ReadOnly);
    {
        auto b = Zip::Archive::create(s / "dst.zip");
        b.copyFrom(src, src.find("d.bin").value(), "copy.bin");
        b.close();
    }
    auto c = Zip::Archive::open(s / "dst.zip", Zip::OpenFlag::ReadOnly);
    CHECK(c.entry("copy.bin").size() == 5000);
    CHECK(c.entry("copy.bin").readAll() == data);
}

void test_modify_existing() {
    Scratch s("modify");
    writeFile(s / "new.txt", "from file");
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.add("one", toBytes("1"));
        a.add("two", toBytes("2"));
        a.add("three", toBytes("3"));
        a.close();
    }
    {
        auto a = Zip::Archive::open(s / "a.zip");  // read/write
        a.replace(a.find("one").value(), toBytes("ONE"));
        a.replaceFile(a.find("two").value(), s / "new.txt");
        a.rename(a.find("three").value(), "renamed");
        a.add("extra", toBytes("x"));
        a.remove(a.find("extra").value());
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    CHECK(r.size() == 3);
    CHECK(toString(r.entry("one").readAll()) == "ONE");
    CHECK(toString(r.entry("two").readAll()) == "from file");
    CHECK(toString(r.entry("renamed").readAll()) == "3");
    CHECK(!r.contains("three"));
    CHECK(!r.contains("extra"));
    r.close();  // Windows cannot replace a.zip below while this handle keeps it open

    // overwrite flag on add
    {
        auto a = Zip::Archive::open(s / "a.zip");
        CHECK_THROWS_CODE(a.add("one", toBytes("dup")), ZIP_ER_EXISTS);
        a.add("one", toBytes("dup"), /*overwrite=*/true);
        a.close();
    }
    auto r2 = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    CHECK(toString(r2.entry("one").readAll()) == "dup");
}

void test_compression() {
    Scratch s("compress");
    Bytes data(100000);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<std::byte>("abcabcabd"[i % 9]);

    CHECK(Zip::Archive::compressionSupported(Zip::Compression::Store));
    CHECK(Zip::Archive::compressionSupported(Zip::Compression::Deflate));
    CHECK(Zip::Archive::compressionSupported(Zip::Compression::Store, false));

    {
        auto a = Zip::Archive::create(s / "a.zip");
        const auto stored = a.add("stored", data);
        const auto fast   = a.add("fast", data);
        const auto best   = a.add("best", data);
        const auto def    = a.add("def", data);
        a.setCompression(stored, Zip::Compression::Store);
        a.setCompression(fast,   Zip::Compression::Deflate, 1);
        a.setCompression(best,   Zip::Compression::Deflate, 9);
        a.setCompression(def,    Zip::Compression::Default);
        CHECK_THROWS_CODE(a.setCompression(def, Zip::Compression::Deflate, 10), ZIP_ER_INVAL);
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    const auto st = r.entry("stored").stat();
    const auto sf = r.entry("fast").stat();
    const auto sb = r.entry("best").stat();
    const auto sd = r.entry("def").stat();
    CHECK(st.compressionMethod == ZIP_CM_STORE);
    CHECK(st.compressedSize == st.size);
    CHECK(sf.compressionMethod == ZIP_CM_DEFLATE);
    CHECK(sf.compressedSize < sf.size);
    CHECK(sb.compressedSize <= sf.compressedSize);
    CHECK(sd.compressionMethod == ZIP_CM_DEFLATE);
    for (const char* n : {"stored", "fast", "best", "def"})
        CHECK(r.entry(n).readAll() == data);

    // Optional backends.
    struct Opt { Zip::Compression m; const char* name; };
    for (const auto& o : {Opt{Zip::Compression::BZip2, "bzip2"},
                          Opt{Zip::Compression::Lzma,  "lzma"},
                          Opt{Zip::Compression::Zstd,  "zstd"}}) {
        if (!Zip::Archive::compressionSupported(o.m)) {
            std::cout << "  SKIP " << o.name << " (not built into libzip)\n";
            continue;
        }
        const fs::path p = s.dir / (std::string(o.name) + ".zip");
        {
            auto a = Zip::Archive::create(p);
            a.setCompression(a.add("d", data), o.m);
            a.close();
        }
        auto rr = Zip::Archive::open(p, Zip::OpenFlag::ReadOnly);
        CHECK(rr.entry("d").stat().compressionMethod == static_cast<std::uint16_t>(o.m));
        CHECK(rr.entry("d").readAll() == data);
    }
}

void test_backend_detection() {
    Scratch s("backends");

    // The reported lists must agree with the single-method queries, and the
    // always-available methods must be present.
    for (const bool write : {true, false}) {
        const auto comps = Zip::Archive::supportedCompressions(write);
        const auto has = [&](Zip::Compression m) {
            return std::ranges::find(comps, m) != comps.end();
        };
        CHECK(has(Zip::Compression::Store));
        CHECK(has(Zip::Compression::Deflate));
        for (const auto m : {Zip::Compression::BZip2, Zip::Compression::Lzma,
                             Zip::Compression::Zstd})
            CHECK(has(m) == Zip::Archive::compressionSupported(m, write));
    }
    CHECK(!Zip::Archive::libzipVersion().empty());
    std::cout << "  libzip " << Zip::Archive::libzipVersion() << ": "
              << Zip::Archive::supportedCompressions().size()
              << " compression, "
              << Zip::Archive::supportedEncryptions().size()
              << " encryption methods writable\n";

    // Every method reported as unsupported must be refused cleanly, without
    // leaving a half-written archive (this is the interesting path on a
    // libzip built without the optional backends).
    auto a = Zip::Archive::create(s / "a.zip");
    const auto idx = a.add("x", pattern(100));
    for (const auto m : {Zip::Compression::BZip2, Zip::Compression::Lzma,
                         Zip::Compression::Zstd})
        if (!Zip::Archive::compressionSupported(m))
            CHECK_THROWS_CODE(a.setCompression(idx, m), ZIP_ER_COMPNOTSUPP);
    for (const auto m : {Zip::Encryption::AES128, Zip::Encryption::AES192,
                         Zip::Encryption::AES256})
        if (!Zip::Archive::encryptionSupported(m))
            CHECK_THROWS_CODE(a.setEncryption(idx, m, "pw"), ZIP_ER_ENCRNOTSUPP);
    a.close();
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    CHECK(r.entry("x").readAll() == pattern(100));
}

void test_encryption() {
    Scratch s("crypt");
    if (!Zip::Archive::encryptionSupported(Zip::Encryption::AES256)) {
        std::cout << "  SKIP AES-256 (libzip built without a crypto backend)\n";
        return;
    }
    const auto data = pattern(3000);
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.setEncryption(a.add("secret.bin", data), Zip::Encryption::AES256, "pw");
        a.add("plain.bin", toBytes("plain"));
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    CHECK(r.entry("secret.bin").stat().encryptionMethod == ZIP_EM_AES_256);
    CHECK(r.entry("plain.bin").stat().encryptionMethod == ZIP_EM_NONE);

    CHECK_THROWS_CODE(r.entry("secret.bin").readAll(), ZIP_ER_NOPASSWD);
    CHECK_THROWS_CODE(r.entry("secret.bin").openEncrypted("wrong"), ZIP_ER_WRONGPASSWD);
    CHECK(r.entry("secret.bin").openEncrypted("pw").readAll() == data);

    // extractAll without the password fails and leaves nothing behind.
    CHECK_THROWS_CODE(r.extractAll(s / "out_nopw"), ZIP_ER_NOPASSWD);
    CHECK(!fs::exists(s / "out_nopw" / "secret.bin"));

    Zip::ExtractOptions opt;
    opt.password = "pw";
    fs::remove_all(s / "out");
    r.extractAll(s / "out", opt);
    CHECK(readFile(s / "out" / "secret.bin") == toString(data));
    CHECK(readFile(s / "out" / "plain.bin") == "plain");
}

void test_seek_tell() {
    Scratch s("seek");
    const auto data = pattern(1000);
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.setCompression(a.add("d", data), Zip::Compression::Store);
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    auto f = r.openEntry("d");
    CHECK(f.seekable());
    f.seek(10);
    std::array<std::byte, 5> buf{};
    CHECK(f.read(buf) == 5);
    CHECK(f.tell() == 15);
    CHECK(std::equal(buf.begin(), buf.end(), data.begin() + 10));
    f.seek(-5, SEEK_END);
    CHECK(f.tell() == 995);
    f.close();
    CHECK_THROWS_CODE(f.tell(), ZIP_ER_INVAL);
}

void test_extract_all() {
    Scratch s("extract");
    const auto data = pattern(200000);
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.mkdir("sub");
        a.add("sub/big.bin", data);
        a.add("top.txt", toBytes("top"));
        a.close();
    }
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    r.extractAll(s / "out");
    CHECK(fs::is_directory(s / "out" / "sub"));
    CHECK(readFile(s / "out" / "sub" / "big.bin") == toString(data));
    CHECK(readFile(s / "out" / "top.txt") == "top");

    // existing files: refused by default, allowed with overwrite
    CHECK_THROWS_CODE(r.extractAll(s / "out"), ZIP_ER_EXISTS);
    writeFile(s / "out" / "top.txt", "changed");
    Zip::ExtractOptions opt;
    opt.overwrite = true;
    r.extractAll(s / "out", opt);
    CHECK(readFile(s / "out" / "top.txt") == "top");

    // createDirectories = false: parent missing -> failure
    Zip::ExtractOptions nodirs;
    nodirs.createDirectories = false;
    fs::remove_all(s / "out2");
    fs::create_directories(s / "out2");
    bool threw = false;
    try { r.entry("sub/big.bin").extract(s / "out2", nodirs); }
    catch (const Zip::Exception&) { threw = true; }
    CHECK(threw);
}

void test_zip_slip() {
    Scratch s("slip");
    const struct { const char* name; } bad[] = {
        {"../evil.txt"}, {"a/../../evil.txt"}, {"/abs_evil.txt"}};
    for (const auto& b : bad) {
        const fs::path z = s.dir / "slip.zip";
        fs::remove(z);
        {
            auto a = Zip::Archive::create(z);
            a.add(b.name, toBytes("pwn"));
            a.close();
        }
        auto r = Zip::Archive::open(z, Zip::OpenFlag::ReadOnly);
        CHECK_THROWS_CODE(r.extractAll(s / "dest"), ZIP_ER_INVAL);
        CHECK(!fs::exists(s / "evil.txt"));
        CHECK(!fs::exists("/abs_evil.txt"));
    }
}

void test_symlink_escape() {
#if defined(_WIN32)
    std::cout << "  SKIP symlink escape (POSIX only test)\n";
#else
    Scratch s("symlink");
    fs::create_directories(s / "dest");
    fs::create_directories(s / "outside");
    writeFile(s / "outside" / "target.txt", "original");

    // directory symlink: dest/link -> outside
    fs::create_directory_symlink(s / "outside", s / "dest" / "link");
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.add("link/pwned.txt", toBytes("pwn"));
        a.close();
    }
    {
        auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
        CHECK_THROWS_CODE(r.extractAll(s / "dest"), ZIP_ER_INVAL);
        CHECK(!fs::exists(s / "outside" / "pwned.txt"));
    }

    // file symlink: dest/f.txt -> outside/target.txt, overwrite enabled
    fs::create_symlink(s / "outside" / "target.txt", s / "dest" / "f.txt");
    fs::remove(s / "a.zip");
    {
        auto a = Zip::Archive::create(s / "a.zip");
        a.add("f.txt", toBytes("pwn"));
        a.close();
    }
    {
        auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
        Zip::ExtractOptions opt;
        opt.overwrite = true;
        CHECK_THROWS_CODE(r.extractAll(s / "dest", opt), ZIP_ER_INVAL);
        CHECK(readFile(s / "outside" / "target.txt") == "original");
    }
#endif
}

void test_lifecycle() {
    Scratch s("life");

    // Destroying without close() discards the changes.
    {
        auto a = Zip::Archive::create(s / "forgot.zip");
        a.add("x", toBytes("x"));
    }
    CHECK(!fs::exists(s / "forgot.zip"));

    // Move: the moved-to object owns the archive, the source is unusable.
    auto a = Zip::Archive::create(s / "moved.zip");
    a.add("x", toBytes("x"));
    Zip::Archive b = std::move(a);
    CHECK_THROWS_CODE(a.size(), ZIP_ER_INVAL);
    b.close();
    CHECK(fs::exists(s / "moved.zip"));

    // Use after close.
    CHECK_THROWS_CODE(b.size(), ZIP_ER_INVAL);
    CHECK_THROWS_CODE(b.close(), ZIP_ER_INVAL);

    // Open failures carry libzip codes.
    CHECK_THROWS_CODE(Zip::Archive::open(s / "nope.zip", Zip::OpenFlag::ReadOnly), ZIP_ER_NOENT);
    CHECK_THROWS_CODE(Zip::Archive::open(s / "moved.zip",
                      Zip::OpenFlag::Create | Zip::OpenFlag::Exclusive), ZIP_ER_EXISTS);

    // Explicit discard.
    auto c = Zip::Archive::create(s / "discarded.zip");
    c.add("x", toBytes("x"));
    c.discard();
    CHECK(!fs::exists(s / "discarded.zip"));
}

void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);  // an empty value removes the variable
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

void test_password_from_env() {
    const char* var = "ZIPWRAPPER_TEST_PASSWORD";

    unsetEnv(var);
    CHECK(!Zip::passwordFromEnv(var).has_value());   // unset
    setEnv(var, "");
    CHECK(!Zip::passwordFromEnv(var).has_value());   // empty counts as unset
    setEnv(var, "s3cret");
    CHECK(Zip::passwordFromEnv(var) == std::optional<std::string>("s3cret"));

#if defined(_WIN32)
    std::cout << "  SKIP non-ASCII environment test on Windows (setting it needs the wide API)\n";
#else
    // UTF-8 bytes must come back untouched ("päss wörd").
    setEnv(var, "p\xc3\xa4ss w\xc3\xb6rd");
    CHECK(Zip::passwordFromEnv(var) == std::optional<std::string>("p\xc3\xa4ss w\xc3\xb6rd"));
#endif
    unsetEnv(var);
}

void test_source_ownership() {
    Scratch s("source");
    auto a = Zip::Archive::create(s / "a.zip");

    // A Source that is never handed to an archive must not leak (LSan).
    { auto unused = Zip::Source::fromBuffer(pattern(100)); CHECK(unused.valid()); }

    auto src = Zip::Source::fromBuffer(toBytes("owned"));
    a.add("s", std::move(src));
    CHECK(!src.valid());  // NOLINT: moved-from state is specified (released)
    a.close();
    auto r = Zip::Archive::open(s / "a.zip", Zip::OpenFlag::ReadOnly);
    CHECK(toString(r.entry("s").readAll()) == "owned");

    // libzip opens file sources lazily: creating the source succeeds, the
    // error surfaces at close(), which leaves the archive open (see
    // @ref zip_commit) so the destructor discards it.
    auto bad = Zip::Archive::create(s / "bad.zip");
    bad.addFile("missing", s / "missing.txt");
    bool threw = false;
    try { bad.close(); } catch (const Zip::Exception& e) { threw = true; CHECK(e.zipCode() == ZIP_ER_READ); }
    CHECK(threw);
    CHECK(!fs::exists(s / "bad.zip"));
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
    RUN(test_buffer_roundtrip);
    RUN(test_add_file_roundtrip);
    RUN(test_open_overload_regression);
    RUN(test_lookup_and_iteration);
    RUN(test_copy_from);
    RUN(test_modify_existing);
    RUN(test_compression);
    RUN(test_backend_detection);
    RUN(test_encryption);
    RUN(test_seek_tell);
    RUN(test_extract_all);
    RUN(test_zip_slip);
    RUN(test_symlink_escape);
    RUN(test_lifecycle);
    RUN(test_password_from_env);
    RUN(test_source_ownership);

    std::cout << "\n" << g_pass << " checks passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}

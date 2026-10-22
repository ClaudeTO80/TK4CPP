/**
 * @file test_encode.cpp
 * @brief Self-contained unit tests for TK4CPP::Encode (no external framework needed).
 */
#include <TK4CPP/Encode/Base64.h>
#include <TK4CPP/Encode/UrlEncoding.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <random>
#include <string>
#include <vector>

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(...)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(__VA_ARGS__)) {                                                                \
            ++g_failures;                                                             \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #__VA_ARGS__ "\n"; \
        }                                                                             \
    } while (false)

using namespace TK4CPP::Encode;
using Alphabet = Base64::Alphabet;
using Padding  = Base64::Padding;
using Mode     = UrlEncoding::Mode;

/** @brief Decodes and returns the error, or a value-initialised Error with position SIZE_MAX if decoding succeeded. */
Error errorOf(const Base64& codec, std::string_view text) {
    auto r = codec.decode(text);
    return r ? Error{ErrorCode::InvalidCharacter, static_cast<std::size_t>(-1)} : r.error();
}

void testBase64Rfc4648Vectors() {
    struct V { std::string_view plain, padded, unpadded; };
    const V vectors[] = {
        {"",       "",         ""},
        {"f",      "Zg==",     "Zg"},
        {"fo",     "Zm8=",     "Zm8"},
        {"foo",    "Zm9v",     "Zm9v"},
        {"foob",   "Zm9vYg==", "Zm9vYg"},
        {"fooba",  "Zm9vYmE=", "Zm9vYmE"},
        {"foobar", "Zm9vYmFy", "Zm9vYmFy"},
    };
    const Base64 padded;
    const Base64 unpadded{Base64::Options{.padding = Padding::None}};
    for (const auto& v : vectors) {
        CHECK(padded.encode(v.plain) == v.padded);
        CHECK(unpadded.encode(v.plain) == v.unpadded);
        auto d1 = padded.decodeToString(v.padded);
        CHECK(d1 && *d1 == v.plain);
        auto d2 = unpadded.decodeToString(v.unpadded);
        CHECK(d2 && *d2 == v.plain);
        CHECK(Base64::encodeDefault(v.plain) == v.padded);
    }
}

void testBase64Alphabets() {
    const std::string_view data = "\xfb\xff";
    CHECK(Base64{}.encode(data) == "+/8=");
    CHECK(Base64{Base64::urlSafe()}.encode(data) == "-_8");

    auto d = Base64{Base64::urlSafe()}.decodeToString("-_8");
    CHECK(d && *d == data);

    // Each alphabet must reject the other one's specific symbols.
    CHECK(errorOf(Base64{Base64::urlSafe()}, "+/8").code == ErrorCode::InvalidCharacter);
    CHECK(errorOf(Base64{}, "-_8=").code == ErrorCode::InvalidCharacter);
}

void testBase64RoundTripAndSize() {
    std::mt19937 rng{12345};
    const Alphabet alphabets[] = {Alphabet::Standard, Alphabet::UrlSafe};
    const Padding  paddings[]  = {Padding::Required, Padding::Optional, Padding::None};
    const std::size_t lines[]  = {0, 4, 76};

    for (const auto alphabet : alphabets) {
        for (const auto padding : paddings) {
            for (const auto lineLength : lines) {
                const Base64 codec{Base64::Options{.alphabet = alphabet, .padding = padding,
                                                   .lineLength = lineLength, .ignoreWhitespace = true,
                                                   .strictTrailingBits = true}};
                for (std::size_t n = 0; n <= 300; ++n) {
                    std::vector<std::byte> data(n);
                    for (auto& b : data) {
                        b = static_cast<std::byte>(rng() & 0xFF);
                    }
                    const std::string text = codec.encode(data);
                    CHECK(text.size() == codec.encodedSize(n));
                    auto back = codec.decode(text);
                    CHECK(back && *back == data);
                    CHECK(!back || back->size() <= Base64::maxDecodedSize(text.size()));
                }
            }
        }
    }

    // All 256 byte values survive a round trip.
    std::vector<std::byte> all(256);
    for (std::size_t i = 0; i < all.size(); ++i) {
        all[i] = static_cast<std::byte>(i);
    }
    auto back = Base64::decodeDefault(Base64::encodeDefault(all));
    CHECK(back && *back == all);
}

void testBase64Mime() {
    const std::string input(100, 'A');
    const Base64 mime{Base64::mime()};
    const std::string text = mime.encode(input);

    // 100 bytes -> 136 characters -> lines of 76 and 60 separated by one CRLF.
    CHECK(text.size() == 76 + 2 + 60);
    CHECK(text.substr(76, 2) == "\r\n");
    CHECK(text.find("\r\n", 78) == std::string::npos);
    CHECK(text.back() != '\n');

    auto back = mime.decodeToString(text);
    CHECK(back && *back == input);

    // No trailing CRLF when the output is exactly one full line (57 bytes -> 76 chars).
    CHECK(mime.encode(std::string(57, 'x')).find('\r') == std::string::npos);
}

void testBase64Errors() {
    const Base64 strict;
    CHECK(errorOf(strict, "Zm9v!")      == (Error{ErrorCode::InvalidCharacter, 4}));
    CHECK(errorOf(strict, "Z")          == (Error{ErrorCode::InvalidLength, 1}));
    CHECK(errorOf(strict, "Zg=")        == (Error{ErrorCode::InvalidPadding, 2}));
    CHECK(errorOf(strict, "Zg")         == (Error{ErrorCode::InvalidPadding, 2}));
    CHECK(errorOf(strict, "Zm9v====")   == (Error{ErrorCode::InvalidPadding, 4}));
    CHECK(errorOf(strict, "====")       == (Error{ErrorCode::InvalidPadding, 0}));
    CHECK(errorOf(strict, "Zg==Zg==")   == (Error{ErrorCode::InvalidPadding, 4}));
    CHECK(errorOf(strict, "Zg=A")       == (Error{ErrorCode::InvalidPadding, 3}));
    CHECK(errorOf(strict, "Zm 9v")      == (Error{ErrorCode::InvalidCharacter, 2})); // whitespace not tolerated by default
    CHECK(errorOf(strict, "A=")         == (Error{ErrorCode::InvalidLength, 2}));

    // Padding policies.
    const Base64 optional{Base64::Options{.padding = Padding::Optional}};
    CHECK(optional.decodeToString("Zg").value_or("?") == "f");
    CHECK(optional.decodeToString("Zg==").value_or("?") == "f");
    CHECK(errorOf(optional, "Zg=").code == ErrorCode::InvalidPadding);

    const Base64 none{Base64::Options{.padding = Padding::None}};
    CHECK(errorOf(none, "Zg==") == (Error{ErrorCode::InvalidPadding, 2}));

    // Whitespace tolerance.
    const Base64 ws{Base64::Options{.ignoreWhitespace = true}};
    CHECK(ws.decodeToString(" Zm9v\r\nYmFy\t").value_or("?") == "foobar");
    CHECK(ws.decodeToString("Zg = =").value_or("?") == "f");

    // Canonical encodings.
    const Base64 canon{Base64::Options{.strictTrailingBits = true}};
    CHECK(canon.decode("Zg==").has_value());
    CHECK(errorOf(canon, "Zh==").code == ErrorCode::NonZeroTrailingBits);
    CHECK(errorOf(canon, "Zm9=").code == ErrorCode::NonZeroTrailingBits);
    CHECK(Base64{}.decodeToString("Zh==").value_or("?") == "f"); // lenient by default

    // Error text.
    CHECK(Error{ErrorCode::InvalidCharacter, 7}.message() == "invalid character at offset 7");
}

void testUrlEncode() {
    CHECK(UrlEncoding::encode("") == "");
    CHECK(UrlEncoding::encode("a b&c") == "a%20b%26c");
    CHECK(UrlEncoding::encode("AZaz09-._~") == "AZaz09-._~");
    CHECK(UrlEncoding::encode("/?#[]@!$'()*+,;=") == "%2F%3F%23%5B%5D%40%21%24%27%28%29%2A%2B%2C%3B%3D");
    CHECK(UrlEncoding::encode("\xc3\xa9") == "%C3%A9");               // UTF-8 'e acute'
    CHECK(UrlEncoding::encode(std::string_view{"a\0b", 3}) == "a%00b");
    CHECK(UrlEncoding::encode("%") == "%25");

    CHECK(UrlEncoding::encode("a b+c", Mode::Form) == "a+b%2Bc");
    CHECK(UrlEncoding::encode("a b+c", Mode::Component) == "a%20b%2Bc");
    CHECK(UrlEncoding::encode("/a b/\xc3\xa9", Mode::Path) == "/a%20b/%C3%A9");
    CHECK(UrlEncoding::encode("/", Mode::Component) == "%2F");
}

void testUrlDecode() {
    CHECK(UrlEncoding::decode("").value_or("?") == "");
    CHECK(UrlEncoding::decode("%41%62").value_or("?") == "Ab");
    CHECK(UrlEncoding::decode("%c3%A9").value_or("?") == "\xc3\xa9"); // mixed-case hex
    CHECK(UrlEncoding::decode("a+b").value_or("?") == "a+b");        // '+' is literal in Component mode
    CHECK(UrlEncoding::decode("a+b", Mode::Form).value_or("?") == "a b");
    CHECK(UrlEncoding::decode("a%2Bb", Mode::Form).value_or("?") == "a+b");

    auto nul = UrlEncoding::decode("a%00b");
    CHECK(nul && nul->size() == 3 && (*nul)[1] == '\0');

    CHECK(UrlEncoding::decode("%").error()    == (Error{ErrorCode::InvalidPercentEncoding, 0}));
    CHECK(UrlEncoding::decode("abc%").error() == (Error{ErrorCode::InvalidPercentEncoding, 3}));
    CHECK(UrlEncoding::decode("%4").error()   == (Error{ErrorCode::InvalidPercentEncoding, 0}));
    CHECK(UrlEncoding::decode("a%zz").error() == (Error{ErrorCode::InvalidPercentEncoding, 1}));
    CHECK(UrlEncoding::decode("%4g").error()  == (Error{ErrorCode::InvalidPercentEncoding, 0}));
    CHECK(UrlEncoding::decode("ok%41%").error() == (Error{ErrorCode::InvalidPercentEncoding, 5}));
}

void testUrlRoundTrip() {
    std::string all;
    for (int i = 0; i < 256; ++i) {
        all += static_cast<char>(i);
    }
    for (const auto mode : {Mode::Component, Mode::Path, Mode::Form}) {
        auto back = UrlEncoding::decode(UrlEncoding::encode(all, mode), mode);
        CHECK(back && *back == all);
    }

    std::mt19937 rng{777};
    for (int n = 0; n < 500; ++n) {
        std::string s(rng() % 64, '\0');
        for (auto& c : s) {
            c = static_cast<char>(rng() & 0xFF);
        }
        for (const auto mode : {Mode::Component, Mode::Path, Mode::Form}) {
            auto back = UrlEncoding::decode(UrlEncoding::encode(s, mode), mode);
            CHECK(back && *back == s);
        }
    }
}


// ---------------------------------------------------------------------------
// Incremental API, streams, files, folders, byte ranges
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;

std::string randomBytes(std::mt19937& rng, std::size_t n) {
    std::string s(n, '\0');
    for (auto& c : s) {
        c = static_cast<char>(rng() & 0xFF);
    }
    return s;
}

/** @brief Feeds the text to a Base64 decoder in chunks of the given size; returns decoded bytes or the first error. */
std::expected<std::string, Error> chunkedBase64Decode(const Base64::Options& o, std::string_view text, std::size_t chunk) {
    auto decoder = Base64{o}.makeDecoder();
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += chunk) {
        if (auto r = decoder.update(text.substr(i, chunk), out); !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = decoder.finish(out); !r) {
        return std::unexpected(r.error());
    }
    return out;
}

void testIncremental() {
    std::mt19937 rng{2024};
    const std::size_t chunks[] = {1, 2, 3, 5, 7, 64};
    const Base64::Options variants[] = {
        Base64::Options{},
        Base64::urlSafe(),
        Base64::mime(),
        Base64::Options{.padding = Padding::Optional, .lineLength = 5, .ignoreWhitespace = true},
    };

    for (const auto& options : variants) {
        for (std::size_t n : {0u, 1u, 2u, 3u, 4u, 5u, 100u, 1000u}) {
            const std::string data = randomBytes(rng, n);
            const Base64 codec{options};
            const std::string whole = codec.encode(data);

            for (const std::size_t chunk : chunks) {
                auto encoder = codec.makeEncoder();
                std::string out;
                const auto bytes = std::as_bytes(std::span{data.data(), data.size()});
                for (std::size_t i = 0; i < bytes.size(); i += chunk) {
                    encoder.update(bytes.subspan(i, std::min(chunk, bytes.size() - i)), out);
                }
                encoder.finish(out);
                CHECK(out == whole);

                auto back = chunkedBase64Decode(options, whole, chunk);
                CHECK(back && *back == data);
            }
        }
    }

    // The same encoder object can be reused after finish().
    auto encoder = Base64{}.makeEncoder();
    std::string a, b;
    encoder.update(std::as_bytes(std::span{"fo", 2}), a);
    encoder.finish(a);
    encoder.update(std::as_bytes(std::span{"fo", 2}), b);
    encoder.finish(b);
    CHECK(a == "Zm8=" && a == b);

    // Errors are reported identically (same code, same absolute offset) however the input is split.
    const std::string_view bad[] = {"Zm9v!AAA", "Zg==Zg==", "Zm9vY", "Zg=", "Zg", "Zm9v====", "A="};
    for (const auto text : bad) {
        const auto reference = chunkedBase64Decode(Base64::Options{}, text, text.size());
        CHECK(!reference);
        for (const std::size_t chunk : chunks) {
            const auto split = chunkedBase64Decode(Base64::Options{}, text, chunk);
            CHECK(!split && !reference && split.error() == reference.error());
        }
    }

    // A failed decoder keeps failing until finish() resets it.
    auto decoder = Base64{}.makeDecoder();
    std::string sink;
    CHECK(!decoder.update("!!!!", sink));
    CHECK(!decoder.update("Zm9v", sink));
    CHECK(!decoder.finish(sink));
    CHECK(decoder.update("Zm9v", sink).has_value() && decoder.finish(sink).has_value() && sink == "foo");

    // URL decoder: a %XX sequence split across chunks.
    for (const std::size_t chunk : chunks) {
        const std::string text = "a%20b%C3%A9+c%2B";
        UrlEncoding::Decoder dec{Mode::Form};
        std::string out;
        bool ok = true;
        for (std::size_t i = 0; i < text.size(); i += chunk) {
            ok = ok && dec.update(std::string_view{text}.substr(i, chunk), out).has_value();
        }
        ok = ok && dec.finish(out).has_value();
        CHECK(ok && out == "a b\xc3\xa9 c+");
    }
    {
        UrlEncoding::Decoder dec{Mode::Component};
        std::string out;
        CHECK(dec.update("ab%4", out).has_value());
        CHECK(dec.finish(out).error() == (Error{ErrorCode::InvalidPercentEncoding, 2}));
        CHECK(dec.update("%", out).has_value());                      // reset: offsets restart at 0
        CHECK(dec.update("zz", out).error() == (Error{ErrorCode::InvalidPercentEncoding, 0}));
    }
}

void testByteRanges() {
    const std::vector<std::uint8_t> u8{'f', 'o', 'o'};
    const std::vector<char>         chars{'f', 'o', 'o', 'b'};
    const std::array<unsigned char, 2> arr{'f', 'o'};
    const std::vector<std::byte>    bytes{std::byte{'f'}};
    const std::string_view          view = "fooba";

    CHECK(Base64::encodeDefault(u8) == "Zm9v");
    CHECK(Base64::encodeDefault(chars) == "Zm9vYg==");
    CHECK(Base64{}.encode(arr) == "Zm8=");
    CHECK(Base64::encodeDefault(bytes) == "Zg==");
    CHECK(Base64::encodeDefault(std::span<const std::byte>{bytes}) == "Zg==");
    CHECK(Base64::encodeDefault(view) == "Zm9vYmE=");
    CHECK(Base64::encodeDefault(std::string{"foobar"}) == "Zm9vYmFy");
    CHECK(Base64::encodeDefault("Hi") == "SGk=");                      // literal: the NUL terminator is not encoded
    CHECK(Base64::encodeDefault(std::string_view{"a\0b", 3}) == "YQBi"); // embedded NUL is data

    CHECK(UrlEncoding::encode(u8) == "foo");
    CHECK(UrlEncoding::encode(std::vector<std::uint8_t>{0xC3, 0xA9}) == "%C3%A9");
    CHECK(UrlEncoding::encode(std::array<char, 2>{' ', '/'}, Mode::Path) == "%20/");
}

void testStreams() {
    std::mt19937 rng{99};
    const std::string data = randomBytes(rng, 200'000); // spans several 64 KiB chunks

    for (const auto& options : {Base64::Options{}, Base64::urlSafe(), Base64::mime()}) {
        const Base64 codec{options};
        std::istringstream in(data, std::ios::binary);
        std::ostringstream encoded(std::ios::binary);
        CHECK(codec.encodeStream(in, encoded).has_value());
        CHECK(encoded.str() == codec.encode(data));

        std::istringstream back(encoded.str(), std::ios::binary);
        std::ostringstream decoded(std::ios::binary);
        CHECK(codec.decodeStream(back, decoded).has_value());
        CHECK(decoded.str() == data);
    }

    // Empty stream.
    {
        std::istringstream in;
        std::ostringstream out;
        CHECK(Base64{}.encodeStream(in, out).has_value() && out.str().empty());
    }

    // Decode error offset is relative to the stream start even beyond the first chunk.
    {
        std::string text(100'000, 'A');
        text[70'000] = '*';
        std::istringstream in(text);
        std::ostringstream out;
        auto r = Base64{}.decodeStream(in, out);
        CHECK(!r && r.error() == (Error{ErrorCode::InvalidCharacter, 70'000}));
    }

    // URL encoding through streams.
    {
        const std::string raw = "name=\xc3\xa9 & co";
        std::istringstream in(raw);
        std::ostringstream out;
        CHECK(UrlEncoding{Mode::Form}.encodeStream(in, out).has_value());
        CHECK(out.str() == "name%3D%C3%A9+%26+co");
        std::istringstream in2(out.str());
        std::ostringstream out2;
        CHECK(UrlEncoding{Mode::Form}.decodeStream(in2, out2).has_value() && out2.str() == raw);
    }

    // Output stream failure.
    {
        std::istringstream in(data);
        std::ostringstream out;
        out.setstate(std::ios::badbit);
        auto r = Base64{}.encodeStream(in, out);
        CHECK(!r && r.error().code == ErrorCode::IoError);
    }
    // Input stream already failed.
    {
        std::istringstream in("abc");
        in.setstate(std::ios::failbit);
        std::ostringstream out;
        auto r = Base64{}.encodeStream(in, out);
        CHECK(!r && r.error().code == ErrorCode::IoError);
    }
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void spit(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("tk4cpp_encode_test_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void testFiles() {
    TempDir tmp;
    std::mt19937 rng{5};
    const std::string data = randomBytes(rng, 150'000);
    const fs::path in = tmp.path / "in.bin";
    spit(in, data);

    const Base64 codec;
    CHECK(codec.encodeFile(in, tmp.path / "sub" / "dir" / "in.b64").has_value()); // parents are created
    CHECK(slurp(tmp.path / "sub" / "dir" / "in.b64") == codec.encode(data));
    CHECK(!fs::exists(tmp.path / "sub" / "dir" / "in.b64.tk4tmp"));

    CHECK(codec.decodeFile(tmp.path / "sub" / "dir" / "in.b64", tmp.path / "out.bin").has_value());
    CHECK(slurp(tmp.path / "out.bin") == data);

    auto asString = codec.encodeFileToString(in);
    CHECK(asString && *asString == codec.encode(data));
    auto asBytes = codec.decodeFileToBytes(tmp.path / "sub" / "dir" / "in.b64");
    CHECK(asBytes && asBytes->size() == data.size());
    auto asText = codec.decodeFileToString(tmp.path / "sub" / "dir" / "in.b64");
    CHECK(asText && *asText == data);

    // Overwrite policy.
    CHECK(!codec.encodeFile(in, tmp.path / "out.bin", FileOptions{.overwrite = false}));
    CHECK(slurp(tmp.path / "out.bin") == data); // untouched
    CHECK(codec.encodeFile(in, tmp.path / "out.bin").has_value());

    // Same file, missing input, directory as input, no parent creation.
    CHECK(codec.encodeFile(in, in).error().code == ErrorCode::IoError);
    CHECK(slurp(in) == data);
    CHECK(codec.encodeFile(tmp.path / "missing", tmp.path / "x").error().code == ErrorCode::IoError);
    CHECK(codec.encodeFile(tmp.path, tmp.path / "x").error().code == ErrorCode::IoError);
    CHECK(!codec.encodeFile(in, tmp.path / "nodir" / "x", FileOptions{.createDirectories = false}));

    // A decode failure must leave neither a partial output nor a temp file, and must not clobber an existing output.
    spit(tmp.path / "bad.b64", "Zm9v!!!!");
    spit(tmp.path / "keep.bin", "precious");
    auto r = codec.decodeFile(tmp.path / "bad.b64", tmp.path / "keep.bin");
    CHECK(!r && r.error().code == ErrorCode::InvalidCharacter && r.error().position == 4);
    CHECK(slurp(tmp.path / "keep.bin") == "precious");
    CHECK(!fs::exists(tmp.path / "keep.bin.tk4tmp"));
    auto r2 = codec.decodeFile(tmp.path / "bad.b64", tmp.path / "new.bin");
    CHECK(!r2 && !fs::exists(tmp.path / "new.bin") && !fs::exists(tmp.path / "new.bin.tk4tmp"));

    // URL encoding on files.
    spit(tmp.path / "q.txt", "a b&c=\xc3\xa9");
    CHECK(UrlEncoding{Mode::Component}.encodeFile(tmp.path / "q.txt", tmp.path / "q.enc").has_value());
    CHECK(slurp(tmp.path / "q.enc") == "a%20b%26c%3D%C3%A9");
    auto url = UrlEncoding{}.decodeFileToString(tmp.path / "q.enc");
    CHECK(url && *url == "a b&c=\xc3\xa9");
    spit(tmp.path / "q2.enc", "100%");
    CHECK(UrlEncoding{}.decodeFile(tmp.path / "q2.enc", tmp.path / "q2.txt").error().code == ErrorCode::InvalidPercentEncoding);
}

void testFolders() {
    TempDir tmp;
    std::mt19937 rng{11};
    const fs::path src = tmp.path / "src";
    const std::map<std::string, std::string> tree = {
        {"a.bin", randomBytes(rng, 1000)},
        {"empty.txt", ""},
        {"sub/b.bin", randomBytes(rng, 70'000)},
        {"sub/deep/c.bin", randomBytes(rng, 3)},
    };
    for (const auto& [name, content] : tree) {
        spit(src / name, content);
    }

    const Base64 codec;
    const fs::path enc = tmp.path / "enc";
    auto report = codec.encodeFolder(src, enc);
    CHECK(report && report->ok() && report->succeeded == 4 && report->failed == 0 && report->entries.size() == 4);
    for (const auto& [name, content] : tree) {
        CHECK(slurp(enc / (name + ".b64")) == codec.encode(content));
    }

    // Round trip into another folder (+ a foreign file that must be skipped).
    spit(enc / "notes.txt", "ignore me");
    const fs::path dec = tmp.path / "dec";
    auto back = codec.decodeFolder(enc, dec);
    CHECK(back && back->ok() && back->succeeded == 4 && back->skipped == 1);
    for (const auto& [name, content] : tree) {
        CHECK(slurp(dec / name) == content);
    }
    CHECK(!fs::exists(dec / "notes.txt"));

    // Non recursive: only the top-level files.
    auto flat = codec.encodeFolder(src, tmp.path / "flat", FolderOptions{.recursive = false});
    CHECK(flat && flat->succeeded == 2 && !fs::exists(tmp.path / "flat" / "sub"));

    // Custom suffix.
    auto custom = codec.encodeFolder(src, tmp.path / "custom", FolderOptions{.suffix = ".txt64"});
    CHECK(custom && custom->succeeded == 4 && fs::exists(tmp.path / "custom" / "sub" / "deep" / "c.bin.txt64"));
    auto customBack = codec.decodeFolder(tmp.path / "custom", tmp.path / "custom_dec", FolderOptions{.suffix = ".txt64"});
    CHECK(customBack && customBack->succeeded == 4 && slurp(tmp.path / "custom_dec" / "a.bin") == tree.at("a.bin"));

    // overwrite = false reports failures per file instead of aborting.
    auto again = codec.encodeFolder(src, enc, FolderOptions{.overwrite = false, .suffix = std::string{".b64"}});
    CHECK(again && !again->ok() && again->failed == 4 && again->succeeded == 0);

    // Per-file failure is reported and does not stop the run, unless stopOnError.
    spit(enc / "broken.b64", "@@@@");
    auto mixed = codec.decodeFolder(enc, tmp.path / "dec2");
    CHECK(mixed && mixed->failed == 1 && mixed->succeeded == 4);
    auto stopped = codec.decodeFolder(enc, tmp.path / "dec3", FolderOptions{.stopOnError = true});
    CHECK(stopped && stopped->failed == 1 && stopped->entries.size() < 5);

    // Invalid input folder.
    CHECK(codec.encodeFolder(tmp.path / "nope", tmp.path / "x").error().code == ErrorCode::IoError);

    // Output folder nested in the input folder is not re-read, and running twice is idempotent.
    // (Uses its own copy of the tree so that the output folder does not pollute the cases above.)
    const fs::path src2 = tmp.path / "src2";
    for (const auto& [name, content] : tree) {
        spit(src2 / name, content);
    }
    auto nested = codec.encodeFolder(src2, src2 / "out");
    CHECK(nested && nested->succeeded == 4 && nested->skipped == 0);
    auto nested2 = codec.encodeFolder(src2, src2 / "out");
    CHECK(nested2 && nested2->succeeded == 4 && nested2->skipped == 4); // the 4 files under out/ are skipped
    CHECK(!fs::exists(src2 / "out" / "out"));

    // URL encoding on folders uses its own default suffix.
    auto urls = UrlEncoding{}.encodeFolder(src, tmp.path / "urls");
    CHECK(urls && urls->succeeded == 4 && fs::exists(tmp.path / "urls" / "empty.txt.urlenc"));
}

} // namespace

int main() {
    testBase64Rfc4648Vectors();
    testBase64Alphabets();
    testBase64RoundTripAndSize();
    testBase64Mime();
    testBase64Errors();
    testUrlEncode();
    testUrlDecode();
    testUrlRoundTrip();
    testIncremental();
    testByteRanges();
    testStreams();
    testFiles();
    testFolders();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file Zip.cpp
 * @brief Implementation of the core layer declared in Zip.h.
 */

#include <ZipWrapper/Zip.h>

#include "Detail.h"

#include <cstdlib>
#include <cstring>
#include <fstream>

namespace Zip {

std::string errorMessage(zip_t* archive) {
    if (!archive) return "libzip: null archive";
    const char* s = zip_strerror(archive);
    return s ? std::string{s} : "libzip: unknown error";
}

void throwArchiveError(zip_t* archive, std::string_view operation) {
    const zip_error_t* e = archive ? zip_get_error(archive) : nullptr;
    const int z = e ? zip_error_code_zip(e) : ZIP_ER_INTERNAL;
    const int sy = e ? zip_error_code_system(e) : 0;
    std::string msg(operation);
    msg += ": ";
    msg += errorMessage(archive);
    throw Exception(z, sy, std::move(msg));
}

void throwFileError(zip_t* archive, zip_file_t* file, std::string_view operation) {
    const char* s = file ? zip_file_strerror(file) : nullptr;
    std::string msg(operation);
    msg += ": ";
    msg += s ? s : errorMessage(archive).c_str();
    int z = ZIP_ER_INTERNAL;
    int sy = 0;
    if (file) {
        const zip_error_t* e = zip_file_get_error(file);
        if (e) {
            z = zip_error_code_zip(e);
            sy = zip_error_code_system(e);
        }
    }
    throw Exception(z, sy, std::move(msg));
}

std::optional<std::string> passwordFromEnv(const char* variable) {
#if defined(_WIN32)
    wchar_t* buffer = nullptr;
    std::size_t length = 0;
    const std::wstring name = std::filesystem::path(variable).wstring();
    if (_wdupenv_s(&buffer, &length, name.c_str()) != 0 || !buffer)
        return std::nullopt;
    const std::wstring wide(buffer);
    std::free(buffer);
    if (wide.empty()) return std::nullopt;
    const auto u8 = std::filesystem::path(wide).u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    const char* value = std::getenv(variable);
    if (!value || !*value) return std::nullopt;
    return std::string(value);
#endif
}

Source::~Source() { reset(); }

Source::Source(Source&& o) noexcept
    : source_(std::exchange(o.source_, nullptr)) {}

Source& Source::operator=(Source&& o) noexcept {
    if (this != &o) {
        reset();
        source_ = std::exchange(o.source_, nullptr);
    }
    return *this;
}

Source Source::fromFile(const std::filesystem::path& path, std::uint64_t start, std::int64_t length) {
    const std::string name = detail::toUtf8(path);
    zip_error_t ze;
    zip_error_init(&ze);
    Source s;
    s.source_ = zip_source_file_create(
        name.c_str(), start, static_cast<zip_int64_t>(length), &ze);
    if (!s.source_) detail::throwZipError(ze, "zip_source_file_create");
    zip_error_fini(&ze);
    return s;
}

Source Source::fromBuffer(std::span<const std::byte> data) {
    zip_error_t ze;
    zip_error_init(&ze);
    Source s;

    if (data.empty()) {
        s.source_ = zip_source_buffer_create(nullptr, 0, 0, &ze);
        if (!s.source_) detail::throwZipError(ze, "zip_source_buffer_create");
        zip_error_fini(&ze);
        return s;
    }

    // libzip reads the source later (during zip_close()), so the bytes
    // must outlive this call. freep = 1 hands the malloc'ed block to
    // libzip, which releases it when the source is destroyed.
    void* memory = std::malloc(data.size());
    if (!memory) {
        zip_error_fini(&ze);
        throw Exception(ZIP_ER_MEMORY, 0, "zip buffer allocation failed");
    }
    std::memcpy(memory, data.data(), data.size());

    s.source_ = zip_source_buffer_create(memory, data.size(), 1, &ze);
    if (!s.source_) {
        std::free(memory);
        detail::throwZipError(ze, "zip_source_buffer_create");
    }
    zip_error_fini(&ze);
    return s;
}

void Source::reset() noexcept {
    if (source_) zip_source_free(source_);
    source_ = nullptr;
}

File::File(zip_t* archive, zip_file_t* file)
    : archive_(archive), file_(file) {}

File::~File() { reset(); }

File::File(File&& o) noexcept
    : archive_(std::exchange(o.archive_, nullptr)), file_(std::exchange(o.file_, nullptr)) {}

File& File::operator=(File&& o) noexcept {
    if (this != &o) {
        reset();
        archive_ = std::exchange(o.archive_, nullptr);
        file_ = std::exchange(o.file_, nullptr);
    }
    return *this;
}

std::size_t File::read(std::span<std::byte> buffer) {
    if (!file_) throw Exception(ZIP_ER_INVAL, 0, "read on closed ZIP file");
    if (buffer.empty()) return 0;
    const auto n = zip_fread(file_, buffer.data(), buffer.size());
    if (n < 0) throwFileError(archive_, file_, "zip_fread");
    return static_cast<std::size_t>(n);
}

std::vector<std::byte> File::readAll() {
    if (!file_) throw Exception(ZIP_ER_INVAL, 0, "readAll on closed ZIP file");
    std::vector<std::byte> out;
    std::vector<std::byte> buffer(64 * 1024);
    for (;;) {
        const auto n = read(buffer);
        if (!n) break;
        out.insert(out.end(), buffer.begin(),
                   buffer.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return out;
}

bool File::seekable() const {
    if (!file_) return false;
    const int r = zip_file_is_seekable(file_);
    if (r < 0) throwFileError(archive_, file_, "zip_file_is_seekable");
    return r != 0;
}

void File::seek(std::int64_t offset, int whence) {
    if (!file_) throw Exception(ZIP_ER_INVAL, 0, "seek on closed ZIP file");
    if (zip_fseek(file_, static_cast<zip_int64_t>(offset), whence) < 0)
        throwFileError(archive_, file_, "zip_fseek");
}

std::int64_t File::tell() const {
    if (!file_) throw Exception(ZIP_ER_INVAL, 0, "tell on closed ZIP file");
    const auto p = zip_ftell(file_);
    if (p < 0) throwFileError(archive_, file_, "zip_ftell");
    return static_cast<std::int64_t>(p);
}

void File::close() {
    if (!file_) return;
    const int r = zip_fclose(file_);
    file_ = nullptr;
    archive_ = nullptr;
    if (r != 0) throw Exception(r, 0, "zip_fclose failed");
}

void File::reset() noexcept {
    if (file_) zip_fclose(file_);
    file_ = nullptr;
    archive_ = nullptr;
}

Archive::~Archive() { discardNoThrow(); }

Archive::Archive(Archive&& o) noexcept
    : archive_(std::exchange(o.archive_, nullptr)), path_(std::move(o.path_)), closed_(std::exchange(o.closed_, false)), callbacks_(std::move(o.callbacks_)) {}

Archive& Archive::operator=(Archive&& o) noexcept {
    if (this != &o) {
        discardNoThrow();
        archive_ = std::exchange(o.archive_, nullptr);
        path_ = std::move(o.path_);
        closed_ = std::exchange(o.closed_, false);
        callbacks_ = std::move(o.callbacks_);
    }
    return *this;
}

Archive Archive::open(const std::filesystem::path& path, OpenFlag flags) {
    return Archive(path, nativeOpenFlags(flags));
}

Archive Archive::create(const std::filesystem::path& path) {
    return Archive(path, ZIP_CREATE | ZIP_TRUNCATE);
}

std::size_t Archive::size() const {
    ensureOpen();
    return static_cast<std::size_t>(zip_get_num_entries(archive_, 0));
}

std::optional<std::size_t> Archive::find(std::string_view name) const {
    ensureOpen();
    std::string n(name);
    const auto i = zip_name_locate(archive_, n.c_str(), ZIP_FL_ENC_UTF_8);
    if (i < 0) {
        const auto* e = zip_get_error(archive_);
        if (e && zip_error_code_zip(e) == ZIP_ER_NOENT)
            return std::nullopt;
        throwArchiveError(archive_, "zip_name_locate");
    }
    return static_cast<std::size_t>(i);
}

Entry Archive::entry(std::size_t index) {
    ensureOpen();
    if (index >= size())
        throw Exception(ZIP_ER_INVAL, 0, "ZIP entry index out of range");
    return Entry(this, index);
}

Entry Archive::entry(std::string_view name) {
    const auto i = find(name);
    if (!i) throw Exception(ZIP_ER_NOENT, 0, "ZIP entry not found: " + std::string(name));
    return Entry(this, *i);
}

std::size_t Archive::add(std::string_view name, std::span<const std::byte> data, bool overwrite) {
    ensureOpen();
    auto source = Source::fromBuffer(data);
    return addSource(name, source, overwrite);
}

std::size_t Archive::addFile(std::string_view name, const std::filesystem::path& file, bool overwrite) {
    ensureOpen();
    auto source = Source::fromFile(file);
    return addSource(name, source, overwrite);
}

std::size_t Archive::add(std::string_view name, Source&& source, bool overwrite) {
    ensureOpen();
    return addSource(name, source, overwrite);
}

void Archive::replace(std::size_t index, std::span<const std::byte> data) {
    ensureOpen();
    auto source = Source::fromBuffer(data);
    replaceSource(index, source);
}

void Archive::replaceFile(std::size_t index, const std::filesystem::path& file) {
    ensureOpen();
    auto source = Source::fromFile(file);
    replaceSource(index, source);
}

void Archive::remove(std::size_t index) {
    ensureOpen();
    if (zip_delete(archive_, static_cast<zip_uint64_t>(index)) < 0)
        throwArchiveError(archive_, "zip_delete");
}

void Archive::rename(std::size_t index, std::string_view newName) {
    ensureOpen();
    std::string n(newName);
    if (zip_file_rename(archive_, index, n.c_str(), ZIP_FL_ENC_UTF_8) < 0)
        throwArchiveError(archive_, "zip_file_rename");
}

void Archive::mkdir(std::string_view name) {
    ensureOpen();
    std::string n(name);
    if (!n.ends_with('/')) n.push_back('/');
    if (zip_dir_add(archive_, n.c_str(), ZIP_FL_ENC_UTF_8) < 0)
        throwArchiveError(archive_, "zip_dir_add");
}

void Archive::setCompression(std::size_t index, Compression method, unsigned level) {
    ensureOpen();
    if (level > 9)
        throw Exception(ZIP_ER_INVAL, 0, "compression level must be 0..9");
    if (zip_set_file_compression(archive_, index,
            static_cast<zip_int32_t>(method),
            static_cast<zip_uint32_t>(level)) < 0)
        throwArchiveError(archive_, "zip_set_file_compression");
}

void Archive::setEncryption(std::size_t index, Encryption method, std::string_view password) {
    ensureOpen();
    const std::string p(password);
    const char* pw = method == Encryption::None ? nullptr : p.c_str();
    if (zip_file_set_encryption(
            archive_, index, static_cast<zip_uint16_t>(method), pw) < 0)
        throwArchiveError(archive_, "zip_file_set_encryption");
}

bool Archive::compressionSupported(Compression method, bool compress) {
    return zip_compression_method_supported(
        static_cast<zip_int32_t>(method), compress ? 1 : 0) != 0;
}

bool Archive::encryptionSupported(Encryption method, bool encrypt) {
    return zip_encryption_method_supported(
        static_cast<zip_uint16_t>(method), encrypt ? 1 : 0) != 0;
}

std::vector<Compression> Archive::supportedCompressions(bool compress) {
    std::vector<Compression> out;
    for (const auto m : {Compression::Store, Compression::Deflate,
                         Compression::BZip2, Compression::Lzma,
                         Compression::Zstd})
        if (compressionSupported(m, compress)) out.push_back(m);
    return out;
}

std::vector<Encryption> Archive::supportedEncryptions(bool encrypt) {
    std::vector<Encryption> out;
    for (const auto m : {Encryption::AES128, Encryption::AES192,
                         Encryption::AES256, Encryption::Traditional})
        if (encryptionSupported(m, encrypt)) out.push_back(m);
    return out;
}

std::string_view Archive::libzipVersion() noexcept {
    const char* v = zip_libzip_version();
    return v ? std::string_view{v} : std::string_view{"unknown"};
}

File Archive::openEntry(std::size_t index) {
    ensureOpen();
    auto* f = zip_fopen_index(archive_, index, 0);
    if (!f) throwArchiveError(archive_, "zip_fopen_index");
    return File(archive_, f);
}

File Archive::openEntry(std::string_view name) {
    const auto i = find(name);
    if (!i) throw Exception(ZIP_ER_NOENT, 0, "ZIP entry not found: " + std::string(name));
    return openEntry(*i);
}

File Archive::openEntryEncrypted(std::size_t index, std::string_view password) {
    ensureOpen();
    const std::string p(password);
    auto* f = zip_fopen_index_encrypted(archive_, index, 0, p.c_str());
    if (!f) throwArchiveError(archive_, "zip_fopen_index_encrypted");
    return File(archive_, f);
}

void Archive::extractAll(const std::filesystem::path& destination, ExtractOptions options) {
    ensureOpen();
    const std::size_t n = size();
    for (std::size_t i = 0; i < n; ++i)
        entry(i).extract(destination, options);
}

std::size_t Archive::copyFrom(Archive& source, std::size_t sourceIndex, std::string_view destinationName, bool overwrite) {
    ensureOpen();
    source.ensureOpen();
    auto* zs = zip_source_zip_file(
        archive_, source.archive_, sourceIndex, 0, 0,
        detail::kLengthToEnd, nullptr);
    if (!zs) throwArchiveError(archive_, "zip_source_zip_file");

    zip_flags_t flags = ZIP_FL_ENC_UTF_8;
    if (overwrite) flags |= ZIP_FL_OVERWRITE;
    const std::string n(destinationName);
    const auto r = zip_file_add(archive_, n.c_str(), zs, flags);
    if (r < 0) {
        zip_source_free(zs);
        throwArchiveError(archive_, "zip_file_add(copyFrom)");
    }
    return static_cast<std::size_t>(r);
}

void Archive::close() {
    ensureOpen();
    if (zip_close(archive_) < 0) {
        // An exception thrown by the progress callback takes precedence
        // over libzip's generic "operation cancelled" error.
        if (callbacks_ && callbacks_->error)
            std::rethrow_exception(std::exchange(callbacks_->error, nullptr));
        throwArchiveError(archive_, "zip_close");
    }
    archive_ = nullptr;
    closed_ = true;
    callbacks_.reset();
}

void Archive::setProgressCallback(std::function<bool(double)> callback, double precision) {
    ensureOpen();
    if (!callback) {
        zip_register_progress_callback_with_state(archive_, 0.0, nullptr, nullptr, nullptr);
        zip_register_cancel_callback_with_state(archive_, nullptr, nullptr, nullptr);
        callbacks_.reset();
        return;
    }
    auto state = std::make_unique<detail::CallbackState>();
    state->progress = std::move(callback);
    if (zip_register_progress_callback_with_state(
            archive_, precision, &detail::progressTrampoline, nullptr, state.get()) < 0 ||
        zip_register_cancel_callback_with_state(
            archive_, &detail::cancelTrampoline, nullptr, state.get()) < 0)
        throwArchiveError(archive_, "zip_register_*_callback_with_state");
    callbacks_ = std::move(state);
}

void Archive::setDefaultPassword(std::string_view password) {
    ensureOpen();
    const std::string p(password);
    if (zip_set_default_password(archive_, p.empty() ? nullptr : p.c_str()) < 0)
        throwArchiveError(archive_, "zip_set_default_password");
}

void Archive::discard() noexcept { discardNoThrow(); }

Archive::Archive(const std::filesystem::path& path, int flags)
    : path_(path) {
    const std::string u8 = detail::toUtf8(path);
    int error = ZIP_ER_OK;
    archive_ = zip_open(u8.c_str(), flags, &error);
    if (!archive_) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, error);
        detail::throwZipError(ze, "zip_open");
    }
}

void Archive::ensureOpen() const {
    if (!archive_ || closed_)
        throw Exception(ZIP_ER_INVAL, 0, "ZIP archive is closed");
}

std::size_t Archive::addSource(std::string_view name, Source& source, bool overwrite) {
    if (!source.valid())
        throw Exception(ZIP_ER_INVAL, 0, "invalid ZIP source");
    const std::string n(name);
    zip_flags_t flags = ZIP_FL_ENC_UTF_8;
    if (overwrite) flags |= ZIP_FL_OVERWRITE;
    auto* raw = source.release();
    const auto r = zip_file_add(archive_, n.c_str(), raw, flags);
    if (r < 0) {
        zip_source_free(raw);
        throwArchiveError(archive_, "zip_file_add");
    }
    return static_cast<std::size_t>(r);
}

void Archive::replaceSource(std::size_t index, Source& source) {
    if (!source.valid())
        throw Exception(ZIP_ER_INVAL, 0, "invalid ZIP source");
    auto* raw = source.release();
    if (zip_file_replace(archive_, index, raw, ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(raw);
        throwArchiveError(archive_, "zip_file_replace");
    }
}

void Archive::discardNoThrow() noexcept {
    if (archive_) zip_discard(archive_);
    archive_ = nullptr;
    closed_ = true;
    callbacks_.reset();  // after zip_discard: libzip may call it until then
}

Stat Entry::stat() const {
    if (!archive_) throw Exception(ZIP_ER_INVAL, 0, "invalid ZIP entry");
    archive_->ensureOpen();
    zip_stat_t s;
    zip_stat_init(&s);
    if (zip_stat_index(archive_->archive_, index_, 0, &s) < 0)
        throwArchiveError(archive_->archive_, "zip_stat_index");

    Stat r;
    if (s.valid & ZIP_STAT_NAME) r.name = s.name ? s.name : "";
    if (s.valid & ZIP_STAT_INDEX) r.index = s.index;
    if (s.valid & ZIP_STAT_SIZE) r.size = s.size;
    if (s.valid & ZIP_STAT_COMP_SIZE) r.compressedSize = s.comp_size;
    if (s.valid & ZIP_STAT_MTIME) r.modificationTime = static_cast<std::int64_t>(s.mtime);
    if (s.valid & ZIP_STAT_CRC) r.crc = s.crc;
    if (s.valid & ZIP_STAT_COMP_METHOD) r.compressionMethod = s.comp_method;
    if (s.valid & ZIP_STAT_ENCRYPTION_METHOD) r.encryptionMethod = s.encryption_method;
    if (s.valid & ZIP_STAT_FLAGS) r.flags = s.flags;
    r.directory = !r.name.empty() && r.name.back() == '/';
    return r;
}

File Entry::open() const {
    if (!archive_) throw Exception(ZIP_ER_INVAL, 0, "invalid ZIP entry");
    return archive_->openEntry(index_);
}

File Entry::openEncrypted(std::string_view password) const {
    if (!archive_) throw Exception(ZIP_ER_INVAL, 0, "invalid ZIP entry");
    return archive_->openEntryEncrypted(index_, password);
}

std::vector<std::byte> Entry::readAll() const {
    return open().readAll();
}

void Entry::extract(const std::filesystem::path& destination, ExtractOptions options) const {
    namespace fs = std::filesystem;
    if (!archive_) throw Exception(ZIP_ER_INVAL, 0, "invalid ZIP entry");

    const Stat st = stat();
    const fs::path relative = detail::fromUtf8(st.name);

    if (options.rejectUnsafePaths) {
        if (relative.empty() || relative.is_absolute() ||
            relative.has_root_name() || relative.has_root_directory())
            throw Exception(ZIP_ER_INVAL, 0, "unsafe absolute ZIP path: " + st.name);

        for (const auto& part : relative) {
            if (part == "..")
                throw Exception(ZIP_ER_INVAL, 0, "unsafe traversal ZIP path: " + st.name);
        }
    }

    const fs::path target = destination / relative;

    if (options.rejectUnsafePaths) {
        // weakly_canonical() resolves symlinks in every existing component,
        // so a pre-existing link pointing outside the destination is caught
        // here, before anything is created or written.
        const fs::path root = fs::weakly_canonical(destination);
        const fs::path resolved = fs::weakly_canonical(target);
        if (!detail::isInside(root, resolved))
            throw Exception(ZIP_ER_INVAL, 0, "ZIP path escapes destination: " + st.name);
    }

    if (st.directory) {
        fs::create_directories(target);
        return;
    }

    if (options.createDirectories && target.has_parent_path())
        fs::create_directories(target.parent_path());

    if (!options.overwrite && fs::exists(fs::symlink_status(target)))
        throw Exception(ZIP_ER_EXISTS, 0, "destination exists: " + target.string());

    // Open the member before touching the target, so a wrong password does
    // not truncate an existing file.
    File file = options.password ? openEncrypted(*options.password) : open();

    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out)
        throw Exception(ZIP_ER_OPEN, 0, "cannot open extraction target: " + target.string());

    try {
        std::vector<std::byte> buffer(128 * 1024);
        for (;;) {
            const auto n = file.read(buffer);
            if (!n) break;
            out.write(reinterpret_cast<const char*>(buffer.data()),
                      static_cast<std::streamsize>(n));
            if (!out)
                throw Exception(ZIP_ER_WRITE, 0, "write failed: " + target.string());
        }
        out.close();
        if (out.fail())
            throw Exception(ZIP_ER_WRITE, 0, "close failed: " + target.string());
        file.close();  // reports CRC / decryption errors
    } catch (...) {
        out.close();
        std::error_code ec;
        fs::remove(target, ec);  // do not leave a truncated file behind
        throw;
    }
}

} // namespace Zip

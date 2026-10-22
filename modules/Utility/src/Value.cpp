#include <TK4CPP/Utility/Value.hpp>

using namespace TK4CPP;
using namespace TK4CPP::Utility;
using namespace TK4CPP::Utility::detail;

    // ---- Costruttori ------------------------------------------------

    // ------------------------------------------------------------------
    // Costruttori "path su disco" — richiedono std::filesystem::path
    // esplicito per evitare ambiguita' con i costruttori da testo.
    // ------------------------------------------------------------------

    /// Solo path su disco (lazy: il file non viene letto subito)
    FileArtifact::FileArtifact(std::filesystem::path src,
                          std::string           mime)
        : source_path_(std::move(src))
        , mime_type_(std::move(mime))
    {}

    // ------------------------------------------------------------------
    // Costruttori "buffer in memoria" — accettano testo o byte raw.
    // const char* disambiguato da overload dedicato che prevale sulla
    // conversione implicita a path o string_view.
    // ------------------------------------------------------------------

    /// Buffer da const char* — risolve l'ambiguita' path vs string_view
    FileArtifact::FileArtifact(const char* text, std::string mime)
        : FileArtifact(std::string_view{text}, std::move(mime))
    {}

    /// Buffer da std::string
    FileArtifact::FileArtifact(std::string text, std::string mime)
        : FileArtifact(std::string_view{text}, std::move(mime))
    {}

    /// Buffer da stringa di testo (string_view — costruttore base)
    FileArtifact::FileArtifact(std::string_view text, std::string mime)
        : mime_type_(std::move(mime))
    {
        Buffer buf(text.size());
        std::memcpy(buf.data(), text.data(), text.size());
        buffer_ = std::make_shared<Buffer>(std::move(buf));
    }

    /// Buffer da vector<byte> grezzo
    FileArtifact::FileArtifact(Buffer data, std::string mime)
        : mime_type_(std::move(mime))
        , buffer_(std::make_shared<Buffer>(std::move(data)))
    {}

    /// Buffer da span<const byte> (copia i dati)
    FileArtifact::FileArtifact(std::span<const std::byte> data,
                 std::string               mime)
        : mime_type_(std::move(mime))
        , buffer_(std::make_shared<Buffer>(data.begin(), data.end()))
    {}

    /// Path sorgente + buffer gia' disponibile
    FileArtifact::FileArtifact(std::filesystem::path src, Buffer data,
                 std::string mime)
        : source_path_(std::move(src))
        , mime_type_(std::move(mime))
        , buffer_(std::make_shared<Buffer>(std::move(data)))
    {}

    // ---- Copy / Move ------------------------------------------------
    //
    // Il buffer è shared (copy-on-write semantics implicita tramite shared_ptr).
    // Una copia di FileArtifact condivide i byte fino al primo write.


    // ---- Query -------------------------------------------------------

     bool FileArtifact::has_source()  const  noexcept { return !source_path_.empty(); }
     bool FileArtifact::has_buffer()  const  noexcept { return buffer_ && !buffer_->empty(); }
     bool FileArtifact::is_text()     const  noexcept { return mime_type_.starts_with("text/"); }

     FileArtifact::Mode FileArtifact::mode() const  noexcept {
        if (has_source() && has_buffer()) return Mode::disk_and_mem;
        if (has_buffer())                 return Mode::memory_only;
        return Mode::disk_only;
    }

     const std::filesystem::path& FileArtifact::source_path() const  noexcept { return source_path_; }
     const std::string&           FileArtifact::mime_type()   const  noexcept { return mime_type_;   }

    /// Dimensione in byte: se c'è buffer usa quello, altrimenti legge dal disco
     std::size_t FileArtifact::size_bytes() const {
        if (has_buffer())  return buffer_->size();
        if (has_source())  return std::filesystem::file_size(source_path_);
        return 0;
    }

    // ---- Accesso al buffer ------------------------------------------

    /// Ritorna riferimento al buffer (lancia FileError se non presente)
     const FileArtifact::Buffer& FileArtifact::buffer() const {
        if (!has_buffer())
            throw FileError("FileArtifact::buffer() — nessun dato in memoria; chiama load() prima");
        return *buffer_;
    }

    /// Vista span<const byte> sul buffer
     std::span<const std::byte> FileArtifact::bytes() const {
        return buffer();
    }

    /// Interpreta il buffer come stringa di testo (UTF-8 assunto)
     std::string FileArtifact::text() const {
        const auto& b = buffer();
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    }

    // ---- Operazioni I/O ---------------------------------------------

    /**
     * @brief Carica source_path nel buffer interno.
     * @param force  Se true ricarica anche se il buffer è già presente.
     * @throws FileError se source_path è vuoto o il file non è leggibile.
     */
    void FileArtifact::load(bool force) {
        if (!has_source())
            throw FileError("FileArtifact::load() — nessun source_path impostato");
        if (has_buffer() && !force)
            return; // già caricato

        std::ifstream ifs(source_path_, std::ios::binary | std::ios::ate);
        if (!ifs)
            throw FileError(std::format("FileArtifact::load() — impossibile aprire '{}'",
                                        source_path_.string()));
        const auto sz = static_cast<std::size_t>(ifs.tellg());
        ifs.seekg(0);

        auto buf = std::make_shared<Buffer>(sz);
        if (!ifs.read(reinterpret_cast<char*>(buf->data()),
                      static_cast<std::streamsize>(sz)))
            throw FileError(std::format("FileArtifact::load() — errore lettura '{}'",
                                        source_path_.string()));
        buffer_ = std::move(buf);
    }

    /**
     * @brief Rilascia il buffer dalla memoria (mantiene il source_path).
     */
    void FileArtifact::unload()  noexcept {
        buffer_.reset();
    }

    /**
     * @brief Salva il buffer su dest (o su source_path se dest omesso).
     * @throws FileError se non c'è buffer o non c'è destinazione.
     */
    void FileArtifact::save(std::optional<std::filesystem::path> dest) const {
        if (!has_buffer())
            throw FileError("FileArtifact::save() — nessun dato in memoria da salvare");

        const auto& target = dest.has_value() ? *dest : source_path_;
        if (target.empty())
            throw FileError("FileArtifact::save() — nessuna destinazione specificata");

        // Crea le directory intermedie se necessario
        if (target.has_parent_path())
            std::filesystem::create_directories(target.parent_path());

        std::ofstream ofs(target, std::ios::binary | std::ios::trunc);
        if (!ofs)
            throw FileError(std::format("FileArtifact::save() — impossibile scrivere su '{}'",
                                        target.string()));
        if (!ofs.write(reinterpret_cast<const char*>(buffer_->data()),
                       static_cast<std::streamsize>(buffer_->size())))
            throw FileError(std::format("FileArtifact::save() — errore scrittura su '{}'",
                                        target.string()));
    }

    /**
     * @brief Copia il file da source_path a dest senza passare per la RAM.
     *        Usa std::filesystem::copy_file (efficiente, no buffering utente).
     * @param options  Opzioni di sovrascrittura (default: overwrite_existing).
     * @throws FileError se source_path è vuoto o la copia fallisce.
     */
    void FileArtifact::copy_to(const std::filesystem::path& dest,
                 std::filesystem::copy_options options) const
    {
        if (!has_source())
            throw FileError("FileArtifact::copy_to() — nessun source_path impostato");

        if (dest.has_parent_path())
            std::filesystem::create_directories(dest.parent_path());

        std::error_code ec;
        std::filesystem::copy_file(source_path_, dest, options, ec);
        if (ec)
            throw FileError(std::format("FileArtifact::copy_to() — copia '{}' → '{}' fallita: {}",
                                        source_path_.string(), dest.string(), ec.message()));
    }

    /**
     * @brief Operazione unificata: usa copy_to() se il buffer è vuoto,
     *        altrimenti usa save().
     *        Copre tutti i casi:
     *          memoria → dest      (save)
     *          disco   → dest      (copy_to, zero-copy)
     *          disco+mem → dest    (save, usa il buffer)
     */
    void FileArtifact::save_or_copy(const std::filesystem::path& dest) const {
        if (has_buffer())
            save(dest);
        else
            copy_to(dest);
    }

    /**
     * @brief Carica source_path e restituisce il testo come std::string.
     *        Comodo per file di testo senza dover chiamare load() esplicitamente.
     */
     std::string FileArtifact::load_text() {
        load();
        return text();
    }

    /**
     * @brief Sostituisce il buffer con una nuova stringa di testo.
     */
    void FileArtifact::set_text(std::string_view t) {
        Buffer buf(t.size());
        std::memcpy(buf.data(), t.data(), t.size());
        buffer_ = std::make_shared<Buffer>(std::move(buf));
    }

    /**
     * @brief Sostituisce il buffer con nuovi byte binari.
     */
    void FileArtifact::set_bytes(std::span<const std::byte> data) {
        buffer_ = std::make_shared<Buffer>(data.begin(), data.end());
    }

    // ---- Confronto --------------------------------------------------
    //
    // Identità basata su (source_path, mime_type, size).
    // Due FileArtifact con lo stesso path ma buffer diversi sono "uguali"
    // come identità di risorsa — scelta deliberata per uso in variant/map.

     bool FileArtifact::operator==(const FileArtifact& rhs) const noexcept {
        return source_path_.string() == rhs.source_path_.string()
            && mime_type_            == rhs.mime_type_;
    }

     std::strong_ordering FileArtifact::operator<=>(const FileArtifact& rhs) const  noexcept {
        if (auto c = source_path_.string() <=> rhs.source_path_.string(); c != 0) return c;
        return mime_type_ <=> rhs.mime_type_;
    }

    // ---- Descrizione ------------------------------------------------

     std::string FileArtifact::to_string() const {
        const auto sz = has_buffer() ? buffer_->size()
                      : has_source() ? std::filesystem::exists(source_path_)
                                           ? std::filesystem::file_size(source_path_) : 0
                      : 0;

        return std::format("FileArtifact{{path='{}', mime='{}', size={}, buffered={}}}",
                           source_path_.string(), mime_type_, sz, has_buffer());
    }

    std::ostream& TK4CPP::Utility::operator<<(std::ostream& os, const FileArtifact& fa) {
        return os << fa.to_string();
    }


    /// Dalla variant grezza
    Value::Value(VariableValue v)  noexcept
        : data_(std::move(v)) {}

    /// Da literal C-string → std::string
    Value::Value(const char* s)
        : data_(std::string(s)) {}


    Value& Value::operator=(const char* s) { data_ = std::string(s); return *this; }

     std::size_t Value::type_index() const  noexcept { return data_.index(); }

     std::string Value::type_name() const  noexcept {
        return std::visit(detail::overloaded{
            [](int)                -> std::string { return "int"; },
            [](double)             -> std::string { return "double"; },
            [](const std::string&) -> std::string { return "string"; },
            [](const FileArtifact&)-> std::string { return "FileArtifact"; },
        }, data_);
    }


     std::string Value::to_string() const {
        return std::visit(detail::overloaded{
            [](int v)                -> std::string { return std::to_string(v); },
            [](double v)             -> std::string { return std::format("{}", v); },
            [](const std::string& v) -> std::string { return v; },
            [](const FileArtifact& v)-> std::string { return v.to_string(); },
        }, data_);
    }

     double Value::to_double() const {
        return std::visit(detail::overloaded{
            [](int v)    -> double { return static_cast<double>(v); },
            [](double v) -> double { return v; },
            [](auto&&)   -> double { throw ConversionError("Value::to_double() — tipo non numerico"); },
        }, data_);
    }

     int Value::to_int() const {
        return std::visit(detail::overloaded{
            [](int v)    -> int { return v; },
            [](double v) -> int { return static_cast<int>(v); },
            [](auto&&)   -> int { throw ConversionError("Value::to_int() — tipo non numerico"); },
        }, data_);
    }

     bool Value::is_falsy() const noexcept {
        return std::visit(detail::overloaded{
            [](int v)                -> bool { return v == 0; },
            [](double v)             -> bool { return v == 0.0; },
            [](const std::string& v) -> bool { return v.empty(); },
            [](const FileArtifact&)  -> bool { return false; },
        }, data_);
    }

     bool Value::is_truthy()  const  noexcept { return !is_falsy(); }
     bool Value::is_numeric() const  noexcept { return holds<int>() || holds<double>(); }

    // ---- Confronto --------------------------------------------------

     bool Value::operator==(const Value& rhs) const noexcept {
        return data_ == rhs.data_;
    }

     std::partial_ordering Value::operator<=>(const Value& rhs) const noexcept {
        if (data_.index() != rhs.data_.index())
            return std::partial_ordering::unordered;

        return std::visit(detail::overloaded{
            [&](int lv) -> std::partial_ordering {
                return lv <=> std::get<int>(rhs.data_);
            },
            [&](double lv) -> std::partial_ordering {
                return lv <=> std::get<double>(rhs.data_);
            },
            [&](const std::string& lv) -> std::partial_ordering {
                return lv <=> std::get<std::string>(rhs.data_);
            },
            [&](const FileArtifact& lv) -> std::partial_ordering {
                const auto cmp = lv <=> std::get<FileArtifact>(rhs.data_);
                if (cmp == std::strong_ordering::less)    return std::partial_ordering::less;
                if (cmp == std::strong_ordering::greater) return std::partial_ordering::greater;
                return std::partial_ordering::equivalent;
            },
        }, data_);
    }

    // ---- Accesso raw ------------------------------------------------

     const VariableValue& Value::raw()       const noexcept { return data_; }
           VariableValue& Value::raw()             noexcept { return data_; }

    std::ostream& TK4CPP::Utility::operator<<(std::ostream& os, const Value& v) {
        return os << v.to_string();
    }



// ================================================================
//  ValueFactory
// ================================================================

   std::shared_ptr<Value> ValueFactory::make_shared(const char* s) {
        return std::make_shared<Value>(std::string(s));
    }
   
   std::shared_ptr<Value> ValueFactory::make_shared(VariableValue v) {
        return std::make_shared<Value>(std::move(v));
    }

    std::unique_ptr<Value> ValueFactory::make_unique(const char* s) {
        return std::make_unique<Value>(std::string(s));
    }

    // clone
     Value ValueFactory::clone(const Value& src) { return src; }
     std::shared_ptr<Value> ValueFactory::clone_shared(const std::shared_ptr<Value>& src) {
        return src ? std::make_shared<Value>(*src) : nullptr;
    }


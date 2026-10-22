#pragma once

/**
 * @file Value.hpp
 * @brief Classe generica per la gestione di valori tipizzati a runtime.
 *
 * Tutto il codice utente vive nel namespace TK4CPP::Utility.
 * La specializzazione std::hash<TK4CPP::Utility::Value> è l'unica cosa in namespace std.
 *
 * Layout:
 *   TK4CPP::Utility::FileArtifact   — gestione file (disco ↔ memoria, copia, salvataggio)
 *   TK4CPP::Utility::VariableValue  — alias variant<int, double, string, FileArtifact>
 *   TK4CPP::Utility::Value          — wrapper ricco attorno alla variant
 *   TK4CPP::Utility::ValueFactory   — factory con tre policy di ownership
 */

#include <variant>
#include "TK4CPP/Utility/Export.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <stdexcept>
#include <optional>
#include <type_traits>
#include <concepts>
#include <compare>
#include <format>
#include <ostream>
#include <fstream>
#include <filesystem>
#include <span>
#include <cstddef>
#include <cstring>


namespace TK4CPP {
	namespace Utility {

		// ================================================================
		//  Helpers interni (detail)
		// ================================================================

		namespace detail {

			/// Visitor multi-lambda — https://en.cppreference.com/w/cpp/utility/variant/visit
			template<typename... Fs>
			struct overloaded : Fs... { using Fs::operator()...; };

			/// Membership compile-time nella variant
			template<typename T, typename Variant>
			struct is_in_variant : std::false_type {};

			template<typename T, typename... Types>
			struct is_in_variant<T, std::variant<Types...>>
				: std::bool_constant<(std::same_as<std::remove_cvref_t<T>, Types> || ...)>
			{
			};

			template<typename T, typename Variant>
			inline constexpr bool is_in_variant_v = is_in_variant<T, Variant>::value;

		} // namespace detail


		// ================================================================
		//  Eccezioni di dominio
		// ================================================================

		struct TK4CPP_UTILITY_EXPORT TypeError : std::runtime_error {
			using std::runtime_error::runtime_error;
		};

		struct TK4CPP_UTILITY_EXPORT ConversionError : std::runtime_error {
			using std::runtime_error::runtime_error;
		};

		struct TK4CPP_UTILITY_EXPORT FileError : std::runtime_error {
			using std::runtime_error::runtime_error;
		};


		// ================================================================
		//  FileArtifact
		//
		//  Rappresenta un file che può essere:
		//    • solo su disco   (source_path impostato, buffer vuoto)
		//    • solo in memoria (buffer popolato, source_path vuoto)
		//    • entrambi        (buffer popolato + source_path come origine)
		//
		//  Operazioni principali:
		//    load()          — legge source_path nel buffer interno
		//    save(dest)      — scrive il buffer su dest (o su source_path se dest omesso)
		//    copy_to(dest)   — copia il file da source_path a dest (senza passare per la RAM)
		//    save_or_copy(d) — usa copy_to se il buffer è vuoto, altrimenti save
		// ================================================================

		class FileArtifact;
		/// Stream insertion for FileArtifact (declared before the class so the export attribute applies).
		TK4CPP_UTILITY_EXPORT std::ostream& operator<<(std::ostream& os, const FileArtifact& fa);

		class TK4CPP_UTILITY_EXPORT FileArtifact {
		public:
			using Buffer = std::vector<std::byte>;

			// ---- Modalità di costruzione ------------------------------------

			enum class Mode {
				disk_only,    ///< solo path, niente buffer
				memory_only,  ///< solo buffer, niente path sorgente
				disk_and_mem, ///< path sorgente + buffer (già caricato o da caricare)
			};

			// ---- Costruttori ------------------------------------------------

			FileArtifact() = default;

			// ------------------------------------------------------------------
			// Costruttori "path su disco" — richiedono std::filesystem::path
			// esplicito per evitare ambiguita' con i costruttori da testo.
			// ------------------------------------------------------------------

			/// Solo path su disco (lazy: il file non viene letto subito)
			explicit FileArtifact(std::filesystem::path src,
				std::string           mime = "application/octet-stream");

			// ------------------------------------------------------------------
			// Costruttori "buffer in memoria" — accettano testo o byte raw.
			// const char* disambiguato da overload dedicato che prevale sulla
			// conversione implicita a path o string_view.
			// ------------------------------------------------------------------

			/// Buffer da const char* — risolve l'ambiguita' path vs string_view
			FileArtifact(const char* text, std::string mime = "text/plain");

			/// Buffer da std::string
			FileArtifact(std::string text, std::string mime = "text/plain");

			/// Buffer da stringa di testo (string_view — costruttore base)
			FileArtifact(std::string_view text, std::string mime = "text/plain");

			/// Buffer da vector<byte> grezzo
			FileArtifact(Buffer data, std::string mime = "application/octet-stream");

			/// Buffer da span<const byte> (copia i dati)
			FileArtifact(std::span<const std::byte> data,
				std::string               mime = "application/octet-stream");

			/// Path sorgente + buffer gia' disponibile
			FileArtifact(std::filesystem::path src, Buffer data,
				std::string mime = "application/octet-stream");

			// ---- Copy / Move ------------------------------------------------
			//
			// Il buffer è shared (copy-on-write semantics implicita tramite shared_ptr).
			// Una copia di FileArtifact condivide i byte fino al primo write.

			FileArtifact(const FileArtifact&) = default;
			FileArtifact(FileArtifact&&) = default;
			FileArtifact& operator=(const FileArtifact&) = default;
			FileArtifact& operator=(FileArtifact&&) = default;
			~FileArtifact() = default;

			// ---- Query -------------------------------------------------------

			[[nodiscard]] bool has_source()  const noexcept;
			[[nodiscard]] bool has_buffer()  const noexcept;
			[[nodiscard]] bool is_text()     const noexcept;

			[[nodiscard]] Mode mode() const noexcept;

			[[nodiscard]] const std::filesystem::path& source_path() const noexcept;
			[[nodiscard]] const std::string& mime_type()   const noexcept;

			/// Dimensione in byte: se c'è buffer usa quello, altrimenti legge dal disco
			[[nodiscard]] std::size_t size_bytes() const;

			// ---- Accesso al buffer ------------------------------------------

			/// Ritorna riferimento al buffer (lancia FileError se non presente)
			[[nodiscard]] const Buffer& buffer() const;

			/// Vista span<const byte> sul buffer
			[[nodiscard]] std::span<const std::byte> bytes() const;

			/// Interpreta il buffer come stringa di testo (UTF-8 assunto)
			[[nodiscard]] std::string text() const;

			// ---- Operazioni I/O ---------------------------------------------

			/**
			 * @brief Carica source_path nel buffer interno.
			 * @param force  Se true ricarica anche se il buffer è già presente.
			 * @throws FileError se source_path è vuoto o il file non è leggibile.
			 */
			void load(bool force = false);

			/**
			 * @brief Rilascia il buffer dalla memoria (mantiene il source_path).
			 */
			void unload() noexcept;

			/**
			 * @brief Salva il buffer su dest (o su source_path se dest omesso).
			 * @throws FileError se non c'è buffer o non c'è destinazione.
			 */
			void save(std::optional<std::filesystem::path> dest = std::nullopt) const;

			/**
			 * @brief Copia il file da source_path a dest senza passare per la RAM.
			 *        Usa std::filesystem::copy_file (efficiente, no buffering utente).
			 * @param options  Opzioni di sovrascrittura (default: overwrite_existing).
			 * @throws FileError se source_path è vuoto o la copia fallisce.
			 */
			void copy_to(const std::filesystem::path& dest,
				std::filesystem::copy_options options =
				std::filesystem::copy_options::overwrite_existing) const;

			/**
			 * @brief Operazione unificata: usa copy_to() se il buffer è vuoto,
			 *        altrimenti usa save().
			 *        Copre tutti i casi:
			 *          memoria → dest      (save)
			 *          disco   → dest      (copy_to, zero-copy)
			 *          disco+mem → dest    (save, usa il buffer)
			 */
			void save_or_copy(const std::filesystem::path& dest) const;

			/**
			 * @brief Carica source_path e restituisce il testo come std::string.
			 *        Comodo per file di testo senza dover chiamare load() esplicitamente.
			 */
			[[nodiscard]] std::string load_text();

			/**
			 * @brief Sostituisce il buffer con una nuova stringa di testo.
			 */
			void set_text(std::string_view t);

			/**
			 * @brief Sostituisce il buffer con nuovi byte binari.
			 */
			void set_bytes(std::span<const std::byte> data);

			// ---- Confronto --------------------------------------------------
			//
			// Identità basata su (source_path, mime_type, size).
			// Due FileArtifact con lo stesso path ma buffer diversi sono "uguali"
			// come identità di risorsa — scelta deliberata per uso in variant/map.

			[[nodiscard]] bool operator==(const FileArtifact& rhs) const noexcept;

			[[nodiscard]] std::strong_ordering operator<=>(const FileArtifact& rhs) const noexcept;

			// ---- Descrizione ------------------------------------------------

			[[nodiscard]] std::string to_string() const;

			friend std::ostream& operator<<(std::ostream& os, const FileArtifact& fa);

		private:
			std::filesystem::path         source_path_;
			std::string                   mime_type_{ "application/octet-stream" };
			std::shared_ptr<Buffer>       buffer_;       ///< nullptr = nessun dato in RAM
		};


		// ================================================================
		//  Concepts
		// ================================================================

		// Forward-declare VariableValue prima dei concepts che la usano
		using VariableValue = std::variant<int, double, std::string, FileArtifact>;

		template<typename T>
		concept VariantMember = detail::is_in_variant_v<std::remove_cvref_t<T>, VariableValue>;

		template<typename T>
		concept Numeric = std::integral<T> || std::floating_point<T>;


		// ================================================================
		//  Value — wrapper ricco attorno a VariableValue
		// ================================================================

		class Value;
		/// Stream insertion for Value (declared before the class so the export attribute applies).
		TK4CPP_UTILITY_EXPORT std::ostream& operator<<(std::ostream& os, const Value& v);

		class TK4CPP_UTILITY_EXPORT Value {
		public:
			using variant_type = VariableValue;

			// ---- Costruttori ------------------------------------------------

			Value() = delete;   // usa std::optional<Value> per il caso "vuoto"

			/// Da qualsiasi membro della variant (perfect-forward)
			template<VariantMember T>
			explicit(false) Value(T&& v)    // NOLINT(google-explicit-constructor)
				: data_(std::forward<T>(v)) {
			}

			/// Dalla variant grezza
			explicit Value(VariableValue v) noexcept;

			/// Da literal C-string → std::string
			explicit(false) Value(const char* s);

			/// Da Numeric non esatto nella variant (long→int, float→double)
			template<Numeric N>
				requires (!VariantMember<N>)
			explicit Value(N n) {
				if constexpr (std::integral<N>)
					data_ = static_cast<int>(n);
				else
					data_ = static_cast<double>(n);
			}

			// ---- Copy / Move ------------------------------------------------

			Value(const Value&) = default;
			Value(Value&&) = default;
			Value& operator=(const Value&) = default;
			Value& operator=(Value&&) = default;
			~Value() = default;

			// ---- Assignment -------------------------------------------------

			template<VariantMember T>
			Value& operator=(T&& v) { data_ = std::forward<T>(v); return *this; }

			Value& operator=(const char* s);

			// ---- Accesso tipizzato ------------------------------------------

			template<VariantMember T>
			[[nodiscard]] T& as() {
				if (auto* p = std::get_if<T>(&data_)) return *p;
				throw TypeError(std::format("Value::as<{}>() — tipo interno è '{}'",
					type_name_static<T>(), type_name()));
			}

			template<VariantMember T>
			[[nodiscard]] const T& as() const {
				if (auto* p = std::get_if<T>(&data_)) return *p;
				throw TypeError(std::format("Value::as<{}>() const — tipo interno è '{}'",
					type_name_static<T>(), type_name()));
			}

			template<VariantMember T>
			[[nodiscard]] std::optional<T> get() const noexcept {
				if (auto* p = std::get_if<T>(&data_)) return *p;
				return std::nullopt;
			}

			template<VariantMember T>
			[[nodiscard]] T* get_if()       noexcept { return std::get_if<T>(&data_); }

			template<VariantMember T>
			[[nodiscard]] const T* get_if() const noexcept { return std::get_if<T>(&data_); }

			// ---- Query di tipo ----------------------------------------------

			template<VariantMember T>
			[[nodiscard]] bool holds() const noexcept {
				return std::holds_alternative<T>(data_);
			}

			[[nodiscard]] std::size_t type_index() const noexcept;

			[[nodiscard]] std::string type_name() const noexcept;

			// ---- Visitor ----------------------------------------------------

			template<typename Visitor>
			decltype(auto) visit(Visitor&& vis) { return std::visit(std::forward<Visitor>(vis), data_); }

			template<typename Visitor>
			decltype(auto) visit(Visitor&& vis) const { return std::visit(std::forward<Visitor>(vis), data_); }

			template<typename Visitor>
			[[nodiscard]] Value transform(Visitor&& vis) const {
				return Value{ std::visit(std::forward<Visitor>(vis), data_) };
			}

			// ---- Conversioni ------------------------------------------------

			[[nodiscard]] std::string to_string() const;

			[[nodiscard]] double to_double() const;

			[[nodiscard]] int to_int() const;

			[[nodiscard]] bool is_falsy() const noexcept;

			[[nodiscard]] bool is_truthy()  const noexcept ;
			[[nodiscard]] bool is_numeric() const noexcept ;

			// ---- Confronto --------------------------------------------------

			[[nodiscard]] bool operator==(const Value& rhs) const noexcept;

			[[nodiscard]] std::partial_ordering operator<=>(const Value& rhs) const noexcept;

			// ---- Accesso raw ------------------------------------------------

			[[nodiscard]] const VariableValue& raw()       const noexcept;
			[[nodiscard]] VariableValue& raw()             noexcept;

			friend std::ostream& operator<<(std::ostream& os, const Value& v);

		private:
			VariableValue data_;

			template<typename T>
			static constexpr std::string_view type_name_static() noexcept {
				if constexpr (std::same_as<T, int>)           return "int";
				else if constexpr (std::same_as<T, double>)   return "double";
				else if constexpr (std::same_as<T, std::string>)    return "string";
				else if constexpr (std::same_as<T, FileArtifact>)   return "FileArtifact";
				else return "unknown";
			}
		};


		// ================================================================
		//  ValueFactory
		// ================================================================

		struct ValueFactory {
			// shared_ptr
			template<VariantMember T>
			[[nodiscard]] static std::shared_ptr<Value> make_shared(T&& v) {
				return std::make_shared<Value>(std::forward<T>(v));
			}
			[[nodiscard]] static std::shared_ptr<Value> make_shared(const char* s);
			[[nodiscard]] static std::shared_ptr<Value> make_shared(VariableValue v);

			// unique_ptr
			template<VariantMember T>
			[[nodiscard]] static std::unique_ptr<Value> make_unique(T&& v) {
				return std::make_unique<Value>(std::forward<T>(v));
			}
			[[nodiscard]] static std::unique_ptr<Value> make_unique(const char* s);

			// by value
			template<VariantMember T>
			[[nodiscard]] static Value make(T&& v) { return Value{ std::forward<T>(v) }; }
			[[nodiscard]] static Value make(const char* s);

			// clone
			[[nodiscard]] static Value clone(const Value& src);
			[[nodiscard]] static std::shared_ptr<Value> clone_shared(const std::shared_ptr<Value>& src);
		};

	} // namespace TK4CPP::Utility
}

// ================================================================
//  std::hash specialization (the only piece outside TK4CPP::Utility)
// ================================================================

template<>
struct std::hash<TK4CPP::Utility::Value> {
	std::size_t operator()(const TK4CPP::Utility::Value& v) const noexcept {
		using FA = TK4CPP::Utility::FileArtifact;
		return std::visit(TK4CPP::Utility::detail::overloaded{
			[](int i)                -> std::size_t { return std::hash<int>{}(i); },
			[](double d)             -> std::size_t { return std::hash<double>{}(d); },
			[](const std::string& s) -> std::size_t { return std::hash<std::string>{}(s); },
			[](const FA& f)          -> std::size_t {
				const std::size_t h1 = std::hash<std::string>{}(f.source_path().string());
				const std::size_t h2 = std::hash<std::string>{}(f.mime_type());
				return h1 ^ (h2 << 1);
			},
			}, v.raw());
	}

};

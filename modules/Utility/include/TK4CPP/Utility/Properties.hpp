#pragma once

/**
 * @file Properties.hpp
 * @brief Single-property and property-bag abstractions for the TK4CPP framework.
 *
 * Depends on Value.hpp (TK4CPP::Utility::Value, TK4CPP::Utility::ValueFactory,
 * TK4CPP::Utility::VariantMember, TK4CPP::Utility::VariableValue).
 *
 * Layout (all inside namespace TK4CPP::Utility):
 *   Serializable          — abstract mixin: any object that accepts a Serializer
 *   Serializer            — abstract visitor base for serialization strategies
 *   Property              — immutable key/value pair (key: string, value: Value)
 *   PropertyError         — domain exception thrown by Properties operations
 *   Properties            — ordered, keyed bag of Property objects
 */

#include "TK4CPP/Utility/Value.hpp"

#include <list>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iterator>
#include <ranges>


namespace TK4CPP::Utility {

// ================================================================
//  Forward declarations
// ================================================================

class Property;
class Properties;
class Serializer;


// ================================================================
//  Serializable — abstract mixin
//
//  Any class that can be visited by a Serializer inherits from this.
//  The accept() method implements the double-dispatch (Visitor pattern).
// ================================================================

class Serializable {
public:
    virtual ~Serializable() = default;

    /// Accept a serializer visitor. Implementations call visitor->visit(this).
    virtual void accept(Serializer* visitor) = 0;
};


// ================================================================
//  Serializer — abstract visitor base
//
//  Concrete serializers (JSON, XML, binary, …) derive from this and
//  override the visit() overloads they care about.
// ================================================================

class Serializer {
public:
    virtual ~Serializer() = default;

    virtual void visit(Property*   property)   = 0;
    virtual void visit(Properties* properties) = 0;
};


// ================================================================
//  PropertyError — domain exception
// ================================================================

struct PropertyError : std::runtime_error {
    using std::runtime_error::runtime_error;
};


// ================================================================
//  Property — immutable key / value pair
//
//  A Property is always created through one of the static create()
//  factory overloads; the constructor is private to enforce this.
//  The value is stored as a shared_ptr<Value> so that Properties
//  can share values without copying when get() is called.
// ================================================================

class Property final : public Serializable {
public:
    // ---- Factory ----------------------------------------------------

    /// Create from an existing shared_ptr<Value>.
    [[nodiscard]]
    static std::shared_ptr<Property> create(std::string            key,
                                            std::shared_ptr<Value> value)
    {
        if (key.empty())
            throw PropertyError("Property::create() — key must not be empty");
        if (!value)
            throw PropertyError("Property::create() — value must not be null");
        return std::shared_ptr<Property>(new Property(std::move(key), std::move(value)));
    }

    /// Create from any type accepted by Value's constructors
    /// (int, double, std::string, FileArtifact, const char*, …).
    template<typename T>
        requires std::constructible_from<Value, T>
    [[nodiscard]]
    static std::shared_ptr<Property> create(std::string key, T&& val)
    {
        return create(std::move(key),
                      std::make_shared<Value>(std::forward<T>(val)));
    }

    // ---- Accessors --------------------------------------------------

    /// The property key (never empty).
    [[nodiscard]] const std::string& key() const noexcept { return key_; }

    /// The property value (never null).
    [[nodiscard]] std::shared_ptr<Value>       value()       noexcept { return value_; }
    [[nodiscard]] std::shared_ptr<const Value> value() const noexcept { return value_; }

    /// Convenience: typed access to the inner variant value.
    /// Returns std::nullopt if the held type does not match T.
    template<VariantMember T>
    [[nodiscard]] std::optional<T> value_as() const noexcept {
        return value_->get<T>();
    }

    /// Convenience: typed access — throws TypeError if type does not match.
    template<VariantMember T>
    [[nodiscard]] const T& value_ref() const {
        return value_->as<T>();
    }

    // ---- Serializable -----------------------------------------------

    void accept(Serializer* visitor) override {
        visitor->visit(this);
    }

    // ---- Representation ---------------------------------------------

    [[nodiscard]] std::string to_string() const {
        return std::format("Property{{key='{}', value={}}}", key_, value_->to_string());
    }

    friend std::ostream& operator<<(std::ostream& os, const Property& p) {
        return os << p.to_string();
    }

private:
    // Private constructor — use create()
    Property(std::string key, std::shared_ptr<Value> value)
        : key_(std::move(key))
        , value_(std::move(value))
    {}

    std::string            key_;
    std::shared_ptr<Value> value_;
};


// ================================================================
//  Properties — ordered, keyed bag of Property objects
//
//  Internally stores properties in both:
//    • an unordered_map  for O(1) keyed lookup
//    • an ordered list   to preserve insertion order (used by get_all,
//      iterators, and the Serializer visitor)
//
//  All mutating operations keep the two structures in sync.
// ================================================================

class Properties final : public Serializable {
public:
    // ---- Types ------------------------------------------------------

    using PropertyPtr  = std::shared_ptr<Property>;
    using PropertyList = std::list<PropertyPtr>;
    using iterator       = PropertyList::iterator;
    using const_iterator = PropertyList::const_iterator;

    // ---- Constructors / assignment ----------------------------------

    Properties() = default;

    /// Construct from an initializer list of {key, value} pairs.
    Properties(std::initializer_list<std::pair<std::string, Value>> init) {
        for (auto& [k, v] : init)
            set(k, v);
    }

    Properties(const Properties&)            = default;
    Properties(Properties&&)                 = default;
    Properties& operator=(const Properties&) = default;
    Properties& operator=(Properties&&)      = default;
    ~Properties()                            = default;

    // ---- Mutators ---------------------------------------------------

    /**
     * @brief Insert or replace a property by key.
     *
     * If a property with the same key already exists it is replaced
     * (value updated in-place so that existing shared_ptr<Property>
     * held by callers remain valid — only its internal value changes).
     *
     * @param key   Must not be empty.
     * @param value Pre-built shared_ptr<Value>.
     * @throws PropertyError if key is empty or value is null.
     */
    void set(const std::string& key, std::shared_ptr<Value> value) {
        if (key.empty())
            throw PropertyError("Properties::set() — key must not be empty");
        if (!value)
            throw PropertyError("Properties::set() — value must not be null");

        auto it = index_.find(key);
        if (it != index_.end()) {
            // Key exists: replace the shared_ptr stored in the list node.
            // The list iterator (it->second) remains valid; only the
            // pointed-to shared_ptr is overwritten.
            *(it->second) = Property::create(key, std::move(value));
        } else {
            auto prop = Property::create(key, std::move(value));
            order_.push_back(prop);
            index_.emplace(key, std::prev(order_.end()));
        }
    }

    /// Insert or replace — builds Value from any compatible type.
    template<typename T>
        requires std::constructible_from<Value, T>
    void set(const std::string& key, T&& val) {
        set(key, std::make_shared<Value>(std::forward<T>(val)));
    }

    /// Insert or replace from an existing Value (copied).
    void set(const std::string& key, const Value& val) {
        set(key, std::make_shared<Value>(val));
    }

    /**
     * @brief Remove a property by key.
     * @return true if the key existed and was removed, false otherwise.
     */
    bool remove(const std::string& key) noexcept {
        auto it = index_.find(key);
        if (it == index_.end()) return false;
        order_.erase(it->second);
        index_.erase(it);
        return true;
    }

    /// Remove all properties.
    void clear() noexcept {
        order_.clear();
        index_.clear();
    }

    // ---- Queries ----------------------------------------------------

    [[nodiscard]] bool        empty()    const noexcept { return order_.empty(); }
    [[nodiscard]] std::size_t size()     const noexcept { return order_.size();  }
    [[nodiscard]] bool        contains(const std::string& key) const noexcept {
        return index_.contains(key);
    }

    // ---- Accessors --------------------------------------------------

    /**
     * @brief Return the Value associated with key, or std::nullopt.
     *
     * Returns a shared_ptr so callers can cache the value without
     * preventing the Property from being garbage-collected together with
     * the Properties bag.
     */
    [[nodiscard]]
    std::optional<std::shared_ptr<Value>> get(const std::string& key) const noexcept {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        return (*it->second)->value();
    }

    /**
     * @brief Return the typed inner value for key, or std::nullopt.
     *
     * Equivalent to get(key)->get<T>() but in a single call and
     * without throwing on missing key or wrong type.
     */
    template<VariantMember T>
    [[nodiscard]] std::optional<T> get_as(const std::string& key) const noexcept {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        return (*it->second)->value()->get<T>();
    }

    /**
     * @brief Return the typed inner value, throwing on missing key or
     *        wrong type.
     * @throws PropertyError if key does not exist.
     * @throws TypeError     if the held type does not match T.
     */
    template<VariantMember T>
    [[nodiscard]] const T& require_as(const std::string& key) const {
        auto it = index_.find(key);
        if (it == index_.end())
            throw PropertyError(std::format(
                "Properties::require_as() — key '{}' not found", key));
        return (*it->second)->value()->as<T>();
    }

    /**
     * @brief Return the Property object for key, or nullptr.
     */
    [[nodiscard]]
    std::shared_ptr<Property> get_property(const std::string& key) const noexcept {
        auto it = index_.find(key);
        return (it != index_.end()) ? *it->second : nullptr;
    }

    /**
     * @brief Return all properties in insertion order.
     */
    [[nodiscard]] PropertyList get_all() const {
        return order_;          // cheap: list copy shares the shared_ptrs
    }

    /**
     * @brief Return all keys in insertion order.
     */
    [[nodiscard]] std::vector<std::string> keys() const {
        std::vector<std::string> result;
        result.reserve(order_.size());
        for (const auto& prop : order_)
            result.push_back(prop->key());
        return result;
    }

    // ---- Merge / update ---------------------------------------------

    /**
     * @brief Merge another Properties bag into this one.
     *
     * @param other   Source bag.
     * @param overwrite  If true (default), existing keys are replaced.
     *                   If false, existing keys are kept unchanged.
     */
    void merge(const Properties& other, bool overwrite = true) {
        for (const auto& prop : other.order_) {
            if (overwrite || !contains(prop->key()))
                set(prop->key(), prop->value());
        }
    }

    // ---- Range / iterator support -----------------------------------

    [[nodiscard]] iterator       begin()        noexcept { return order_.begin();  }
    [[nodiscard]] iterator       end()          noexcept { return order_.end();    }
    [[nodiscard]] const_iterator begin()  const noexcept { return order_.begin();  }
    [[nodiscard]] const_iterator end()    const noexcept { return order_.end();    }
    [[nodiscard]] const_iterator cbegin() const noexcept { return order_.cbegin(); }
    [[nodiscard]] const_iterator cend()   const noexcept { return order_.cend();   }

    // ---- Serializable -----------------------------------------------

    void accept(Serializer* visitor) override {
        visitor->visit(this);
    }

    // ---- Representation ---------------------------------------------

    [[nodiscard]] std::string to_string() const {
        std::string result = "Properties{\n";
        for (const auto& prop : order_)
            result += std::format("  {}\n", prop->to_string());
        result += '}';
        return result;
    }

    friend std::ostream& operator<<(std::ostream& os, const Properties& p) {
        return os << p.to_string();
    }

private:
    // Insertion-ordered list of properties (source of truth for order).
    PropertyList order_;

    // Index: key → iterator into order_ (O(1) lookup, O(1) erase).
    std::unordered_map<std::string, PropertyList::iterator> index_;
};

} // namespace TK4CPP::Utility

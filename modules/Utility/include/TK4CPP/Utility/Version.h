/**
 * @file Version.h
 * @brief Defines the Version class for managing version information.
 * @details
 * The Version class provides an interface to represent and compare software versions
 * composed of four components: major, minor, patch, and tweak.
 *
 * @since 0.1.0
 * @see https://semver.org/ for the Semantic Versioning specification.
 */

#pragma once

#include <compare>
#include <memory>

#include "TK4CPP/Utility/Export.h"

/**
 * @namespace TK4CPP
 * @brief Main namespace for the TK4CPP project.
 */
namespace TK4CPP
{
    /**
     * @namespace TK4CPP::Utility
     * @brief Namespace for general utilities in TK4CPP.
     */
    namespace Utility
    {
        /**
         * @class Version
         * @brief Represents a version with major, minor, patch, and tweak components.
         * @details
         * The Version class encapsulates version information following the
         * <b>major.minor.patch.tweak</b> scheme. It allows creation, access to individual
         * components, and comparison between versions.
         *
         * Example usage:
         * @code
         * auto v1 = TK4CPP::Utility::Version::create(1, 2, 3, 0);
         * auto v2 = TK4CPP::Utility::Version::create(1, 2, 4, 0);
         * if (*v1 < *v2) {
         *     // v1 is older than v2
         * }
         * @endcode
         *
         * @since 0.1.0
         * @see https://semver.org/
         */
        class TK4CPP_UTILITY_EXPORT Version
        {
        public:
            /**
             * @brief Creates a new Version object.
             * @param major Major version number.
             * @param minor Minor version number.
             * @param patch Patch version number.
             * @param tweak Tweak version number.
             * @return std::shared_ptr<Version> Shared pointer to the created Version object.
             *
             * @note Use this static method to create new Version instances.
             */
            static std::shared_ptr<Version> create(int major, int minor, int patch, int tweak);

            /**
             * @brief Three-way comparison between two versions.
             * @param other The version to compare with.
             * @return std::strong_ordering Result of the comparison: less, equal, or greater.
             *
             * @details
             * Components are compared lexicographically in the order
             * major, minor, patch, tweak. This also enables <tt>&lt;</tt>, <tt>&gt;</tt>,
             * <tt>&lt;=</tt> and <tt>&gt;=</tt> on <tt>Version</tt> values.
             *
             * @see https://en.cppreference.com/w/cpp/utility/compare/strong_ordering
             */
            std::strong_ordering operator<=>(const Version& other) const;

            /**
             * @brief Equality comparison between two versions.
             * @param other The version to compare with.
             * @return true if all four components are equal.
             */
            bool operator==(const Version& other) const;

            /**
             * @brief Three-way comparison against a version held by a shared pointer.
             * @param other Shared pointer to the version to compare with; must not be null.
             * @return std::strong_ordering Result of the comparison.
             * @see operator<=>(const Version&) const
             */
            std::strong_ordering operator<=>(const std::shared_ptr<Version>& other) const;

            /**
             * @brief Gets the major version number.
             * @return int Major version number.
             */
            int getMajor() const;

            /**
             * @brief Gets the minor version number.
             * @return int Minor version number.
             */
            int getMinor() const;

            /**
             * @brief Gets the patch version number.
             * @return int Patch version number.
             */
            int getPatch() const;

            /**
             * @brief Gets the tweak version number.
             * @return int Tweak version number.
             */
            int getTweak() const;

        private:
            /**
             * @brief Constructs a Version object.
             * @param major Major version number.
             * @param minor Minor version number.
             * @param patch Patch version number.
             * @param tweak Tweak version number.
             *
             * @note The constructor is private. Use Version::create to instantiate.
             */
            Version(int major, int minor, int patch, int tweak);

            int m_major; ///< Major version number.
            int m_minor; ///< Minor version number.
            int m_patch; ///< Patch version number.
            int m_tweak; ///< Tweak version number.
        };
    }
}

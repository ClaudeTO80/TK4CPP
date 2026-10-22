#pragma once

#include <string>
#include <vector>
#include <memory>
#include <list>

#include "TK4CPP/Utility/FileHandler.h"

/******************************************************************************
@file XML.h
@ingroup xml
@addtogroup xml XML Utilities
@brief Provides tools for managing XML documents.
The XML group contains interfaces and utilities for working with XML data.
It offers abstractions for XML attributes, elements, and documents, enabling
reading, writing, traversing, and modifying XML structures in a flexible way.
Typical usage includes loading XML files, accessing and editing elements and attributes,
and saving changes back to disk or memory.
The main interfaces provided are:
    - IXmlAttribute: Represents an XML attribute (name-value pair). See: https://www.w3schools.com/xml/xml_attributes.asp
    - IXmlElement: Represents an XML element, supporting hierarchical structure, attributes, and text content. See: https://www.w3schools.com/xml/xml_elements.asp
    - IXmlDocument: Represents an entire XML document, supporting loading, saving, and manipulation. See: https://www.w3schools.com/xml/xml_dom.asp
    - These interfaces are designed for extensibility and can be implemented using various XML libraries.
They support common XML operations such as parsing, editing, and serialization.
@par Example

 @code
 // Example: Address Book XML structure using the provided interfaces
 // Root element: People
 // Each person: Person
 // Each person has Addresses, with Address elements of type "house" and "work"

 auto doc = std::make_shared<MyXmlDocument>();
 auto root = doc->createElement("People");
 doc->root()->appendChild(root);

 // Add first person
 auto person1 = root->appendChild("Person");
 person1->setAttribute("name", "Alice");
 person1->setAttribute("age", "30");

 auto addresses1 = person1->appendChild("Addresses");
 auto house1 = addresses1->appendChild("Address");
 house1->setAttribute("type", "house");
 house1->setValue("123 Main St, Springfield");

 auto work1 = addresses1->appendChild("Address");
 work1->setAttribute("type", "work");
 work1->setValue("456 Corporate Ave, Springfield");

 // Add second person
 auto person2 = root->appendChild("Person");
 person2->setAttribute("name", "Bob");
 person2->setAttribute("age", "40");

 auto addresses2 = person2->appendChild("Addresses");
 auto house2 = addresses2->appendChild("Address");
 house2->setAttribute("type", "house");
 house2->setValue("789 Elm St, Shelbyville");

 auto work2 = addresses2->appendChild("Address");
 work2->setAttribute("type", "work");
 work2->setValue("1010 Business Rd, Shelbyville");

 doc->saveToFile("addressbook.xml");
 @endcode


For more information about XML, see:
W3C XML Specification: https://www.w3.org/TR/xml/
XML Tutorials: https://www.w3schools.com/xml/
@{ ***************************************************************************/
 
namespace TK4CPP
{
	namespace XML
	{
        /**
         * @class IXmlAttribute
         * @ingroup xml
         * @brief Interface for representing an XML attribute.
         *
         * The IXmlAttribute interface provides an abstraction for an attribute in an XML element.
         * An XML attribute is a name-value pair associated with an element, used to store metadata or additional information.
         *
         * Typical usage involves accessing the attribute's name and value, for example when parsing or serializing XML documents.
         *
         * @see https://www.w3schools.com/xml/xml_attributes.asp
         * @see https://www.xml.com/pub/a/2003/02/12/examples.html
         *
         * @par Example
         * @code
         * std::shared_ptr<IXmlAttribute> attr = element->attribute("id");
         * if (attr) {
         *     std::cout << "Attribute name: " << attr->name() << std::endl;
         *     std::cout << "Attribute value: " << attr->value() << std::endl;
         * }
         * @endcode
         *
         * @note This is an abstract interface; concrete implementations must provide the actual storage and logic.
         *
         * @author tk4cpp contributors
         * @date 2024
         */
        class IXmlAttribute {
        public:
            virtual ~IXmlAttribute() = default;
            /**
             * @brief Returns the name of the attribute.
             * @return Attribute name as a string.
             */
            virtual std::string name() const = 0;
            /**
             * @brief Returns the value of the attribute.
             * @return Attribute value as a string.
             */
            virtual std::string value() const = 0;
        };


        /**
         * @class IXmlElement
         * @ingroup xml
         * @brief Interface for representing an XML element.
         *
         * Provides an abstraction for an XML element, which can contain a name, value (text), attributes, and children.
         * Allows reading and modifying the name and value, managing attributes and children, and navigating the XML tree.
         *
         * @see https://www.w3schools.com/xml/xml_elements.asp
         *
         * @par Example
         * @code
         * std::shared_ptr<IXmlElement> root = doc->root();
         * root->setName("RootElement");
         * root->setAttribute("version", "1.0");
         * auto child = root->appendChild("ChildElement");
         * child->setValue("Some text");
         * @endcode
         *
         * @note Abstract interface; concrete implementations must provide the actual logic.
         */
        class IXmlElement {
        public:
            virtual ~IXmlElement() = default;

            /**
             * @brief Returns the name of the element.
             * @return Name as a string.
             */
            virtual std::string name() const = 0;

            /**
             * @brief Returns the value (text content) of the element.
             * @return Value as a string.
             */
            virtual std::string value() const = 0;

            /**
             * @brief Sets the name of the element.
             * @param name New name.
             */
            virtual void setName(const std::string& name) = 0;

            /**
             * @brief Sets the value (text content) of the element.
             * @param value New value.
             */
            virtual void setValue(const std::string& value) = 0;

            /**
             * @brief Sets the value of the element using a type convertible to string via std::to_string.
             *
             * This template method allows setting the element's value using any type T that is convertible to std::string
             * via std::to_string. It is enabled only for types where std::to_string(T) is valid, such as standard numeric types.
             *
             * @tparam T Type convertible to std::string using std::to_string.
             * @param value The value to set.
             * @see https://en.cppreference.com/w/cpp/string/basic_string/to_string
             * @since 0.1.0.0
             */
            template<typename T>
            typename std::enable_if_t<
                std::is_convertible_v<decltype(std::to_string(std::declval<T>())), std::string>
            >
                setValue(T value) { setValue(std::to_string(value)); }

            /**
             * @brief Returns the parent of the element, or nullptr if it does not exist.
             */
            virtual std::shared_ptr<IXmlElement> parent() const = 0;

            /**
             * @brief Returns the children of the element.
             * @return Vector of shared_ptr to children.
             */
            virtual std::vector<std::shared_ptr<IXmlElement>> children() const = 0;

            /**
             * @brief Returns all attributes of the element.
             * @return Vector of shared_ptr to attributes.
             */
            virtual std::vector<std::shared_ptr<IXmlAttribute>> attributes() const = 0;

            /**
             * @brief Returns a specific attribute by name.
             * @param name Name of the attribute.
             * @return shared_ptr to the attribute, or nullptr if not found.
             */
            virtual std::shared_ptr<IXmlAttribute> attribute(const std::string& name) const = 0;

            /**
             * @brief Sets or updates an attribute.
             * @param name Name.
             * @param value Value.
             */
            virtual void setAttribute(const std::string& name, const std::string& value) = 0;

            /**
             * @brief Removes an attribute by name.
             * @param name Name of the attribute to remove.
             */
            virtual void removeAttribute(const std::string& name) = 0;

            /**
             * @brief Adds a new child with the specified name.
             * @param name Name of the child.
             * @return shared_ptr to the new child.
             */
            virtual std::shared_ptr<IXmlElement> appendChild(const std::string& name) = 0;

            /**
             * @brief Appends a child element to this element.
             *
             * Adds the specified child element to the list of children of this element.
             * The child element must be a valid IXmlElement instance.
             *
             * @param child The child element to append.
             * @return A shared pointer to the appended child element.
             *
             * @see https://www.w3schools.com/xml/xml_elements.asp
             * @since 0.1.0.0
             */
            virtual std::shared_ptr<IXmlElement> appendElement(const std::shared_ptr<IXmlElement>& child) = 0;

            /**
             * @brief Removes a child.
             * @param child shared_ptr to the child to remove.
             */
            virtual void removeChild(std::shared_ptr<IXmlElement> child) = 0;

			virtual bool hasChild(const std::string& path) const = 0;

            virtual std::shared_ptr<IXmlElement> getChild(const std::string& path) = 0;

            virtual std::shared_ptr<IXmlElement> getChild(const std::string& path) const = 0;

        };


        /**
         * @class IXmlDocument
         * @ingroup xml
         * @brief Interface for representing an XML document.
         *
         * The IXmlDocument interface provides an abstraction for an entire XML document.
         * It supports loading XML data from files or strings, accessing and manipulating the root element,
         * creating new elements, saving the document to files or strings, and importing elements from other documents.
         *
         * This interface is designed to facilitate the management of XML documents in a flexible and extensible way.
         * It is suitable for parsing, editing, and generating XML data, and can be implemented using various XML libraries.
         *
         * @see https://www.w3schools.com/xml/xml_dom.asp
         * @see https://www.xml.com/pub/a/2003/02/12/examples.html
         * @see https://www.w3.org/TR/xml/
         *
         * @par Example
         * @code
         * std::shared_ptr<IXmlDocument> doc = ...;
         * if (doc->loadFromFile("data.xml")) {
         *     auto root = doc->root();
         *     root->setName("Root");
         *     auto child = doc->createElement("Child");
         *     root->appendChild(child);
         *     doc->saveToFile("output.xml");
         * }
         * @endcode
         *
         * @note This is an abstract interface; concrete implementations must provide the actual storage and logic.
         * @note The encoding and standalone properties are available for document-level configuration.
         * @since 0.1.0.0
         */
        class IXmlDocument {
        public:
            virtual ~IXmlDocument() = default;

            /**
             * @brief Loads the XML document from a file.
             * @param path Path to the XML file.
             * @return True if loading succeeds, false otherwise.
             */
            virtual bool loadFromFile(const std::string& path) = 0;

            virtual bool loadFromFile(const std::filesystem::path& path) = 0;
            /**
             * @brief Loads the XML document from a string.
             * @param xmlContent XML content as a string.
             * @return True if loading succeeds, false otherwise.
             */
            virtual bool loadFromString(const std::string& xmlContent) = 0;

            /**
             * @brief Gets the root element of the document.
             * @return Shared pointer to the root element.
             */
            virtual std::shared_ptr<IXmlElement> root() const = 0;

            /**
             * @brief Creates a new element with the given name.
             * @param name Name of the new element.
             * @return Shared pointer to the newly created element.
             */
            virtual std::shared_ptr<IXmlElement> createElement(const std::string& name) = 0;

            ///**
            // * @brief Saves the XML document to a file.
            // * @param path Path to the output file.
            // * @return True if saving succeeds, false otherwise.
            // */
            //virtual bool saveToFile(const std::string& path) const = 0;

            /**
             * @brief Serializes the XML document to a string.
             * @return XML content as a string.
             */
            virtual std::string saveToString() const = 0;

            /**
             * @brief Imports a child element from another document.
             * @param source Shared pointer to the source element.
             * @return Shared pointer to the imported element.
             */
            virtual std::shared_ptr<IXmlElement> importChild(
                const std::shared_ptr<IXmlElement>& source) = 0;

        protected:
            /**
             * @brief Encoding used for the document (default: UTF-8).
             */
            std::string m_encoding{ "UTF-8" };

            /**
             * @brief Indicates if the document is standalone (default: true).
             */
            bool m_standalone{ true };
        };


        /// @brief Concept to check if a type is derived from IXmlDocument.
        /// @ingroup xml
        /// @since 0.1.0.0
        ///
        /// This concept is satisfied if the given type T is derived from IXmlDocument.
        /// It is useful for constraining template parameters to only accept valid XML document types.
        ///
        /// @see IXmlDocument
        /// @code
        /// static_assert(isXmlDocument<MyXmlDocument>);
        /// @endcode
        template<typename T>
        concept isXmlDocument = std::is_base_of_v<IXmlDocument, T>;

        /**
         * @brief Concept that checks if a type is a std::shared_ptr to IXmlDocument or to a derived type.
         * @ingroup xml
         * @since 0.1.0.0
         *
         * This concept is satisfied if the type is std::shared_ptr<IXmlDocument> or
         * std::shared_ptr<T> where T derives from IXmlDocument.
         *
         * @see IXmlDocument
         * @code
         * static_assert(isXmlDocumentPtr<std::shared_ptr<IXmlDocument>>);
         * static_assert(isXmlDocumentPtr<std::shared_ptr<MyXmlDocument>>);
         * @endcode
         */
        template<typename Ptr>
        concept isXmlDocumentPtr =
            requires {
            typename Ptr::element_type;
        } &&
            (std::is_base_of_v<IXmlDocument, typename Ptr::element_type> ||
                std::is_same_v<IXmlDocument, typename Ptr::element_type>);

        /// @class XMLManager
        /// @ingroup xml
        /// @brief Utility class for creating XML document instances.
        /// @since 0.1.0.0
        ///
        /// The XMLManager class provides static factory methods for creating XML document objects.
        /// It uses concepts to ensure that only types derived from IXmlDocument can be instantiated.
        /// This helps enforce type safety and consistency when working with custom XML document implementations.
        ///
        /// @see IXmlDocument
        /// @see isXmlDocument
        ///
        /// @par Example
        /// @code
        /// // Assuming MyXmlDocument implements IXmlDocument and has a constructor taking a root name:
        /// auto doc = XMLManager::create<MyXmlDocument>("Root");
        /// @endcode
        class XMLManager
        {
        public:
            /**
             * @brief Creates a new XML document of the specified type with the given root element name.
             * @tparam T Type of the XML document, must satisfy isXmlDocument.
             * @param rootName Name of the root element for the new document.
             * @return Shared pointer to the created IXmlDocument instance.
             *
             * @see IXmlDocument
             * @see isXmlDocument
             * @link https://www.w3schools.com/xml/xml_dom.asp XML DOM Reference @endlink
             *
             * @par Example
             * @code
             * auto doc = XMLManager::create<MyXmlDocument>("RootElement");
             * @endcode
             */
            template<isXmlDocument T>
            static std::shared_ptr<IXmlDocument> create(const std::string& rootName)
            {
                // The following assumes T has a constructor that accepts a rootName.
                return T::create(rootName);
            }

            /// @brief Serializes the XML document to a string.
            /// @ingroup xml
            /// @tparam T Type of the XML document pointer, must satisfy @ref isXmlDocumentPtr.
            /// @param doc The XML document to serialize (shared pointer).
            /// @return The XML content as a string.
            /// @since 0.1.0.0
            ///
            /// This method calls the @ref IXmlDocument::saveToString() function on the provided document pointer,
            /// returning the XML representation as a string. It is useful for exporting or logging XML data,
            /// or for further processing in memory.
            ///
            /// @see IXmlDocument::saveToString()
            /// @see https://www.w3schools.com/xml/xml_dom.asp XML DOM Reference
            template<isXmlDocumentPtr T>
            static std::string saveToString(T doc)
            {
                return doc->saveToString();
            }

            //! @brief Saves the XML document to a file, handling parent directory creation as needed.
            //!
            //! This overload allows fine-grained control over the parent directory creation behavior.  
            //! If the parent directory of the target file does not exist, it can be created automatically
            //! (including the full directory tree) if @p createParent is true. If @p createParent is false
            //! and the parent directory does not exist, the function will fail and return false.
            //!
            //! The function uses TK4CPP::Utility::FileHandler for directory and file operations, ensuring
            //! consistent file handling and error management across platforms.
            //!
            //! @tparam T Type of the XML document pointer, must satisfy @ref isXmlDocumentPtr.
            //! @param doc The XML document to save (shared pointer).
            //! @param filename The target file path where the XML will be saved.
            //! @param bh The write behavior (see @ref TK4CPP::Utility::FileHandler::WriteBehavior).
            //! @param createParent If true, creates the parent directory (and all necessary ancestors) if it does not exist.
            //! @return True if the document was saved successfully, false otherwise.
            //!
            //! @see IXmlDocument
            //! @see TK4CPP::Utility::FileHandler
            //! @see TK4CPP::Utility::FileHandler::WriteBehavior
            //! @since 0.1.0.0
            //!
            //! @par Example
            //! @code
            //! auto doc = std::make_shared<MyXmlDocument>();
            //! // ... populate doc ...
            //! bool ok = XMLManager::saveToFile(doc, "output/data.xml", TK4CPP::Utility::FileHandler::WriteBehavior::Overwrite, true);
            //! if (ok) {
            //!     std::cout << "XML saved successfully." << std::endl;
            //! } else {
            //!     std::cerr << "Failed to save XML." << std::endl;
            //! }
            //! @endcode
            //!
            //! @link https://www.w3schools.com/xml/xml_dom.asp XML DOM Reference @endlink
            //! @link https://en.cppreference.com/w/cpp/filesystem/path C++ std::filesystem::path @endlink
            template<isXmlDocumentPtr T>
            static bool saveToFile(
                T doc,
                const std::string& filename,
                TK4CPP::Utility::FileHandler::WriteBehavior bh,
                bool createParent)
            {
                std::string str = saveToString(doc);
                std::filesystem::path path(filename);

                // Check if parent directory is defined
                if (path.has_parent_path()) {
                    auto parent = path.parent_path();
                    // If parent directory does not exist
                    if (!TK4CPP::Utility::FileHandler::exists(parent.string())) {
                        if (!createParent)
                            return false;
                        // Create full directory tree if requested
                        if (!TK4CPP::Utility::FileHandler::createFolders(parent).first) {
                            // Failed to create directory
                            return false;
                        }
                    }
                }
                return !TK4CPP::Utility::FileHandler::writeStringToFile(filename,str, bh).empty();
            }

            /**
             * @brief Saves the XML document to a file, creating parent directories if needed.
             *
             * This overload saves the XML document to the specified file, using the given write behavior.
             * Parent directories will be created automatically if they do not exist.
             *
             * @tparam T Type of the XML document pointer, must satisfy @ref isXmlDocumentPtr.
             * @param doc The XML document to save (shared pointer).
             * @param filename The target file path where the XML will be saved.
             * @param bh The write behavior (see @ref TK4CPP::Utility::FileHandler::WriteBehavior).
             * @return True if the document was saved successfully, false otherwise.
             * @see saveToFile(T, const std::string&, TK4CPP::Utility::FileHandler::WriteBehavior, bool)
             * @since 0.1.0.0
             */
            template<isXmlDocumentPtr T>
            static bool saveToFile(T doc, const std::string& filename, TK4CPP::Utility::FileHandler::WriteBehavior bh)
            {
                return saveToFile(doc, filename, bh, true);
            }

            /**
             * @brief Saves the XML document to a file, overwriting if it exists.
             *
             * This overload saves the XML document to the specified file, overwriting any existing file.
             * Parent directories will be created automatically if they do not exist.
             *
             * @tparam T Type of the XML document pointer, must satisfy @ref isXmlDocumentPtr.
             * @param doc The XML document to save (shared pointer).
             * @param filename The target file path where the XML will be saved.
             * @return True if the document was saved successfully, false otherwise.
             * @see saveToFile(T, const std::string&, TK4CPP::Utility::FileHandler::WriteBehavior, bool)
             * @since 0.1.0.0
             */
            template<isXmlDocumentPtr T>
            static bool saveToFile(T doc, const std::string& filename)
            {
                return saveToFile(doc, filename, TK4CPP::Utility::FileHandler::WriteBehavior::Overwrite);
            }

            ///**
            // * @brief Saves the XML document to a file, overwriting if it exists.
            // *
            // * This overload saves the XML document to the specified file, overwriting any existing file.
            // * The parent directories will be created automatically if they do not exist, depending on the @p createParent flag.
            // *
            // * @tparam T Type of the XML document pointer, must satisfy @ref isXmlDocumentPtr.
            // * @param doc The XML document to save (shared pointer).
            // * @param filename The target file path where the XML will be saved.
            // * @param createParent If true, creates the parent directory (and all necessary ancestors) if it does not exist.
            // * @return True if the document was saved successfully, false otherwise.
            // * @see saveToFile(T, const std::string&, TK4CPP::Utility::FileHandler::WriteBehavior, bool)
            // * @since 0.1.0.0
            // */
            template<isXmlDocumentPtr T>
            static bool saveToFile(T doc, const std::string& filename, bool createParent)
            {
                return saveToFile(doc.get(), filename, createParent, TK4CPP::Utility::FileHandler::WriteBehavior::Overwrite);
            }

            /// @brief Loads an XML document from a file.
            /// @ingroup xml
            /// @tparam T Type of the XML document, must satisfy @ref isXmlDocument.
            /// @param filename Path to the XML file.
            /// @return Shared pointer to the loaded XML document instance.
            /// @details
            /// Reads the XML content from the specified file using @ref TK4CPP::Utility::FileHandler::readStringFromFile,
            /// then parses it by calling @ref loadFromString. The document type T must implement a static create() method.
            /// If the file is empty or cannot be read, an empty document is returned.
            /// @see loadFromString
            /// @see TK4CPP::Utility::FileHandler
            /// @link https://www.w3schools.com/xml/xml_dom.asp XML DOM Reference @endlink
            /// @since 0.1.0.0
            template<isXmlDocument T>
            static std::shared_ptr<T> loadFromFile(const std::filesystem::path& filename)
            {
                return loadFromString<T>(TK4CPP::Utility::FileHandler::readStringFromFile(filename.string()));
            }

            /// @brief Loads an XML document from a string.
            /// @ingroup xml
            /// @tparam T Type of the XML document, must satisfy @ref isXmlDocument.
            /// @param content XML content as a string.
            /// @return Shared pointer to the loaded XML document instance.
            /// @details
            /// Creates a new document of type T using T::create(). If the content is not empty,
            /// it is parsed using the document's loadFromString method. Returns the document instance,
            /// which may be empty if parsing fails.
            /// @see IXmlDocument::loadFromString
            /// @link https://www.w3schools.com/xml/xml_dom.asp XML DOM Reference @endlink
            /// @since 0.1.0.0
            template<isXmlDocument T>
            static std::shared_ptr<T> loadFromString(const std::string& content)
            {
                std::shared_ptr<T> doc = T::create();
                if (!content.empty())
                    doc->loadFromString(content);

                return doc;
            }
        };
	}
}

/* @} */
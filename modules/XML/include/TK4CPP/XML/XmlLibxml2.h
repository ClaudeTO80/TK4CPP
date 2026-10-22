/**
 * @file XmlLibxml2.h
 * @brief libxml2 implementation of the TK4CPP XML interfaces.
 * @ingroup xml
 * @since 0.1.0
 *
 * @details
 * The classes in this file implement IXmlAttribute, IXmlElement and IXmlDocument on top of
 * <a href="https://gitlab.gnome.org/GNOME/libxml2">libxml2</a>. No libxml2 header is needed to use them:
 * the library is an implementation detail of the @c XML module.
 *
 * @par Ownership
 * Elements returned by a document keep the underlying libxml2 document alive, so they stay valid
 * even after the LibXml2Document object that produced them is destroyed or reloaded.
 * Elements created with LibXml2Document::createElement(), or detached with
 * LibXml2Element::removeChild(), are owned by their document until they are attached to the tree.
 *
 * @par Thread safety
 * Distinct documents may be used from different threads. A single document, and all elements
 * obtained from it, must not be used concurrently without external synchronisation.
 *
 * @par Names
 * Element and attribute names are lexical XML names (QNames): an element parsed as
 * <tt>&lt;a:b&gt;</tt> is reported as <tt>a:b</tt>. Names that are not valid XML names are rejected.
 *
 * @par Security
 * Documents are parsed without network access and without resolving external entities
 * or loading external DTDs.
 */

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "TK4CPP/XML/Export.h"
#include "TK4CPP/XML/XML.h"

struct _xmlNode;

namespace TK4CPP
{
	namespace XML
	{
		/// @cond INTERNAL
		namespace detail
		{
			struct DocState;
		}
		/// @endcond

		/**
		 * @class LibXml2Attribute
		 * @ingroup xml
		 * @brief An attribute (name/value pair) read from a LibXml2Element.
		 *
		 * The attribute is a snapshot taken when it was requested: later changes to the element
		 * are not reflected, and removing the attribute from the element does not invalidate it.
		 */
		class TK4CPP_XML_EXPORT LibXml2Attribute final : public IXmlAttribute
		{
		public:
			/**
			 * @brief Creates an attribute snapshot.
			 * @param name Attribute name.
			 * @param value Attribute value.
			 */
			LibXml2Attribute(std::string name, std::string value);

			/** @brief Gets the attribute name. */
			std::string name() const override;

			/** @brief Gets the attribute value. */
			std::string value() const override;

		private:
			std::string m_name;  ///< Attribute name.
			std::string m_value; ///< Attribute value.
		};

		/**
		 * @class LibXml2Element
		 * @ingroup xml
		 * @brief An XML element backed by a libxml2 node.
		 *
		 * Instances are created by LibXml2Document and by the element's own factory methods;
		 * the public constructor exists only so that they can be created with @c std::make_shared.
		 *
		 * @par Text content
		 * value() returns the concatenation of the element's direct text and CDATA children
		 * (child elements are not included); setValue() replaces those text children and keeps
		 * the child elements.
		 */
		class TK4CPP_XML_EXPORT LibXml2Element final : public IXmlElement
		{
		public:
			using IXmlElement::setValue;

			/// @cond INTERNAL
			LibXml2Element(std::shared_ptr<detail::DocState> state, _xmlNode* node);
			/// @endcond

			~LibXml2Element() override;

			/** @brief Gets the element name (the lexical QName). */
			std::string name() const override;

			/** @brief Gets the text content of the element. @see LibXml2Element */
			std::string value() const override;

			/**
			 * @brief Renames the element.
			 * @param name The new name. Ignored if it is not a valid XML name.
			 */
			void setName(const std::string& name) override;

			/** @brief Replaces the text content of the element. @see LibXml2Element */
			void setValue(const std::string& value) override;

			/** @brief Gets the parent element, or @c nullptr for the root and for detached elements. */
			std::shared_ptr<IXmlElement> parent() const override;

			/** @brief Gets the child elements in document order. */
			std::vector<std::shared_ptr<IXmlElement>> children() const override;

			/** @brief Gets all the attributes in document order. */
			std::vector<std::shared_ptr<IXmlAttribute>> attributes() const override;

			/**
			 * @brief Gets an attribute by name.
			 * @param name Attribute name (lexical QName).
			 * @return The attribute, or @c nullptr if it does not exist.
			 */
			std::shared_ptr<IXmlAttribute> attribute(const std::string& name) const override;

			/**
			 * @brief Sets an attribute, replacing it if it already exists.
			 * @param name Attribute name. Ignored if it is not a valid XML name.
			 * @param value Attribute value.
			 */
			void setAttribute(const std::string& name, const std::string& value) override;

			/** @brief Removes an attribute. Does nothing if it does not exist. */
			void removeAttribute(const std::string& name) override;

			/**
			 * @brief Creates a new child element and appends it.
			 * @param name Name of the new element.
			 * @return The new element, or @c nullptr if @p name is not a valid XML name.
			 */
			std::shared_ptr<IXmlElement> appendChild(const std::string& name) override;

			/**
			 * @brief Appends an existing element as the last child.
			 * @param child An element of the same document (see LibXml2Document::createElement and
			 *        LibXml2Document::importChild). If it is already in the tree it is moved.
			 * @return @p child on success; @c nullptr if @p child belongs to another document, is
			 *         not a LibXml2Element, or is this element or one of its ancestors.
			 */
			std::shared_ptr<IXmlElement> appendElement(const std::shared_ptr<IXmlElement>& child) override;

			/**
			 * @brief Detaches a direct child from the tree.
			 * @param child The child to detach. Does nothing if it is not a child of this element.
			 * @details The detached element stays valid and can be appended again.
			 */
			void removeChild(std::shared_ptr<IXmlElement> child) override;

			/**
			 * @brief Tells whether a descendant exists at a path.
			 * @param path Slash-separated child names, e.g. <tt>"Person/Addresses/Address"</tt>.
			 */
			bool hasChild(const std::string& path) const override;

			/**
			 * @brief Finds a descendant by path, following the first matching child at each level.
			 * @param path Slash-separated child names, e.g. <tt>"Person/Addresses/Address"</tt>.
			 * @return The element, or @c nullptr if the path is empty, malformed or not found.
			 */
			std::shared_ptr<IXmlElement> getChild(const std::string& path) override;

			/** @copydoc getChild(const std::string&) */
			std::shared_ptr<IXmlElement> getChild(const std::string& path) const override;

		private:
			friend class LibXml2Document;

			std::shared_ptr<detail::DocState> m_state; ///< Keeps the libxml2 document alive.
			_xmlNode* m_node;                          ///< The wrapped node, never null.
		};

		/**
		 * @class LibXml2Document
		 * @ingroup xml
		 * @brief An XML document backed by libxml2.
		 *
		 * @par Example
		 * @code
		 * auto doc  = TK4CPP::XML::LibXml2Document::create("People");
		 * auto root = doc->root();
		 * auto bob  = root->appendChild("Person");
		 * bob->setAttribute("name", "Bob");
		 * bob->appendChild("Age")->setValue(40);
		 * std::string xml = doc->saveToString();
		 * @endcode
		 */
		class TK4CPP_XML_EXPORT LibXml2Document final : public IXmlDocument
		{
		public:
			/**
			 * @brief Creates a document.
			 * @param rootName Name of the root element; if empty the document has no root until a
			 *        document is loaded.
			 * @throws std::invalid_argument if @p rootName is not empty and not a valid XML name.
			 * @throws std::bad_alloc if libxml2 runs out of memory.
			 */
			explicit LibXml2Document(const std::string& rootName = "");

			~LibXml2Document() override;

			LibXml2Document(const LibXml2Document&) = delete;
			LibXml2Document& operator=(const LibXml2Document&) = delete;

			/**
			 * @brief Factory returning a shared pointer.
			 * @param rootName Name of the root element, see LibXml2Document().
			 * @return The new document.
			 */
			static std::shared_ptr<LibXml2Document> create(const std::string& rootName = "");

			/**
			 * @brief Parses a file, replacing the content of the document on success.
			 * @param path Path of the XML file.
			 * @return true on success; false if the file cannot be read or is not well-formed XML,
			 *         in which case the document is left unchanged and lastError() tells why.
			 */
			bool loadFromFile(const std::string& path) override;

			/** @copydoc loadFromFile(const std::string&) */
			bool loadFromFile(const std::filesystem::path& path) override;

			/** @copydoc loadFromFile(const std::string&) */
			bool loadFromFile(const char* path);

			/**
			 * @brief Parses a string, replacing the content of the document on success.
			 * @param xmlContent The XML text.
			 * @return true on success; false if it is not well-formed XML, in which case the
			 *         document is left unchanged and lastError() tells why.
			 */
			bool loadFromString(const std::string& xmlContent) override;

			/** @brief Gets the root element, or @c nullptr if the document has none. */
			std::shared_ptr<IXmlElement> root() const override;

			/**
			 * @brief Creates a detached element owned by this document.
			 * @param name Element name.
			 * @return The element (attach it with IXmlElement::appendElement), or @c nullptr if
			 *         @p name is not a valid XML name.
			 */
			std::shared_ptr<IXmlElement> createElement(const std::string& name) override;

			/**
			 * @brief Serialises the document, with indentation.
			 * @return The XML text including the XML declaration, or an empty string if the
			 *         document has no root. The declaration uses the encoding set with
			 *         setEncoding() and the standalone flag set with setStandalone().
			 */
			std::string saveToString() const override;

			/**
			 * @brief Deep-copies an element, possibly from another document, into this document.
			 * @param source The element to copy; any IXmlElement implementation is accepted.
			 * @return The detached copy (attach it with IXmlElement::appendElement), or
			 *         @c nullptr if @p source is null. Copies from another LibXml2Document keep all
			 *         content; copies from other implementations keep names, attributes and the
			 *         text returned by IXmlElement::value().
			 */
			std::shared_ptr<IXmlElement> importChild(const std::shared_ptr<IXmlElement>& source) override;

			/** @brief Gets the reason why the last load failed (empty if it succeeded). */
			std::string lastError() const;

			/**
			 * @brief Sets the encoding written by saveToString().
			 * @param encoding An encoding name known to libxml2, e.g. "UTF-8" or "ISO-8859-1".
			 */
			void setEncoding(const std::string& encoding);

			/** @brief Sets the standalone flag: true writes <tt>standalone="yes"</tt>, false omits the attribute. */
			void setStandalone(bool standalone);

		private:
			bool parse(const char* data, std::size_t size, const std::string& url);

			std::shared_ptr<detail::DocState> m_state; ///< The libxml2 document and its detached nodes.
			std::string m_lastError;                   ///< Last parse error.
		};
	}
}

#pragma once

#include <functional>
#include "TK4CPP/Utility/FileHandler.h"
#include "XML.h"
#include "tinyxml2.h"

using namespace TK4CPP::Utility;
namespace TK4CPP
{
	namespace XML
	{
		/**
		 * @class TinyXmlAttribute
		 * @ingroup xml
		 * @brief Implementation of IXmlAttribute for TinyXML2 attributes.
		 * @since 0.1.0.0
		 *
		 * This class provides a concrete implementation of the IXmlAttribute interface,
		 * wrapping a pointer to a TinyXML2 XMLAttribute. It allows access to the attribute's
		 * name and value, enabling integration with the generic XML abstraction layer.
		 *
		 * @see IXmlAttribute
		 * @see https://leethomason.github.io/tinyxml2/
		 * @see https://www.w3schools.com/xml/xml_attributes.asp
		 *
		 * @par Example
		 * @code
		 * tinyxml2::XMLDocument doc;
		 * doc.Parse("<root attr=\"value\"/>");
		 * auto* elem = doc.RootElement();
		 * const tinyxml2::XMLAttribute* attr = elem->FirstAttribute();
		 * TinyXmlAttribute xmlAttr(attr);
		 * std::string attrName = xmlAttr.name();   // "attr"
		 * std::string attrValue = xmlAttr.value(); // "value"
		 * @endcode
		 */
		class TinyXmlAttribute : public IXmlAttribute {
			const tinyxml2::XMLAttribute* attr;
		public:
			/**
			 * @brief Constructs a TinyXmlAttribute from a TinyXML2 XMLAttribute pointer.
			 * @param a Pointer to a TinyXML2 XMLAttribute.
			 */
			explicit TinyXmlAttribute(const tinyxml2::XMLAttribute* a);

			/**
			 * @brief Gets the name of the attribute.
			 * @return The attribute name, or an empty string if not available.
			 */
			std::string name() const override;

			/**
			 * @brief Gets the value of the attribute.
			 * @return The attribute value, or an empty string if not available.
			 */
			std::string value() const override;
		};


		/**
		 * @class TinyXmlElement
		 * @ingroup xml
		 * @brief Implementation of IXmlElement for TinyXML2 elements.
		 * @since 0.1.0.0
		 *
		 * This class provides a concrete implementation of the IXmlElement interface,
		 * wrapping a pointer to a TinyXML2 XMLElement. It allows reading and modifying
		 * the element's name, value, attributes, and children, enabling integration with
		 * the generic XML abstraction layer.
		 *
		 * @see IXmlElement
		 * @see https://leethomason.github.io/tinyxml2/
		 * @see https://www.w3schools.com/xml/xml_elements.asp
		 *
		 * @par Example
		 * @code
		 * tinyxml2::XMLDocument doc;
		 * doc.Parse("<root><child id=\"1\">Text</child></root>");
		 * auto* elem = doc.RootElement();
		 * TinyXmlElement xmlElem(elem);
		 * std::string name = xmlElem.name(); // "root"
		 * auto children = xmlElem.children();
		 * if (!children.empty()) {
		 *     std::string childName = children[0]->name(); // "child"
		 *     std::string childValue = children[0]->value(); // "Text"
		 *     auto attr = children[0]->attribute("id");
		 *     if (attr) std::string id = attr->value(); // "1"
		 * }
		 * @endcode
		 */
		class TinyXmlElement : public IXmlElement {
		public:
			/**
			 * @brief Constructs a TinyXmlElement from a TinyXML2 XMLElement pointer.
			 * @param elem Pointer to a TinyXML2 XMLElement (can be nullptr).
			 */
			TinyXmlElement(tinyxml2::XMLElement* elem = nullptr);

			/**
			 * @brief Gets the name of the element.
			 * @return The element name, or an empty string if not available.
			 */
			std::string name() const override;

			/**
			 * @brief Gets the text value of the element.
			 * @return The element value, or an empty string if not available.
			 */
			std::string value() const override;

			/**
			 * @brief Sets the name of the element.
			 * @param newName The new name for the element.
			 */
			void setName(const std::string& newName) override;

			/**
			 * @brief Sets the text value of the element.
			 * @param newValue The new value for the element.
			 */
			void setValue(const std::string& newValue) override;

			/**
			 * @brief Gets the parent element.
			 * @return Shared pointer to the parent element, or nullptr if not available.
			 */
			std::shared_ptr<IXmlElement> parent() const override;

			/**
			 * @brief Gets the child elements.
			 * @return Vector of shared pointers to child elements.
			 */
			std::vector<std::shared_ptr<IXmlElement>> children() const override;

			/**
			 * @brief Gets all attributes of the element.
			 * @return Vector of shared pointers to attributes.
			 */
			std::vector<std::shared_ptr<IXmlAttribute>> attributes() const override;

			/**
			 * @brief Gets a specific attribute by name.
			 * @param name Name of the attribute.
			 * @return Shared pointer to the attribute, or nullptr if not found.
			 */
			std::shared_ptr<IXmlAttribute> attribute(const std::string& name) const override;

			/**
			 * @brief Sets or updates an attribute.
			 * @param name Name of the attribute.
			 * @param value Value of the attribute.
			 */
			void setAttribute(const std::string& name, const std::string& value) override;

			/**
			 * @brief Removes an attribute by name.
			 * @param name Name of the attribute to remove.
			 */
			void removeAttribute(const std::string& name) override;

			/**
			 * @brief Appends a new child element with the specified name.
			 * @param name Name of the new child element.
			 * @return Shared pointer to the new child element.
			 *
			 * @see https://leethomason.github.io/tinyxml2/index.html#insertendchild
			 */
			std::shared_ptr<IXmlElement> appendChild(const std::string& name) override;

			std::shared_ptr<IXmlElement> appendElement(const std::shared_ptr<IXmlElement>& child) override;

			/**
			 * @brief Removes a child element.
			 * @param child Shared pointer to the child element to remove.
			 * @note This method is currently not implemented.
			 */
			void removeChild(std::shared_ptr<IXmlElement> child) override;


			/**
			 * @brief Checks if a child element exists at the specified XPath-like path.
			 *
			 * This method searches for a descendant element using a simple XPath-like path,
			 * where each node is separated by a '/' (e.g., "parent/child/grandchild").
			 * Only element names are supported (no predicates or attributes).
			 *
			 * @param path XPath-like path to the child element (e.g., "child/grandchild").
			 * @return True if the child element exists, false otherwise.
			 *
			 * @see https://leethomason.github.io/tinyxml2/
			 * @see https://www.w3schools.com/xml/xpath_intro.asp
			 *
			 * @par Example
			 * @code
			 * // Given <root><child><grandchild/></child></root>
			 * xmlElem.hasChild("child/grandchild"); // returns true
			 * xmlElem.hasChild("child/missing");    // returns false
			 * @endcode
			 */
			bool hasChild(const std::string& path) const override;

			std::shared_ptr<IXmlElement> getChild(const std::string& path) override;

			std::shared_ptr<IXmlElement> getChild(const std::string& path) const override;
		private:
			tinyxml2::XMLElement* m_elem; ///< Pointer to the underlying TinyXML2 XMLElement.
		};


		/**
		 * @class TinyXmlDocument
		 * @ingroup xml
		 * @brief Implementation of IXmlDocument for TinyXML2 documents.
		 * @since 0.1.0.0
		 *
		 * This class provides a concrete implementation of the IXmlDocument interface,
		 * wrapping a TinyXML2 XMLDocument. It supports loading, saving, and manipulating
		 * XML documents, including element and attribute management.
		 *
		 * @see IXmlDocument
		 * @see https://leethomason.github.io/tinyxml2/
		 * @see https://www.w3schools.com/xml/xml_whatis.asp
		 *
		 * @par Example
		 * @code
		 * TinyXmlDocument doc("root");
		 * auto rootElem = doc.root();
		 * auto child = doc.createElement("child");
		 * child->setValue("Hello");
		 * rootElem->appendChild(child->name());
		 * doc.saveToFile("example.xml");
		 * @endcode
		 */
		class TinyXmlDocument : public IXmlDocument {
		public:
			/**
			 * @brief Constructs a TinyXmlDocument, optionally with a root element.
			 *
			 * This constructor initializes a new TinyXmlDocument instance. If a non-empty
			 * @p rootName is provided and the file does not exist, a root element with the
			 * specified name is created and inserted as the first child of the document.
			 * If the file exists, the document remains empty and should be loaded using
			 * @ref loadFromFile or @ref loadFromString.
			 *
			 * @param rootName Optional name for the root element. If the file exists at this path,
			 *        the document is not initialized with a root element.
			 *
			 * @see https://en.cppreference.com/w/cpp/filesystem/path
			 * @see https://leethomason.github.io/tinyxml2/
			 * @see TK4CPP::Utility::FileHandler::exists
			 * @see loadFromFile
			 * @see loadFromString
			 *
			 * @par Example
			 * @code
			 * TinyXmlDocument doc(std::filesystem::path("root"));
			 * auto rootElem = doc.root();
			 * @endcode
			 *
			 * @note If @p rootName refers to an existing file, the document is not populated.
			 *       Use @ref loadFromFile to load the XML content.
			 */
			TinyXmlDocument(const std::filesystem::path& rootName = "");

			/**
			 * @brief Constructs a TinyXmlDocument, optionally with a root element (string overload).
			 *
			 * This overload allows constructing a TinyXmlDocument using a string as the root element name.
			 * It delegates to the path-based constructor.
			 *
			 * @param rootName Optional name for the root element as a string.
			 *
			 * @see TinyXmlDocument(const std::filesystem::path&)
			 * @see https://en.cppreference.com/w/cpp/string/basic_string
			 *
			 * @par Example
			 * @code
			 * TinyXmlDocument doc("root");
			 * auto rootElem = doc.root();
			 * @endcode
			 */
			TinyXmlDocument(const std::string& rootName = "");

			/**
			 * @brief Destructor. Releases the underlying TinyXML2 document.
			 */
			~TinyXmlDocument() override;

			/**
			 * @brief Loads XML content from a file.
			 * @param path Path to the XML file.
			 * @return True if loading succeeds, false otherwise.
			 * @see https://leethomason.github.io/tinyxml2/index.html#loadfile
			 */
			bool loadFromFile(const std::string& path) override;

			bool loadFromFile(const std::filesystem::path& path) override;

			/**
			 * @brief Loads XML content from a string.
			 * @param xmlContent XML content as a string.
			 * @return True if parsing succeeds, false otherwise.
			 * @see https://leethomason.github.io/tinyxml2/index.html#parse
			 */
			bool loadFromString(const std::string& xmlContent) override;

			/**
			 * @brief Gets the root element of the document.
			 * @return Shared pointer to the root element.
			 */
			std::shared_ptr<IXmlElement> root() const override;

			/**
			 * @brief Creates a new element with the specified name.
			 * @param name Name of the new element.
			 * @return Shared pointer to the new element.
			 */
			std::shared_ptr<IXmlElement> createElement(const std::string& name) override;

			///**
			// * @brief Saves the document to a file, including XML declaration.
			// * @param path Path to the output file.
			// * @return True if saving succeeds, false otherwise.
			// * @see https://leethomason.github.io/tinyxml2/index.html#savefile
			// */
			//bool saveToFile(const std::string& path) const override {
			//    std::string header("");
			//    header
			//        .append("<?xml ")
			//        .append(R"(version="1.0" )")
			//        .append(R"(encoding="UTF-8" )");
			//    if (m_standalone)
			//        header.append(R"(standalone="yes" )");
			//    header.append(R"(?>\n)");
			//    std::string fullFile = header.append(saveToString());
			//    return !FileHandler::writeStringToFile(path, fullFile).empty();
			//}

			/**
			 * @brief Serializes the document to a string.
			 * @return XML content as a string.
			 * @see https://leethomason.github.io/tinyxml2/index.html#print
			 */
			std::string saveToString() const override;

			/**
			 * @brief Imports a child element from another document, recursively copying its structure.
			 * @param source Shared pointer to the source element.
			 * @return Shared pointer to the imported element.
			 *
			 * @par Example
			 * @code
			 * TinyXmlDocument doc1("root1");
			 * TinyXmlDocument doc2("root2");
			 * auto imported = doc1.importChild(doc2.root());
			 * doc1.root()->appendChild(imported->name());
			 * @endcode
			 */
			std::shared_ptr<IXmlElement> importChild(const std::shared_ptr<IXmlElement>& source) override;

			/**
			 * @brief Creates a new TinyXmlDocument with the specified root name.
			 * @param rootName Name of the root element.
			 * @return Shared pointer to the new document.
			 */
			static std::shared_ptr<IXmlDocument> create(const std::string& rootName);

		private:
			std::shared_ptr<tinyxml2::XMLDocument> m_doc{ nullptr }; ///< Underlying TinyXML2 document.
		};

	}
}

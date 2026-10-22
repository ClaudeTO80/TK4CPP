#pragma once

#include <functional>
#include "TK4CPP/Utility/FileHandler.h"
#include "TK4CPP/XML/Abstract/XML.h"
#include <TK4CPP/XML/Impl/tinyxml2/tinyxml2.h>

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
			explicit TinyXmlAttribute(const tinyxml2::XMLAttribute* a) : attr(a) {}

			/**
			 * @brief Gets the name of the attribute.
			 * @return The attribute name, or an empty string if not available.
			 */
			std::string name() const override { return attr ? attr->Name() : ""; }

			/**
			 * @brief Gets the value of the attribute.
			 * @return The attribute value, or an empty string if not available.
			 */
			std::string value() const override { return attr ? attr->Value() : ""; }
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
			TinyXmlElement(tinyxml2::XMLElement* elem = nullptr) { m_elem = elem; }

			/**
			 * @brief Gets the name of the element.
			 * @return The element name, or an empty string if not available.
			 */
			std::string name() const override {
				return m_elem ? m_elem->Name() : "";
			}

			/**
			 * @brief Gets the text value of the element.
			 * @return The element value, or an empty string if not available.
			 */
			std::string value() const override {
				const char* text = m_elem ? m_elem->GetText() : nullptr;
				return text ? text : "";
			}

			/**
			 * @brief Sets the name of the element.
			 * @param newName The new name for the element.
			 */
			void setName(const std::string& newName) override {
				if (m_elem) m_elem->SetName(newName.c_str());
			}

			/**
			 * @brief Sets the text value of the element.
			 * @param newValue The new value for the element.
			 */
			void setValue(const std::string& newValue) override {
				if (m_elem) m_elem->SetText(newValue.c_str());
			}

			/**
			 * @brief Gets the parent element.
			 * @return Shared pointer to the parent element, or nullptr if not available.
			 */
			std::shared_ptr<IXmlElement> parent() const override {
				if (!m_elem) return nullptr;
				auto parentElem = m_elem->Parent() ? m_elem->Parent()->ToElement() : nullptr;
				return parentElem ? std::make_shared<TinyXmlElement>(parentElem) : nullptr;
			}

			/**
			 * @brief Gets the child elements.
			 * @return Vector of shared pointers to child elements.
			 */
			std::vector<std::shared_ptr<IXmlElement>> children() const override {
				std::vector<std::shared_ptr<IXmlElement>> out;
				if (!m_elem) return out;
				for (auto child = m_elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
					out.emplace_back(std::make_shared<TinyXmlElement>(child));
				}
				return out;
			}

			/**
			 * @brief Gets all attributes of the element.
			 * @return Vector of shared pointers to attributes.
			 */
			std::vector<std::shared_ptr<IXmlAttribute>> attributes() const override {
				std::vector<std::shared_ptr<IXmlAttribute>> out;
				if (!m_elem) return out;
				for (auto* attr = m_elem->FirstAttribute(); attr; attr = attr->Next()) {
					out.emplace_back(std::make_shared<TinyXmlAttribute>(attr));
				}
				return out;
			}

			/**
			 * @brief Gets a specific attribute by name.
			 * @param name Name of the attribute.
			 * @return Shared pointer to the attribute, or nullptr if not found.
			 */
			std::shared_ptr<IXmlAttribute> attribute(const std::string& name) const override {
				if (!m_elem) return nullptr;
				auto* attr = m_elem->FindAttribute(name.c_str());
				return attr ? std::make_shared<TinyXmlAttribute>(attr) : nullptr;
			}

			/**
			 * @brief Sets or updates an attribute.
			 * @param name Name of the attribute.
			 * @param value Value of the attribute.
			 */
			void setAttribute(const std::string& name, const std::string& value) override {
				if (m_elem) m_elem->SetAttribute(name.c_str(), value.c_str());
			}

			/**
			 * @brief Removes an attribute by name.
			 * @param name Name of the attribute to remove.
			 */
			void removeAttribute(const std::string& name) override {
				if (m_elem) m_elem->DeleteAttribute(name.c_str());
			}

			/**
			 * @brief Appends a new child element with the specified name.
			 * @param name Name of the new child element.
			 * @return Shared pointer to the new child element.
			 *
			 * @see https://leethomason.github.io/tinyxml2/index.html#insertendchild
			 */
			std::shared_ptr<IXmlElement> appendChild(const std::string& name) override {
				if (!m_elem) return nullptr;
				auto* doc = m_elem->GetDocument();
				//auto* newElem = doc->NewElement(name.c_str());
				return appendElement(std::make_shared<TinyXmlElement>(doc->NewElement(name.c_str())));
				/* return appendElement(doc->NewElement(name.c_str()));
				 m_elem->InsertEndChild(newElem);*/
			}

			std::shared_ptr<IXmlElement> appendElement(const std::shared_ptr<IXmlElement>& child) override {
				if (!m_elem) return nullptr;
				/*auto* doc = m_elem->GetDocument();
				auto* newElem = doc->NewElement(name.c_str());*/
				m_elem->InsertEndChild((std::dynamic_pointer_cast<TinyXmlElement>(child))->m_elem);
				return child;
			}

			/**
			 * @brief Removes a child element.
			 * @param child Shared pointer to the child element to remove.
			 * @note This method is currently not implemented.
			 */
			void removeChild(std::shared_ptr<IXmlElement> child) override {
				// Not implemented: requires access to the underlying XMLElement pointer of 'child'.
			}


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
			bool hasChild(const std::string& path) const override
			{
				return getChild(path) != nullptr;
			}

			std::shared_ptr<IXmlElement> getChild(const std::string& path) override
			{
				std::shared_ptr<TinyXmlElement> output{nullptr};
				if (!m_elem || path.empty())
					return output;

				tinyxml2::XMLElement* current = m_elem;
				size_t start = 0, end = 0;
				while (start < path.size()) {
					end = path.find('/', start);
					std::string node = (end == std::string::npos)
						? path.substr(start)
						: path.substr(start, end - start);

					if (node.empty())
						return output;

					current = current->FirstChildElement(node.c_str());
					if (!current)
						return output;

					if (end == std::string::npos)
						break;
					start = end + 1;
				}
				output.reset(dynamic_cast<TinyXmlElement*>(current));
				return output;
			}

			std::shared_ptr<IXmlElement> getChild(const std::string& path) const override
			{
				std::shared_ptr<TinyXmlElement> output{ nullptr };
				if (!m_elem || path.empty())
					return output;

				tinyxml2::XMLElement* current = m_elem;
				size_t start = 0, end = 0;
				while (start < path.size()) {
					end = path.find('/', start);
					std::string node = (end == std::string::npos)
						? path.substr(start)
						: path.substr(start, end - start);

					if (node.empty())
						return output;

					current = current->FirstChildElement(node.c_str());
					if (!current)
						return output;

					if (end == std::string::npos)
						break;
					start = end + 1;
				}

				output.reset(dynamic_cast<TinyXmlElement*>(current));
				return output;
			}
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
			TinyXmlDocument(const std::filesystem::path& rootName = "") {

				m_doc.reset(new tinyxml2::XMLDocument());

				if (!TK4CPP::Utility::FileHandler::exists(rootName))
					return;

				if (!rootName.empty()) {
					auto elem = m_doc->NewElement(rootName.string().c_str());
					m_doc->InsertFirstChild(elem);
				}
			}

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
			TinyXmlDocument(const std::string& rootName = "") :
				TinyXmlDocument(std::filesystem::path(rootName)) {
			}

			/**
			 * @brief Destructor. Releases the underlying TinyXML2 document.
			 */
			~TinyXmlDocument() override {
				m_doc.reset();
				m_doc = nullptr;
			}

			/**
			 * @brief Loads XML content from a file.
			 * @param path Path to the XML file.
			 * @return True if loading succeeds, false otherwise.
			 * @see https://leethomason.github.io/tinyxml2/index.html#loadfile
			 */
			bool loadFromFile(const std::string& path) override {
				return m_doc->LoadFile(path.c_str()) == tinyxml2::XML_SUCCESS;
			}

			bool loadFromFile(const std::filesystem::path& path) override {
				return m_doc->LoadFile(path.string().c_str()) == tinyxml2::XML_SUCCESS;
			}

			/**
			 * @brief Loads XML content from a string.
			 * @param xmlContent XML content as a string.
			 * @return True if parsing succeeds, false otherwise.
			 * @see https://leethomason.github.io/tinyxml2/index.html#parse
			 */
			bool loadFromString(const std::string& xmlContent) override {
				return m_doc->Parse(xmlContent.c_str()) == tinyxml2::XML_SUCCESS;
			}

			/**
			 * @brief Gets the root element of the document.
			 * @return Shared pointer to the root element.
			 */
			std::shared_ptr<IXmlElement> root() const override {
				return std::make_shared<TinyXmlElement>(m_doc->RootElement());
			}

			/**
			 * @brief Creates a new element with the specified name.
			 * @param name Name of the new element.
			 * @return Shared pointer to the new element.
			 */
			std::shared_ptr<IXmlElement> createElement(const std::string& name) override {
				auto* elem = m_doc->NewElement(name.c_str());
				return std::make_shared<TinyXmlElement>(elem);
			}

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
			std::string saveToString() const override {
				tinyxml2::XMLPrinter printer;
				m_doc->Print(&printer);
				return printer.CStr();
			}

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
			std::shared_ptr<IXmlElement> importChild(const std::shared_ptr<IXmlElement>& source) override {
				if (!m_doc || !source) return nullptr;

				std::function<tinyxml2::XMLElement* (const std::shared_ptr<IXmlElement>&)> cloneElem;
				cloneElem = [this, &cloneElem](const std::shared_ptr<IXmlElement>& src) -> tinyxml2::XMLElement* {
					auto* newElem = m_doc->NewElement(src->name().c_str());
					newElem->SetText(src->value().c_str());
					for (const auto& attr : src->attributes()) {
						newElem->SetAttribute(attr->name().c_str(), attr->value().c_str());
					}
					for (const auto& child : src->children()) {
						auto* childElem = cloneElem(child);
						if (childElem) newElem->InsertEndChild(childElem);
					}
					return newElem;
					};

				tinyxml2::XMLElement* cloned = cloneElem(source);
				return cloned ? std::make_shared<TinyXmlElement>(cloned) : nullptr;
			}

			/**
			 * @brief Creates a new TinyXmlDocument with the specified root name.
			 * @param rootName Name of the root element.
			 * @return Shared pointer to the new document.
			 */
			static std::shared_ptr<IXmlDocument> create(const std::string& rootName) {
				return std::shared_ptr<TinyXmlDocument>(new TinyXmlDocument(rootName));
			}

		private:
			std::shared_ptr<tinyxml2::XMLDocument> m_doc{ nullptr }; ///< Underlying TinyXML2 document.
		};

	}
}

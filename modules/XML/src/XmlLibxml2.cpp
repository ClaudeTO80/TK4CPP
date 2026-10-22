#include "TK4CPP/XML/XmlLibxml2.h"

#include <climits>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <new>
#include <set>
#include <stdexcept>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/valid.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlmemory.h>

namespace TK4CPP
{
	namespace XML
	{
		namespace detail
		{
			/**
			 * @brief Owns a libxml2 document plus the element nodes that are not linked into its tree.
			 *
			 * libxml2 frees only the nodes reachable from the document, so freshly created or detached
			 * elements are tracked here and released with the document.
			 */
			struct DocState
			{
				xmlDocPtr doc{ nullptr };
				std::set<xmlNodePtr> orphans;

				DocState() = default;
				DocState(const DocState&) = delete;
				DocState& operator=(const DocState&) = delete;

				~DocState()
				{
					for (xmlNodePtr node : orphans)
						if (node->parent == nullptr)
							xmlFreeNode(node);
					if (doc)
						xmlFreeDoc(doc);
				}
			};
		}

		namespace
		{
			using detail::DocState;

			void ensureInit()
			{
				static const bool initialised = (xmlInitParser(), true);
				(void)initialised;
			}

			const xmlChar* xc(const std::string& s) { return reinterpret_cast<const xmlChar*>(s.c_str()); }

			std::string str(const xmlChar* s)
			{
				return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
			}

			bool validName(const std::string& name)
			{
				return !name.empty() && xmlValidateName(xc(name), 0) == 0;
			}

			std::string qname(const xmlNode* node)
			{
				if (node->ns && node->ns->prefix)
					return str(node->ns->prefix) + ":" + str(node->name);
				return str(node->name);
			}

			std::string qname(const xmlAttr* attr)
			{
				if (attr->ns && attr->ns->prefix)
					return str(attr->ns->prefix) + ":" + str(attr->name);
				return str(attr->name);
			}

			bool isText(const xmlNode* node)
			{
				return node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE;
			}

			xmlAttrPtr findAttribute(xmlNodePtr node, const std::string& name)
			{
				for (xmlAttrPtr a = node->properties; a; a = a->next)
					if (qname(a) == name)
						return a;
				return nullptr;
			}

			xmlNodePtr findChildElement(xmlNodePtr parent, const std::string& name)
			{
				for (xmlNodePtr c = parent->children; c; c = c->next)
					if (c->type == XML_ELEMENT_NODE && qname(c) == name)
						return c;
				return nullptr;
			}

			/// Follows a slash-separated path of child names; null if empty, malformed or not found.
			xmlNodePtr resolvePath(xmlNodePtr start, const std::string& path)
			{
				if (path.empty())
					return nullptr;
				xmlNodePtr current = start;
				std::size_t begin = 0;
				while (begin <= path.size()) {
					const std::size_t end = path.find('/', begin);
					const std::string part = path.substr(begin, end == std::string::npos ? end : end - begin);
					if (part.empty())
						return nullptr;
					current = findChildElement(current, part);
					if (!current)
						return nullptr;
					if (end == std::string::npos)
						return current;
					begin = end + 1;
				}
				return nullptr;
			}

			/// Creates a detached element in @p state and registers it as an orphan.
			xmlNodePtr newOrphan(DocState& state, const std::string& name)
			{
				xmlNodePtr node = xmlNewDocNode(state.doc, nullptr, xc(name), nullptr);
				if (!node)
					throw std::bad_alloc();
				state.orphans.insert(node);
				return node;
			}

			std::shared_ptr<IXmlElement> wrap(const std::shared_ptr<DocState>& state, xmlNodePtr node)
			{
				return std::make_shared<LibXml2Element>(state, node);
			}
		}

		// ------------------------------------------------------------------ LibXml2Attribute

		LibXml2Attribute::LibXml2Attribute(std::string name, std::string value)
			: m_name(std::move(name)), m_value(std::move(value)) {}

		std::string LibXml2Attribute::name() const { return m_name; }
		std::string LibXml2Attribute::value() const { return m_value; }

		// -------------------------------------------------------------------- LibXml2Element

		LibXml2Element::LibXml2Element(std::shared_ptr<detail::DocState> state, _xmlNode* node)
			: m_state(std::move(state)), m_node(node) {}

		LibXml2Element::~LibXml2Element() = default;

		std::string LibXml2Element::name() const { return qname(m_node); }

		std::string LibXml2Element::value() const
		{
			std::string text;
			for (xmlNodePtr c = m_node->children; c; c = c->next)
				if (isText(c))
					text += str(c->content);
			return text;
		}

		void LibXml2Element::setName(const std::string& name)
		{
			if (validName(name))
				xmlNodeSetName(m_node, xc(name));
		}

		void LibXml2Element::setValue(const std::string& value)
		{
			for (xmlNodePtr c = m_node->children; c;) {
				xmlNodePtr next = c->next;
				if (isText(c)) {
					xmlUnlinkNode(c);
					xmlFreeNode(c);
				}
				c = next;
			}
			if (value.empty())
				return;

			xmlNodePtr text = xmlNewDocText(m_state->doc, xc(value));
			if (!text)
				throw std::bad_alloc();
			if (m_node->children)
				xmlAddPrevSibling(m_node->children, text);
			else
				xmlAddChild(m_node, text);
		}

		std::shared_ptr<IXmlElement> LibXml2Element::parent() const
		{
			xmlNodePtr p = m_node->parent;
			if (p && p->type == XML_ELEMENT_NODE)
				return wrap(m_state, p);
			return nullptr;
		}

		std::vector<std::shared_ptr<IXmlElement>> LibXml2Element::children() const
		{
			std::vector<std::shared_ptr<IXmlElement>> out;
			for (xmlNodePtr c = m_node->children; c; c = c->next)
				if (c->type == XML_ELEMENT_NODE)
					out.push_back(wrap(m_state, c));
			return out;
		}

		std::vector<std::shared_ptr<IXmlAttribute>> LibXml2Element::attributes() const
		{
			std::vector<std::shared_ptr<IXmlAttribute>> out;
			for (xmlAttrPtr a = m_node->properties; a; a = a->next) {
				xmlChar* content = xmlNodeGetContent(reinterpret_cast<xmlNodePtr>(a));
				out.push_back(std::make_shared<LibXml2Attribute>(qname(a), str(content)));
				xmlFree(content);
			}
			return out;
		}

		std::shared_ptr<IXmlAttribute> LibXml2Element::attribute(const std::string& name) const
		{
			xmlAttrPtr a = findAttribute(m_node, name);
			if (!a)
				return nullptr;
			xmlChar* content = xmlNodeGetContent(reinterpret_cast<xmlNodePtr>(a));
			auto out = std::make_shared<LibXml2Attribute>(qname(a), str(content));
			xmlFree(content);
			return out;
		}

		void LibXml2Element::setAttribute(const std::string& name, const std::string& value)
		{
			if (!validName(name))
				return;
			if (xmlAttrPtr existing = findAttribute(m_node, name)) {
				// Replace the value in place so that the attribute keeps its position.
				xmlNodePtr text = xmlNewDocText(m_state->doc, xc(value));
				if (!text)
					throw std::bad_alloc();
				xmlFreeNodeList(existing->children);
				existing->children = existing->last = text;
				text->parent = reinterpret_cast<xmlNodePtr>(existing);
				return;
			}
			if (!xmlSetProp(m_node, xc(name), xc(value)))
				throw std::bad_alloc();
		}

		void LibXml2Element::removeAttribute(const std::string& name)
		{
			if (xmlAttrPtr a = findAttribute(m_node, name))
				xmlRemoveProp(a);
		}

		std::shared_ptr<IXmlElement> LibXml2Element::appendChild(const std::string& name)
		{
			if (!validName(name))
				return nullptr;
			xmlNodePtr node = xmlNewDocNode(m_state->doc, nullptr, xc(name), nullptr);
			if (!node)
				throw std::bad_alloc();
			if (!xmlAddChild(m_node, node)) {
				xmlFreeNode(node);
				throw std::bad_alloc();
			}
			return wrap(m_state, node);
		}

		std::shared_ptr<IXmlElement> LibXml2Element::appendElement(const std::shared_ptr<IXmlElement>& child)
		{
			auto* other = dynamic_cast<LibXml2Element*>(child.get());
			if (!other || other->m_state != m_state || other->m_node->type != XML_ELEMENT_NODE)
				return nullptr;

			// Refuse to create a cycle (libxml2 does not check).
			for (xmlNodePtr p = m_node; p; p = p->parent)
				if (p == other->m_node)
					return nullptr;

			// Unlink explicitly: older libxml2 releases (2.9) do not detach a node from its previous parent.
			xmlUnlinkNode(other->m_node);
			m_state->orphans.erase(other->m_node);
			if (!xmlAddChild(m_node, other->m_node)) {
				m_state->orphans.insert(other->m_node);
				return nullptr;
			}
			return child;
		}

		void LibXml2Element::removeChild(std::shared_ptr<IXmlElement> child)
		{
			auto* other = dynamic_cast<LibXml2Element*>(child.get());
			if (!other || other->m_state != m_state || other->m_node->parent != m_node)
				return;
			xmlUnlinkNode(other->m_node);
			m_state->orphans.insert(other->m_node);
		}

		bool LibXml2Element::hasChild(const std::string& path) const
		{
			return resolvePath(m_node, path) != nullptr;
		}

		std::shared_ptr<IXmlElement> LibXml2Element::getChild(const std::string& path)
		{
			xmlNodePtr node = resolvePath(m_node, path);
			return node ? wrap(m_state, node) : nullptr;
		}

		std::shared_ptr<IXmlElement> LibXml2Element::getChild(const std::string& path) const
		{
			xmlNodePtr node = resolvePath(m_node, path);
			return node ? wrap(m_state, node) : nullptr;
		}

		// ------------------------------------------------------------------- LibXml2Document

		LibXml2Document::LibXml2Document(const std::string& rootName)
		{
			ensureInit();
			if (!rootName.empty() && !validName(rootName))
				throw std::invalid_argument("LibXml2Document: invalid root element name '" + rootName + "'");

			auto state = std::make_shared<DocState>();
			state->doc = xmlNewDoc(reinterpret_cast<const xmlChar*>("1.0"));
			if (!state->doc)
				throw std::bad_alloc();

			if (!rootName.empty()) {
				xmlNodePtr root = xmlNewDocNode(state->doc, nullptr, xc(rootName), nullptr);
				if (!root)
					throw std::bad_alloc();
				xmlDocSetRootElement(state->doc, root);
			}
			m_state = std::move(state);
		}

		LibXml2Document::~LibXml2Document() = default;

		std::shared_ptr<LibXml2Document> LibXml2Document::create(const std::string& rootName)
		{
			return std::make_shared<LibXml2Document>(rootName);
		}

		bool LibXml2Document::parse(const char* data, std::size_t size, const std::string& url)
		{
			ensureInit();
			m_lastError.clear();

			if (size > static_cast<std::size_t>(INT_MAX)) {
				m_lastError = "document too large";
				return false;
			}

			// No network access, no entity substitution, no DTD loading; blank text between elements is dropped.
			constexpr int options = XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING;

			xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
			if (!ctxt) {
				m_lastError = "out of memory";
				return false;
			}
			xmlDocPtr doc = xmlCtxtReadMemory(ctxt, data, static_cast<int>(size),
			                                  url.empty() ? nullptr : url.c_str(), nullptr, options);
			if (!doc) {
				const xmlError* err = xmlCtxtGetLastError(ctxt);
				if (err && err->message) {
					m_lastError = "line " + std::to_string(err->line) + ": " + err->message;
					while (!m_lastError.empty() && (m_lastError.back() == '\n' || m_lastError.back() == '\r'))
						m_lastError.pop_back();
				} else {
					m_lastError = "XML parse error";
				}
				xmlFreeParserCtxt(ctxt);
				return false;
			}
			xmlFreeParserCtxt(ctxt);

			auto state = std::make_shared<DocState>();
			state->doc = doc;
			m_state = std::move(state);
			return true;
		}

		bool LibXml2Document::loadFromFile(const std::string& path)
		{
			return loadFromFile(std::filesystem::path(path));
		}

		bool LibXml2Document::loadFromFile(const char* path)
		{
			return loadFromFile(std::filesystem::path(path ? path : ""));
		}

		bool LibXml2Document::loadFromFile(const std::filesystem::path& path)
		{
			// The file is read here (instead of by libxml2) so that Unicode paths work on every platform.
			std::ifstream in(path, std::ios::binary);
			if (!in) {
				m_lastError = "cannot open file";
				return false;
			}
			std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (in.bad()) {
				m_lastError = "cannot read file";
				return false;
			}
			return parse(content.data(), content.size(), path.generic_string());
		}

		bool LibXml2Document::loadFromString(const std::string& xmlContent)
		{
			return parse(xmlContent.data(), xmlContent.size(), std::string());
		}

		std::shared_ptr<IXmlElement> LibXml2Document::root() const
		{
			xmlNodePtr node = xmlDocGetRootElement(m_state->doc);
			return node ? wrap(m_state, node) : nullptr;
		}

		std::shared_ptr<IXmlElement> LibXml2Document::createElement(const std::string& name)
		{
			if (!validName(name))
				return nullptr;
			return wrap(m_state, newOrphan(*m_state, name));
		}

		std::string LibXml2Document::saveToString() const
		{
			if (!xmlDocGetRootElement(m_state->doc))
				return std::string();

			m_state->doc->standalone = m_standalone ? 1 : -1;   // -1: attribute omitted

			xmlChar* buffer = nullptr;
			int size = 0;
			xmlDocDumpFormatMemoryEnc(m_state->doc, &buffer, &size, m_encoding.c_str(), 1);
			if (!buffer)
				return std::string();
			std::string out(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(size));
			xmlFree(buffer);
			return out;
		}

		std::shared_ptr<IXmlElement> LibXml2Document::importChild(const std::shared_ptr<IXmlElement>& source)
		{
			if (!source)
				return nullptr;

			if (auto* native = dynamic_cast<LibXml2Element*>(source.get())) {
				xmlNodePtr copy = xmlDocCopyNode(native->m_node, m_state->doc, 1);
				if (!copy)
					throw std::bad_alloc();
				m_state->orphans.insert(copy);
				return wrap(m_state, copy);
			}

			// Generic path for foreign implementations: names, attributes and text only.
			std::function<xmlNodePtr(const std::shared_ptr<IXmlElement>&)> clone;
			clone = [&](const std::shared_ptr<IXmlElement>& src) -> xmlNodePtr {
				xmlNodePtr node = xmlNewDocNode(m_state->doc, nullptr, xc(src->name()), nullptr);
				if (!node)
					throw std::bad_alloc();
				for (const auto& attr : src->attributes())
					xmlSetProp(node, xc(attr->name()), xc(attr->value()));
				const std::string text = src->value();
				if (!text.empty())
					xmlAddChild(node, xmlNewDocText(m_state->doc, xc(text)));
				for (const auto& child : src->children())
					xmlAddChild(node, clone(child));
				return node;
			};
			if (!validName(source->name()))
				return nullptr;
			xmlNodePtr copy = clone(source);
			m_state->orphans.insert(copy);
			return wrap(m_state, copy);
		}

		std::string LibXml2Document::lastError() const { return m_lastError; }

		void LibXml2Document::setEncoding(const std::string& encoding) { m_encoding = encoding; }

		void LibXml2Document::setStandalone(bool standalone) { m_standalone = standalone; }
	}
}

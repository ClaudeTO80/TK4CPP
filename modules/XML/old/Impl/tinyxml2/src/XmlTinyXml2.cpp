#pragma once

#include "XmlTinyXml2.h"

using namespace TK4CPP::XML;
using namespace TK4CPP::Utility;

TinyXmlAttribute::TinyXmlAttribute(const tinyxml2::XMLAttribute* a) : attr(a) {}
std::string TinyXmlAttribute::name() const { return attr ? attr->Name() : ""; }
std::string TinyXmlAttribute::value() const { return attr ? attr->Value() : ""; }

TinyXmlElement::TinyXmlElement(tinyxml2::XMLElement* elem) { m_elem = elem; }

std::string TinyXmlElement::name() const {
	return m_elem ? m_elem->Name() : "";
}

std::string TinyXmlElement::value() const {
	const char* text = m_elem ? m_elem->GetText() : nullptr;
	return text ? text : "";
}

void TinyXmlElement::setName(const std::string& newName) {
	if (m_elem) m_elem->SetName(newName.c_str());
}

void TinyXmlElement::setValue(const std::string& newValue) {
	if (m_elem) m_elem->SetText(newValue.c_str());
}

std::shared_ptr<IXmlElement> TinyXmlElement::parent() const {
	if (!m_elem) return nullptr;
	auto parentElem = m_elem->Parent() ? m_elem->Parent()->ToElement() : nullptr;
	return parentElem ? std::make_shared<TinyXmlElement>(parentElem) : nullptr;
}

std::vector<std::shared_ptr<IXmlElement>> TinyXmlElement::children() const {
	std::vector<std::shared_ptr<IXmlElement>> out;
	if (!m_elem) return out;
	for (auto child = m_elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
		out.emplace_back(std::make_shared<TinyXmlElement>(child));
	}
	return out;
}

std::vector<std::shared_ptr<IXmlAttribute>> TinyXmlElement::attributes() const {
	std::vector<std::shared_ptr<IXmlAttribute>> out;
	if (!m_elem) return out;
	for (auto* attr = m_elem->FirstAttribute(); attr; attr = attr->Next()) {
		out.emplace_back(std::make_shared<TinyXmlAttribute>(attr));
	}
	return out;
}

std::shared_ptr<IXmlAttribute> TinyXmlElement::attribute(const std::string& name) const {
	if (!m_elem) return nullptr;
	auto* attr = m_elem->FindAttribute(name.c_str());
	return attr ? std::make_shared<TinyXmlAttribute>(attr) : nullptr;
}

void TinyXmlElement::setAttribute(const std::string& name, const std::string& value) {
	if (m_elem) m_elem->SetAttribute(name.c_str(), value.c_str());
}

void TinyXmlElement::removeAttribute(const std::string& name) {
	if (m_elem) m_elem->DeleteAttribute(name.c_str());
}

std::shared_ptr<IXmlElement> TinyXmlElement::appendChild(const std::string& name) {
	if (!m_elem) return nullptr;
	auto* doc = m_elem->GetDocument();
	//auto* newElem = doc->NewElement(name.c_str());
	return appendElement(std::make_shared<TinyXmlElement>(doc->NewElement(name.c_str())));
	/* return appendElement(doc->NewElement(name.c_str()));
	 m_elem->InsertEndChild(newElem);*/
}

std::shared_ptr<IXmlElement> TinyXmlElement::appendElement(const std::shared_ptr<IXmlElement>& child) {
	if (!m_elem) return nullptr;
	/*auto* doc = m_elem->GetDocument();
	auto* newElem = doc->NewElement(name.c_str());*/
	m_elem->InsertEndChild((std::dynamic_pointer_cast<TinyXmlElement>(child))->m_elem);
	return child;
}

void TinyXmlElement::removeChild(std::shared_ptr<IXmlElement> child) {
	// Not implemented: requires access to the underlying XMLElement pointer of 'child'.
}

bool TinyXmlElement::hasChild(const std::string& path) const
{
	return getChild(path) != nullptr;
}

std::shared_ptr<IXmlElement> TinyXmlElement::getChild(const std::string& path)
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

std::shared_ptr<IXmlElement> TinyXmlElement::getChild(const std::string& path) const
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

TinyXmlDocument::TinyXmlDocument(const std::filesystem::path& rootName) {

	m_doc.reset(new tinyxml2::XMLDocument());

	if (!TK4CPP::Utility::FileHandler::exists(rootName))
		return;

	if (!rootName.empty()) {
		auto elem = m_doc->NewElement(rootName.string().c_str());
		m_doc->InsertFirstChild(elem);
	}
}

TinyXmlDocument::TinyXmlDocument(const std::string& rootName) :
	TinyXmlDocument(std::filesystem::path(rootName)) {
}

TinyXmlDocument::~TinyXmlDocument() {
	m_doc.reset();
	m_doc = nullptr;
}

bool TinyXmlDocument::loadFromFile(const std::string& path) {
	return m_doc->LoadFile(path.c_str()) == tinyxml2::XML_SUCCESS;
}

bool TinyXmlDocument::loadFromFile(const std::filesystem::path& path) {
	return m_doc->LoadFile(path.string().c_str()) == tinyxml2::XML_SUCCESS;
}

bool TinyXmlDocument::loadFromString(const std::string& xmlContent) {
	return m_doc->Parse(xmlContent.c_str()) == tinyxml2::XML_SUCCESS;
}

std::shared_ptr<IXmlElement> TinyXmlDocument::root() const {
	return std::make_shared<TinyXmlElement>(m_doc->RootElement());
}

std::shared_ptr<IXmlElement> TinyXmlDocument::createElement(const std::string& name) {
	auto* elem = m_doc->NewElement(name.c_str());
	return std::make_shared<TinyXmlElement>(elem);
}

std::string TinyXmlDocument::saveToString() const {
	tinyxml2::XMLPrinter printer;
	m_doc->Print(&printer);
	return printer.CStr();
}

std::shared_ptr<IXmlElement> TinyXmlDocument::importChild(const std::shared_ptr<IXmlElement>& source) {
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

std::shared_ptr<IXmlDocument> TinyXmlDocument::create(const std::string& rootName) {
	return std::shared_ptr<TinyXmlDocument>(new TinyXmlDocument(rootName));
}


#pragma once

#include "TK4CPP/XML/Abstract/XML.h"

#include <tinyxml2.h>

using namespace tinyxml2;

namespace TK4CPP
{
	namespace XML
	{
        class TinyXmlAttribute : public IXmlAttribute {
            const tinyxml2::XMLAttribute* attr;
        public:
            explicit TinyXmlAttribute(const tinyxml2::XMLAttribute* a) : attr(a) {}
            std::string name() const override { return attr ? attr->Name() : ""; }
            std::string value() const override { return attr ? attr->Value() : ""; }
        };

        class TinyXmlElement : public IXmlElement<tinyxml2::XMLElement> {
        public:
            TinyXmlElement(tinyxml2::XMLElement* elem = nullptr) { m_elem = elem; }

            std::string name() const override {
                return m_elem ? m_elem->Name() : "";
            }

            std::string value() const override {
                const char* text = m_elem ? m_elem->GetText() : nullptr;
                return text ? text : "";
            }

            void setName(const std::string& newName) override {
                if (m_elem) m_elem->SetName(newName.c_str());
            }

            void setValue(const std::string& newValue) override {
                if (m_elem) m_elem->SetText(newValue.c_str());
            }

            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> parent() const override {
                if (!m_elem) return nullptr;
                auto parentElem = m_elem->Parent() ? m_elem->Parent()->ToElement() : nullptr;
                return parentElem ? std::make_shared<TinyXmlElement>(parentElem) : nullptr;
            }

            std::vector<std::shared_ptr<IXmlElement<tinyxml2::XMLElement>>> children() const override {
                std::vector<std::shared_ptr<IXmlElement<tinyxml2::XMLElement>>> out;
                if (!m_elem) return out;
                for (auto child = m_elem->FirstChildElement(); child; child = child->NextSiblingElement()) {
                    out.emplace_back(std::make_shared<TinyXmlElement>(child));
                }
                return out;
            }

            std::vector<std::shared_ptr<IXmlAttribute>> attributes() const override {
                std::vector<std::shared_ptr<IXmlAttribute>> out;
                if (!m_elem) return out;
                for (auto* attr = m_elem->FirstAttribute(); attr; attr = attr->Next()) {
                    out.emplace_back(std::make_shared<TinyXmlAttribute>(attr));
                }
                return out;
            }

            std::shared_ptr<IXmlAttribute> attribute(const std::string& name) const override {
                if (!m_elem) return nullptr;
                auto* attr = m_elem->FindAttribute(name.c_str());
                return attr ? std::make_shared<TinyXmlAttribute>(attr) : nullptr;
            }

            void setAttribute(const std::string& name, const std::string& value) override {
                if (m_elem) m_elem->SetAttribute(name.c_str(), value.c_str());
            }

            void removeAttribute(const std::string& name) override {
                if (m_elem) m_elem->DeleteAttribute(name.c_str());
            }

            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> appendChild(const std::string& name) override {
                if (!m_elem) return nullptr;
                auto* doc = m_elem->GetDocument();
                auto* newElem = doc->NewElement(name.c_str());
                m_elem->InsertEndChild(newElem);
                return std::make_shared<TinyXmlElement>(newElem);
            }

            void removeChild(std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> child) override {
                /*if (m_elem && child && child->getElem()) {
                    m_elem->DeleteChild(child->getElem());
                }*/
            }
        };

        class TinyXmlDocument : public IXmlDocument<tinyxml2::XMLDocument, tinyxml2::XMLElement> {
        public:
            TinyXmlDocument() { m_doc = new tinyxml2::XMLDocument(); }
            ~TinyXmlDocument() override {
                delete m_doc;
                m_doc = nullptr;
            }

            bool loadFromFile(const std::string& path) override {
                return m_doc->LoadFile(path.c_str()) == tinyxml2::XML_SUCCESS;
            }

            bool loadFromString(const std::string& xmlContent) override {
                return m_doc->Parse(xmlContent.c_str()) == tinyxml2::XML_SUCCESS;
            }

            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> root() const override {
                return std::make_shared<TinyXmlElement>(m_doc->RootElement());
            }

            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> createElement(const std::string& name) override {
                auto* elem = m_doc->NewElement(name.c_str());
                return std::make_shared<TinyXmlElement>(elem);
            }

            bool saveToFile(const std::string& path) const override {
                return m_doc->SaveFile(path.c_str()) == tinyxml2::XML_SUCCESS;
            }

            std::string saveToString() const override {
                tinyxml2::XMLPrinter printer;
                m_doc->Print(&printer);
                return printer.CStr();
            }
        };
    }
}

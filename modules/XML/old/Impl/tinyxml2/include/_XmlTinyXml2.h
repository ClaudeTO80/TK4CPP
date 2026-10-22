#pragma once

#include "XML.h"
#include "tinyxml2.h"

using namespace tinyxml2;

namespace TK4CPP
{
	namespace XML
	{
        class TinyXmlAttribute : public IXmlAttribute {
            const tinyxml2::XMLAttribute* attr;
        public:
            explicit TinyXmlAttribute(const tinyxml2::XMLAttribute* a);
            std::string name() const override;
            std::string value() const override;
        };

        class TinyXmlElement : public IXmlElement<tinyxml2::XMLElement> {
        public:
            TinyXmlElement(tinyxml2::XMLElement* elem = nullptr);

            std::string name() const override;
            std::string value() const override;
            void setName(const std::string& newName) override;
            void setValue(const std::string& newValue) override;
            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> parent() const override;
            std::vector<std::shared_ptr<IXmlElement<tinyxml2::XMLElement>>> children() const override;
            std::vector<std::shared_ptr<IXmlAttribute>> attributes() const override;
            std::shared_ptr<IXmlAttribute> attribute(const std::string& name) const override;
            void setAttribute(const std::string& name, const std::string& value) override;
            void removeAttribute(const std::string& name) override;
            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> appendChild(const std::string& name) override;
            void removeChild(std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> child) override;
        };

        class TinyXmlDocument : public IXmlDocument<tinyxml2::XMLDocument, tinyxml2::XMLElement> {
        public:
            TinyXmlDocument();
            ~TinyXmlDocument() override;

            bool loadFromFile(const std::string& path) override;
            bool loadFromString(const std::string& xmlContent) override;
            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> root() const override;
            std::shared_ptr<IXmlElement<tinyxml2::XMLElement>> createElement(const std::string& name) override;
            bool saveToFile(const std::string& path) const override;
            std::string saveToString() const override;
        };
    }
}

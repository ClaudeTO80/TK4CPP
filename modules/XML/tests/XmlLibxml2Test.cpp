#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "TK4CPP/XML/XML.h"
#include "TK4CPP/XML/XmlLibxml2.h"

using namespace TK4CPP::XML;
namespace fs = std::filesystem;

namespace
{
    std::shared_ptr<LibXml2Document> addressBook()
    {
        auto doc = LibXml2Document::create("People");
        auto root = doc->root();
        auto alice = root->appendChild("Person");
        alice->setAttribute("name", "Alice");
        alice->setAttribute("age", "30");
        auto addresses = alice->appendChild("Addresses");
        auto house = addresses->appendChild("Address");
        house->setAttribute("type", "house");
        house->setValue("123 Main St");
        auto work = addresses->appendChild("Address");
        work->setAttribute("type", "work");
        work->setValue("456 Corporate Ave");
        return doc;
    }
}

TEST(LibXml2Document, CreateBuildsTheRootElement)
{
    auto doc = LibXml2Document::create("Root");
    ASSERT_NE(doc->root(), nullptr);
    EXPECT_EQ(doc->root()->name(), "Root");
    EXPECT_EQ(doc->root()->parent(), nullptr);
}

TEST(LibXml2Document, DefaultDocumentHasNoRootAndCannotBeSerialised)
{
    LibXml2Document doc;
    EXPECT_EQ(doc.root(), nullptr);
    EXPECT_TRUE(doc.saveToString().empty());
}

TEST(LibXml2Document, InvalidRootNameIsRejected)
{
    EXPECT_THROW(LibXml2Document("not valid"), std::invalid_argument);
    EXPECT_THROW(LibXml2Document("1abc"), std::invalid_argument);
}

TEST(LibXml2Element, NavigatesChildrenAndAttributes)
{
    auto doc = addressBook();
    auto person = doc->root()->children().at(0);
    EXPECT_EQ(person->name(), "Person");
    EXPECT_EQ(person->parent()->name(), "People");
    EXPECT_EQ(person->attribute("name")->value(), "Alice");
    EXPECT_EQ(person->attribute("missing"), nullptr);

    auto attrs = person->attributes();
    ASSERT_EQ(attrs.size(), 2u);
    EXPECT_EQ(attrs[0]->name(), "name");   // document order
    EXPECT_EQ(attrs[1]->name(), "age");
}

TEST(LibXml2Element, FindsDescendantsByPath)
{
    auto doc = addressBook();
    auto root = doc->root();
    EXPECT_TRUE(root->hasChild("Person/Addresses/Address"));
    EXPECT_EQ(root->getChild("Person/Addresses/Address")->value(), "123 Main St");   // first match
    EXPECT_FALSE(root->hasChild("Person/Nothing"));
    EXPECT_EQ(root->getChild(""), nullptr);
    EXPECT_EQ(root->getChild("Person//Addresses"), nullptr);
    EXPECT_EQ(root->getChild("Person/"), nullptr);

    const std::shared_ptr<IXmlElement> constRoot = root;
    EXPECT_EQ(std::const_pointer_cast<const IXmlElement>(constRoot)->getChild("Person/Addresses")->name(), "Addresses");
}

TEST(LibXml2Element, ValueIgnoresChildElementsAndSetValueKeepsThem)
{
    auto doc = LibXml2Document::create("a");
    auto root = doc->root();
    root->appendChild("b");
    root->setValue("text");
    EXPECT_EQ(root->value(), "text");
    EXPECT_EQ(root->children().size(), 1u);

    root->setValue("other");
    EXPECT_EQ(root->value(), "other");
    EXPECT_EQ(root->children().size(), 1u);

    root->setValue("");
    EXPECT_EQ(root->value(), "");
}

TEST(LibXml2Element, NumericSetValueUsesTheTemplateOverload)
{
    auto doc = LibXml2Document::create("n");
    doc->root()->setValue(42);
    EXPECT_EQ(doc->root()->value(), "42");
    std::shared_ptr<IXmlElement> asInterface = doc->root();
    asInterface->setValue(2.5);
    EXPECT_EQ(asInterface->value(), std::to_string(2.5));
}

TEST(LibXml2Element, SpecialCharactersRoundTrip)
{
    const std::string tricky = "a & b < c > d \" ' e";
    auto doc = LibXml2Document::create("r");
    doc->root()->setValue(tricky);
    doc->root()->setAttribute("attr", tricky);

    auto copy = LibXml2Document::create();
    ASSERT_TRUE(copy->loadFromString(doc->saveToString())) << copy->lastError();
    EXPECT_EQ(copy->root()->value(), tricky);
    EXPECT_EQ(copy->root()->attribute("attr")->value(), tricky);
}

TEST(LibXml2Element, SetAttributeReplacesInPlaceAndRemoveWorks)
{
    auto doc = LibXml2Document::create("r");
    auto root = doc->root();
    root->setAttribute("a", "1");
    root->setAttribute("b", "2");
    root->setAttribute("a", "3");
    auto attrs = root->attributes();
    ASSERT_EQ(attrs.size(), 2u);
    EXPECT_EQ(attrs[0]->name(), "a");
    EXPECT_EQ(attrs[0]->value(), "3");

    root->removeAttribute("a");
    EXPECT_EQ(root->attribute("a"), nullptr);
    EXPECT_EQ(root->attributes().size(), 1u);
    root->removeAttribute("does-not-exist");   // no-op
}

TEST(LibXml2Element, AttributeSnapshotsSurviveRemoval)
{
    auto doc = LibXml2Document::create("r");
    doc->root()->setAttribute("a", "1");
    auto attr = doc->root()->attribute("a");
    doc->root()->removeAttribute("a");
    EXPECT_EQ(attr->name(), "a");
    EXPECT_EQ(attr->value(), "1");
}

TEST(LibXml2Element, RenameWorksAndInvalidNamesAreIgnored)
{
    auto doc = LibXml2Document::create("old");
    doc->root()->setName("new");
    EXPECT_EQ(doc->root()->name(), "new");
    doc->root()->setName("bad name");
    EXPECT_EQ(doc->root()->name(), "new");
    EXPECT_EQ(doc->root()->appendChild("bad name"), nullptr);
    EXPECT_EQ(doc->createElement("<x>"), nullptr);
    doc->root()->setAttribute("bad attr", "x");
    EXPECT_TRUE(doc->root()->attributes().empty());
}

TEST(LibXml2Element, RemoveChildDetachesAndCanBeReattached)
{
    auto doc = addressBook();
    auto person = doc->root()->children().at(0);
    auto addresses = person->getChild("Addresses");

    person->removeChild(addresses);
    EXPECT_FALSE(person->hasChild("Addresses"));
    EXPECT_EQ(addresses->parent(), nullptr);
    EXPECT_EQ(addresses->name(), "Addresses");   // still alive

    doc->root()->appendElement(addresses);
    EXPECT_EQ(addresses->parent()->name(), "People");
}

TEST(LibXml2Element, RemoveChildOfAnotherParentIsIgnored)
{
    auto doc = addressBook();
    auto person = doc->root()->children().at(0);
    auto address = person->getChild("Addresses/Address");
    doc->root()->removeChild(address);   // not a direct child of root
    EXPECT_TRUE(person->hasChild("Addresses/Address"));
}

TEST(LibXml2Element, AppendElementMovesCreatedAndExistingElements)
{
    auto doc = LibXml2Document::create("r");
    auto a = doc->root()->appendChild("a");
    auto b = doc->root()->appendChild("b");
    auto created = doc->createElement("c");
    EXPECT_EQ(created->parent(), nullptr);

    EXPECT_EQ(a->appendElement(created), created);
    EXPECT_EQ(created->parent()->name(), "a");

    EXPECT_EQ(b->appendElement(created), created);   // move
    EXPECT_FALSE(a->hasChild("c"));
    EXPECT_TRUE(b->hasChild("c"));
}

TEST(LibXml2Element, AppendElementRejectsCyclesAndForeignElements)
{
    auto doc = LibXml2Document::create("r");
    auto child = doc->root()->appendChild("child");
    auto grandchild = child->appendChild("grandchild");

    EXPECT_EQ(grandchild->appendElement(child), nullptr);        // ancestor
    EXPECT_EQ(child->appendElement(child), nullptr);             // itself
    EXPECT_EQ(child->appendElement(doc->root()), nullptr);       // root is an ancestor

    auto other = LibXml2Document::create("other");
    EXPECT_EQ(child->appendElement(other->root()), nullptr);     // different document
    EXPECT_EQ(child->appendElement(nullptr), nullptr);
}

TEST(LibXml2Document, ImportChildCopiesAcrossDocuments)
{
    auto source = addressBook();
    auto target = LibXml2Document::create("Archive");

    auto copy = target->importChild(source->root()->getChild("Person"));
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->parent(), nullptr);
    target->root()->appendElement(copy);

    EXPECT_EQ(target->root()->getChild("Person")->attribute("name")->value(), "Alice");
    EXPECT_EQ(target->root()->getChild("Person/Addresses/Address")->value(), "123 Main St");
    // The source is untouched.
    EXPECT_TRUE(source->root()->hasChild("Person/Addresses"));
    EXPECT_EQ(target->importChild(nullptr), nullptr);
}

TEST(LibXml2Document, ElementsOutliveTheirDocument)
{
    std::shared_ptr<IXmlElement> element;
    {
        auto doc = addressBook();
        element = doc->root()->getChild("Person/Addresses/Address");
    }
    EXPECT_EQ(element->value(), "123 Main St");
    EXPECT_EQ(element->parent()->parent()->parent()->name(), "People");
}

TEST(LibXml2Document, ElementsOutliveAReload)
{
    auto doc = addressBook();
    auto oldRoot = doc->root();
    ASSERT_TRUE(doc->loadFromString("<fresh/>"));
    EXPECT_EQ(doc->root()->name(), "fresh");
    EXPECT_EQ(oldRoot->name(), "People");
}

TEST(LibXml2Document, UnattachedElementsAreReleasedWithTheDocument)
{
    // Nothing to assert: this is the leak/use-after-free check when the tests run under a sanitizer.
    auto doc = LibXml2Document::create("r");
    auto lonely = doc->createElement("lonely");
    lonely->appendChild("child");
    auto copy = doc->importChild(doc->root());
    doc->root()->appendChild("detached");
    doc->root()->removeChild(doc->root()->getChild("detached"));
    SUCCEED();
}

TEST(LibXml2Document, SerialisesWithDeclarationAndIndentation)
{
    auto xml = addressBook()->saveToString();
    EXPECT_EQ(xml.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>", 0), 0u) << xml;
    EXPECT_NE(xml.find("\n  <Person"), std::string::npos) << xml;

    auto doc = LibXml2Document::create("r");
    doc->setStandalone(false);
    doc->setEncoding("ISO-8859-1");
    xml = doc->saveToString();
    EXPECT_NE(xml.find("encoding=\"ISO-8859-1\""), std::string::npos) << xml;
    EXPECT_EQ(xml.find("standalone"), std::string::npos) << xml;
}

TEST(LibXml2Document, Utf8TextRoundTrips)
{
    const std::string text = "caff\xC3\xA8 \xE2\x82\xAC \xE6\x97\xA5\xE6\x9C\xAC";   // caffè € 日本
    auto doc = LibXml2Document::create("r");
    doc->root()->setValue(text);
    auto copy = LibXml2Document::create();
    ASSERT_TRUE(copy->loadFromString(doc->saveToString()));
    EXPECT_EQ(copy->root()->value(), text);
}

TEST(LibXml2Document, ParsesNamespacedNamesLexically)
{
    auto doc = LibXml2Document::create();
    ASSERT_TRUE(doc->loadFromString(
        "<a:root xmlns:a=\"urn:a\"><a:item a:key=\"v\">x</a:item></a:root>"));
    EXPECT_EQ(doc->root()->name(), "a:root");
    EXPECT_EQ(doc->root()->getChild("a:item")->value(), "x");
    EXPECT_EQ(doc->root()->getChild("a:item")->attribute("a:key")->value(), "v");
}

TEST(LibXml2Document, MalformedInputFailsAndKeepsTheDocument)
{
    auto doc = addressBook();
    EXPECT_FALSE(doc->loadFromString("<a><b></a>"));
    EXPECT_FALSE(doc->lastError().empty());
    EXPECT_EQ(doc->root()->name(), "People");   // unchanged

    EXPECT_FALSE(doc->loadFromString(""));
    EXPECT_FALSE(doc->loadFromString("plain text"));
    EXPECT_TRUE(doc->loadFromString("<ok/>"));
    EXPECT_TRUE(doc->lastError().empty());
}

TEST(LibXml2Document, ExternalEntitiesAreNotResolved)
{
    const fs::path secret = fs::temp_directory_path() / "tk4cpp_xxe_secret.txt";
    { std::ofstream(secret) << "TOP-SECRET"; }
    const std::string xml =
        "<?xml version=\"1.0\"?><!DOCTYPE r [<!ENTITY x SYSTEM \"" + secret.generic_string() + "\">]><r>&x;</r>";

    auto doc = LibXml2Document::create();
    doc->loadFromString(xml);   // may fail or succeed, but must never leak the file content
    if (doc->root()) {
        EXPECT_EQ(doc->root()->value().find("TOP-SECRET"), std::string::npos);
    }
    EXPECT_EQ(doc->saveToString().find("TOP-SECRET"), std::string::npos);
    fs::remove(secret);
}

TEST(LibXml2Document, IgnorableWhitespaceDoesNotLeakIntoValues)
{
    auto doc = LibXml2Document::create();
    ASSERT_TRUE(doc->loadFromString("<a>\n  <b>1</b>\n  <c>2</c>\n</a>"));
    EXPECT_EQ(doc->root()->value(), "");
    EXPECT_EQ(doc->root()->children().size(), 2u);
    EXPECT_EQ(doc->root()->getChild("c")->value(), "2");
}

class XmlFileTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir = fs::temp_directory_path() / (std::string("tk4cpp_xml_") + info->name());
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override { fs::remove_all(dir); }
    fs::path dir;
};

TEST_F(XmlFileTest, SaveAndLoadThroughXMLManager)
{
    auto doc = addressBook();
    const auto file = (dir / "deep" / "er" / "book.xml").string();

    EXPECT_FALSE(XMLManager::saveToFile(doc, file, TK4CPP::Utility::FileHandler::WriteBehavior::Overwrite, false));
    EXPECT_TRUE(XMLManager::saveToFile(doc, file));
    EXPECT_TRUE(XMLManager::saveToFile(doc, file, true));   // the bool overload

    auto loaded = XMLManager::loadFromFile<LibXml2Document>(fs::path(file));
    ASSERT_NE(loaded->root(), nullptr);
    EXPECT_EQ(loaded->root()->getChild("Person/Addresses/Address")->value(), "123 Main St");
}

TEST_F(XmlFileTest, LoadFromFileOverloadsAndMissingFile)
{
    const auto file = dir / "a.xml";
    { std::ofstream(file) << "<a><b/></a>"; }

    LibXml2Document doc;
    EXPECT_TRUE(doc.loadFromFile(file));
    EXPECT_TRUE(doc.loadFromFile(file.string()));
    EXPECT_TRUE(doc.loadFromFile(file.string().c_str()));
    EXPECT_FALSE(doc.loadFromFile((dir / "missing.xml").string()));
    EXPECT_FALSE(doc.lastError().empty());
    EXPECT_EQ(doc.root()->name(), "a");   // failed load keeps the previous content
}

TEST(XMLManager, CreatesDocumentsThroughTheInterface)
{
    std::shared_ptr<IXmlDocument> doc = XMLManager::create<LibXml2Document>("Root");
    ASSERT_NE(doc->root(), nullptr);
    doc->root()->appendChild("x")->setValue("1");
    EXPECT_NE(XMLManager::saveToString(doc).find("<x>1</x>"), std::string::npos);

    auto parsed = XMLManager::loadFromString<LibXml2Document>("<q><r/></q>");
    EXPECT_TRUE(parsed->root()->hasChild("r"));
    EXPECT_EQ(XMLManager::loadFromString<LibXml2Document>("")->root(), nullptr);
}

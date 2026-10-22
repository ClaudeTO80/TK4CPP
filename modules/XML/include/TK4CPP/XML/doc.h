/******************************************************************************
@file doc.h
@brief Doxygen documentation for the XML group.
@defgroup xml XML Utilities
@ingroup tk4cpp
@brief Utilities for working with XML files and data.
This group contains all files and modules related to XML processing, parsing, and manipulation.
It includes functionality for reading, writing, validating, and transforming XML documents.
@section xml_features_sec Features
XML file reading and writing
XML parsing and validation
Support for attributes and namespaces
Utilities for traversing and modifying XML nodes
@section xml_usage_sec Usage Example
@code
// Load an XML file
auto doc = TK4CPP::XML::LibXml2Document::create();
if (doc->loadFromFile(std::filesystem::path("data.xml"))) {
    // Access the root element
    auto root = doc->root();
    // Read an attribute
    std::string value = root->attribute("name")->value();
}
@endcode
@section xml_files_sec Files
All files in the xml folder and its subfolders are part of this group.
@{ ***************************************************************************/
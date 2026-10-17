// System.Xml subset: a small DOM with parser and writer.
#pragma once
#include "io.h"

namespace System { namespace Xml {

struct XmlDocument;
struct XmlElement;
struct XmlAttribute;
struct XmlNodeList;
struct XmlAttributeCollection;

enum class XmlNodeType : int32_t { None = 0, Element = 1, Attribute = 2, Text = 3, CDATA = 4, Comment = 8, Document = 9, Whitespace = 13, XmlDeclaration = 17 };

struct XmlNode : Object, Collections::IEnumerable
{
    XmlNodeType nodeType = XmlNodeType::None;
    String* name = nullptr;
    String* value = nullptr;           // text / attribute value
    XmlNode* parent = nullptr;
    XmlDocument* owner = nullptr;
    Collections::Generic::List<XmlNode*>* children = nullptr;
    XmlAttributeCollection* attributes = nullptr;

    virtual String* get_Name() { return name; }
    String* get_LocalName() { return name; }
    XmlNodeType get_NodeType() { return nodeType; }
    virtual String* get_InnerText();
    virtual void set_InnerText(String* s);
    String* get_Value() { return value; }
    void set_Value(String* v) { value = v; }
    String* get_InnerXml();
    String* get_OuterXml();
    XmlNodeList* get_ChildNodes();
    XmlNode* get_FirstChild();
    XmlNode* get_LastChild();
    XmlNode* get_NextSibling();
    XmlNode* get_ParentNode() { return parent; }
    bool get_HasChildNodes() { return children && children->get_Count() > 0; }
    XmlAttributeCollection* get_Attributes() { return attributes; }
    XmlDocument* get_OwnerDocument() { return owner; }
    XmlElement* get_Item(String* name);
    XmlNode* AppendChild(XmlNode* n);
    XmlNode* RemoveChild(XmlNode* n);
    XmlNode* InsertBefore(XmlNode* n, XmlNode* ref);
    void RemoveAll();
    XmlNode* SelectSingleNode(String* path);
    XmlNodeList* SelectNodes(String* path);
    Collections::IEnumerator* GetEnumerator() override;
};

struct XmlAttribute final : XmlNode
{
    String* get_InnerText() override { return value ? value : String::Empty(); }
    void set_InnerText(String* s) override { value = s; }
};

struct XmlNodeList final : Object, Collections::IEnumerable
{
    Collections::Generic::List<XmlNode*>* items;
    XmlNodeList() { items = ::sys::gcnew<Collections::Generic::List<XmlNode*>>(); }
    int32_t get_Count() { return items->get_Count(); }
    XmlNode* get_Item(int32_t i) { return i >= 0 && i < items->get_Count() ? (*items)[i] : nullptr; }
    XmlNode* Item(int32_t i) { return get_Item(i); }
    Collections::IEnumerator* GetEnumerator() override;
};

struct XmlAttributeCollection final : Object, Collections::IEnumerable
{
    Collections::Generic::List<XmlAttribute*>* items;
    XmlNode* ownerNode = nullptr;
    XmlAttributeCollection() { items = ::sys::gcnew<Collections::Generic::List<XmlAttribute*>>(); }
    int32_t get_Count() { return items->get_Count(); }
    XmlAttribute* get_Item(String* name);
    XmlAttribute* get_Item(int32_t i) { return i >= 0 && i < items->get_Count() ? (*items)[i] : nullptr; }
    XmlAttribute* Append(XmlAttribute* a);
    XmlAttribute* GetNamedItem(String* name) { return get_Item(name); }
    void Remove(XmlAttribute* a) { items->Remove(a); }
    void RemoveAll() { items->Clear(); }
    Collections::IEnumerator* GetEnumerator() override;
};

struct XmlElement final : XmlNode
{
    XmlNodeList* GetElementsByTagName(String* name);
    bool HasAttribute(String* name) { return attributes->get_Item(name) != nullptr; }
    String* GetAttribute(String* name) { auto* a = attributes->get_Item(name); return a ? a->value : String::Empty(); }
    void SetAttribute(String* name, String* value);
    void RemoveAttribute(String* name) { if (auto* a = attributes->get_Item(name)) attributes->Remove(a); }
    bool get_HasAttributes() { return attributes->get_Count() > 0; }
    bool get_IsEmpty() { return !get_HasChildNodes(); }
};

struct XmlDocument final : XmlNode
{
    XmlDocument();
    void Load(String* path);
    void Load(IO::Stream* s);
    void LoadXml(String* xml);
    void Save(String* path);
    void Save(IO::Stream* s);
    XmlElement* CreateElement(String* name);
    XmlAttribute* CreateAttribute(String* name);
    XmlNode* CreateTextNode(String* text);
    XmlNode* CreateXmlDeclaration(String* v, String* enc, String* sa);
    XmlNode* CreateComment(String* c);
    XmlElement* get_DocumentElement();
    XmlNodeList* GetElementsByTagName(String* name);
    void Parse(const std::u16string& text);
    std::string Serialize(bool declaration);
};

}} // namespace System::Xml

namespace System { namespace Xml { namespace Serialization {
struct XmlSerializer final : Object
{
    Type* type;
    explicit XmlSerializer(Type* t) : type(t) {}
    void Serialize(IO::Stream* s, Object* o);
    Object* Deserialize(IO::Stream* s);
};
struct XmlIgnoreAttribute : Attribute {};
struct XmlElementAttribute : Attribute {};
struct XmlAttributeAttribute : Attribute {};
}}}

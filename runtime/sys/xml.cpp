#include "runtime_sys.h"
#include <cstdio>

using namespace System;
using namespace System::Xml;
using ::sys::gcnew;

namespace {

String* U(const std::u16string& s) { return String::FromStd(s); }

struct Parser
{
    const std::u16string& t;
    size_t p = 0;
    XmlDocument* doc;
    explicit Parser(const std::u16string& text, XmlDocument* d) : t(text), doc(d) {}

    [[noreturn]] void Fail(const char* msg)
    {
        char b[160];
        snprintf(b, sizeof b, "XML parse error at %zu: %s", p, msg);
        ::sys::throw_msg("XmlException", b);
    }
    bool StartsWith(const char16_t* s)
    {
        size_t n = std::char_traits<char16_t>::length(s);
        return t.compare(p, n, s) == 0;
    }
    static bool IsSpace(char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; }
    void SkipWs() { while (p < t.size() && IsSpace(t[p])) p++; }
    std::u16string Name()
    {
        size_t s = p;
        while (p < t.size() && !IsSpace(t[p]) && t[p] != u'>' && t[p] != u'/' && t[p] != u'=' && t[p] != u'?') p++;
        return t.substr(s, p - s);
    }
    std::u16string Decode(const std::u16string& raw)
    {
        std::u16string r;
        r.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); i++)
        {
            char16_t c = raw[i];
            if (c == u'\r') { if (i + 1 < raw.size() && raw[i + 1] == u'\n') continue; r.push_back(u'\n'); continue; }
            if (c != u'&') { r.push_back(c); continue; }
            size_t semi = raw.find(u';', i);
            if (semi == std::u16string::npos) { r.push_back(c); continue; }
            std::u16string ent = raw.substr(i + 1, semi - i - 1);
            if (ent == u"lt") r.push_back(u'<');
            else if (ent == u"gt") r.push_back(u'>');
            else if (ent == u"amp") r.push_back(u'&');
            else if (ent == u"quot") r.push_back(u'"');
            else if (ent == u"apos") r.push_back(u'\'');
            else if (!ent.empty() && ent[0] == u'#')
            {
                std::string num(ent.begin() + 1, ent.end());
                long v = (num[0] == 'x' || num[0] == 'X') ? strtol(num.c_str() + 1, nullptr, 16) : strtol(num.c_str(), nullptr, 10);
                r.push_back((char16_t)v);
            }
            else { r.append(raw, i, semi - i + 1); }
            i = semi;
        }
        return r;
    }
    void ParseContent(XmlNode* parent)
    {
        while (p < t.size())
        {
            if (t[p] == u'<')
            {
                if (StartsWith(u"</")) return;
                if (StartsWith(u"<!--"))
                {
                    size_t e = t.find(u"-->", p);
                    if (e == std::u16string::npos) Fail("comment");
                    p = e + 3;
                    continue;
                }
                if (StartsWith(u"<![CDATA["))
                {
                    size_t e = t.find(u"]]>", p);
                    if (e == std::u16string::npos) Fail("cdata");
                    auto* n = doc->CreateTextNode(U(t.substr(p + 9, e - p - 9)));
                    parent->AppendChild(n);
                    p = e + 3;
                    continue;
                }
                if (StartsWith(u"<?"))
                {
                    size_t e = t.find(u"?>", p);
                    if (e == std::u16string::npos) Fail("pi");
                    p = e + 2;
                    continue;
                }
                if (StartsWith(u"<!"))
                {
                    size_t e = t.find(u'>', p);
                    p = e == std::u16string::npos ? t.size() : e + 1;
                    continue;
                }
                ParseElement(parent);
            }
            else
            {
                size_t s = p;
                while (p < t.size() && t[p] != u'<') p++;
                std::u16string raw = t.substr(s, p - s);
                bool ws = true;
                for (auto c : raw) if (!IsSpace(c)) { ws = false; break; }
                if (!ws) parent->AppendChild(doc->CreateTextNode(U(Decode(raw))));
            }
        }
    }
    void ParseElement(XmlNode* parent)
    {
        p++; // <
        std::u16string name = Name();
        XmlElement* el = doc->CreateElement(U(name));
        parent->AppendChild(el);
        while (true)
        {
            SkipWs();
            if (p >= t.size()) Fail("eof in tag");
            if (t[p] == u'/') { if (p + 1 < t.size() && t[p + 1] == u'>') { p += 2; return; } Fail("bad /"); }
            if (t[p] == u'>') { p++; break; }
            std::u16string an = Name();
            SkipWs();
            if (p >= t.size() || t[p] != u'=') Fail("attr =");
            p++;
            SkipWs();
            char16_t q = t[p];
            if (q != u'"' && q != u'\'') Fail("attr quote");
            p++;
            size_t e = t.find(q, p);
            if (e == std::u16string::npos) Fail("attr end");
            auto* a = doc->CreateAttribute(U(an));
            a->value = U(Decode(t.substr(p, e - p)));
            el->attributes->Append(a);
            p = e + 1;
        }
        ParseContent(el);
        if (!StartsWith(u"</")) Fail("missing end tag");
        p += 2;
        Name();
        SkipWs();
        if (p < t.size() && t[p] == u'>') p++;
    }
};

void Escape(std::u16string& out, String* s, bool attr)
{
    if (!s) return;
    for (int32_t i = 0; i < s->length; i++)
    {
        char16_t c = s->data[i];
        switch (c)
        {
        case u'<': out += u"&lt;"; break;
        case u'>': out += u"&gt;"; break;
        case u'&': out += u"&amp;"; break;
        case u'"': if (attr) out += u"&quot;"; else out += c; break;
        default: out += c;
        }
    }
}

void WriteNode(std::u16string& out, XmlNode* n, int depth)
{
    std::u16string ind((size_t)depth * 2, u' ');
    if (n->nodeType == XmlNodeType::Text) { Escape(out, n->value, false); return; }
    if (n->nodeType == XmlNodeType::Comment) { out += ind + u"<!--" + (n->value ? n->value->ToU16() : u"") + u"-->\n"; return; }
    if (n->nodeType == XmlNodeType::XmlDeclaration) { out += u"<?xml " + (n->value ? n->value->ToU16() : u"version=\"1.0\"") + u"?>\n"; return; }
    if (n->nodeType != XmlNodeType::Element) return;
    out += ind + u"<" + n->name->ToU16();
    for (int32_t i = 0; i < n->attributes->items->get_Count(); i++)
    {
        auto* a = (*n->attributes->items)[i];
        out += u" " + a->name->ToU16() + u"=\"";
        Escape(out, a->value, true);
        out += u"\"";
    }
    int32_t nc = n->children->get_Count();
    if (nc == 0) { out += u" />\n"; return; }
    bool textOnly = true;
    for (int32_t i = 0; i < nc; i++) if ((*n->children)[i]->nodeType != XmlNodeType::Text) textOnly = false;
    out += u">";
    if (textOnly)
    {
        for (int32_t i = 0; i < nc; i++) WriteNode(out, (*n->children)[i], 0);
        out += u"</" + n->name->ToU16() + u">\n";
        return;
    }
    out += u"\n";
    for (int32_t i = 0; i < nc; i++)
    {
        auto* c = (*n->children)[i];
        if (c->nodeType == XmlNodeType::Text) { out += std::u16string((size_t)(depth + 1) * 2, u' '); WriteNode(out, c, 0); out += u"\n"; }
        else WriteNode(out, c, depth + 1);
    }
    out += ind + u"</" + n->name->ToU16() + u">\n";
}

void Collect(XmlNode* n, String* name, XmlNodeList* out)
{
    for (int32_t i = 0; i < n->children->get_Count(); i++)
    {
        auto* c = (*n->children)[i];
        if (c->nodeType != XmlNodeType::Element) continue;
        if ((name->length == 1 && name->data[0] == u'*') || String::op_Equality(c->name, name)) out->items->Add(c);
        Collect(c, name, out);
    }
}

struct NodeEnum final : Object, Collections::IEnumerator
{
    Collections::Generic::List<XmlNode*>* l; int32_t i = -1;
    explicit NodeEnum(Collections::Generic::List<XmlNode*>* x) : l(x) {}
    bool MoveNext() override { i++; return i < l->get_Count(); }
    Object* get_Current() override { return (*l)[i]; }
};
struct AttrEnum final : Object, Collections::IEnumerator
{
    Collections::Generic::List<XmlAttribute*>* l; int32_t i = -1;
    explicit AttrEnum(Collections::Generic::List<XmlAttribute*>* x) : l(x) {}
    bool MoveNext() override { i++; return i < l->get_Count(); }
    Object* get_Current() override { return (*l)[i]; }
};

} // namespace

namespace System { namespace Xml {

String* XmlNode::get_InnerText()
{
    if (nodeType == XmlNodeType::Text) return value ? value : String::Empty();
    if (!children) return String::Empty();
    int32_t n = children->get_Count();
    if (n == 1 && (*children)[0]->nodeType == XmlNodeType::Text) return (*children)[0]->get_InnerText();
    std::u16string r;
    for (int32_t i = 0; i < n; i++) { String* s = (*children)[i]->get_InnerText(); r.append(s->data, s->length); }
    return String::FromStd(r);
}
void XmlNode::set_InnerText(String* s)
{
    if (nodeType == XmlNodeType::Text) { value = s; return; }
    children->Clear();
    if (s && s->length) AppendChild(owner->CreateTextNode(s));
}
String* XmlNode::get_InnerXml()
{
    std::u16string r;
    for (int32_t i = 0; i < children->get_Count(); i++) WriteNode(r, (*children)[i], 0);
    return String::FromStd(r);
}
String* XmlNode::get_OuterXml() { std::u16string r; WriteNode(r, this, 0); return String::FromStd(r); }
XmlNodeList* XmlNode::get_ChildNodes()
{
    auto* l = gcnew<XmlNodeList>();
    l->items = children;
    return l;
}
XmlNode* XmlNode::get_FirstChild() { return children && children->get_Count() ? (*children)[0] : nullptr; }
XmlNode* XmlNode::get_LastChild() { return children && children->get_Count() ? (*children)[children->get_Count() - 1] : nullptr; }
XmlNode* XmlNode::get_NextSibling()
{
    if (!parent) return nullptr;
    int32_t i = parent->children->IndexOf(this);
    return i >= 0 && i + 1 < parent->children->get_Count() ? (*parent->children)[i + 1] : nullptr;
}
XmlElement* XmlNode::get_Item(String* n)
{
    if (!children) return nullptr;
    for (int32_t i = 0; i < children->get_Count(); i++)
    {
        auto* c = (*children)[i];
        if (c->nodeType == XmlNodeType::Element && String::op_Equality(c->name, n)) return static_cast<XmlElement*>(c);
    }
    return nullptr;
}
XmlNode* XmlNode::AppendChild(XmlNode* n)
{
    if (n->parent) n->parent->RemoveChild(n);
    n->parent = this;
    children->Add(n);
    return n;
}
XmlNode* XmlNode::RemoveChild(XmlNode* n) { children->Remove(n); n->parent = nullptr; return n; }
XmlNode* XmlNode::InsertBefore(XmlNode* n, XmlNode* ref)
{
    if (!ref) return AppendChild(n);
    if (n->parent) n->parent->RemoveChild(n);
    int32_t i = children->IndexOf(ref);
    n->parent = this;
    children->Insert(i < 0 ? children->get_Count() : i, n);
    return n;
}
void XmlNode::RemoveAll() { children->Clear(); if (attributes) attributes->items->Clear(); }
XmlNode* XmlNode::SelectSingleNode(String* path)
{
    auto* l = SelectNodes(path);
    return l->get_Count() ? l->get_Item(0) : nullptr;
}
XmlNodeList* XmlNode::SelectNodes(String* path)
{
    auto* parts = path->Split(Array<char16_t>::Of({ u'/' }), 1);
    std::vector<XmlNode*> cur{ this };
    for (int32_t i = 0; i < parts->length; i++)
    {
        std::vector<XmlNode*> next;
        for (auto* n : cur)
            for (int32_t j = 0; j < n->children->get_Count(); j++)
            {
                auto* c = (*n->children)[j];
                if (c->nodeType == XmlNodeType::Element && String::op_Equality(c->name, parts->data[i])) next.push_back(c);
            }
        cur.swap(next);
    }
    auto* l = gcnew<XmlNodeList>();
    for (auto* n : cur) l->items->Add(n);
    return l;
}
Collections::IEnumerator* XmlNode::GetEnumerator() { return gcnew<NodeEnum>(children); }
Collections::IEnumerator* XmlNodeList::GetEnumerator() { return gcnew<NodeEnum>(items); }
Collections::IEnumerator* XmlAttributeCollection::GetEnumerator() { return gcnew<AttrEnum>(items); }

XmlAttribute* XmlAttributeCollection::get_Item(String* n)
{
    for (int32_t i = 0; i < items->get_Count(); i++) if (String::op_Equality((*items)[i]->name, n)) return (*items)[i];
    return nullptr;
}
XmlAttribute* XmlAttributeCollection::Append(XmlAttribute* a)
{
    if (auto* ex = get_Item(a->name)) items->Remove(ex);
    a->parent = ownerNode;
    items->Add(a);
    return a;
}

XmlNodeList* XmlElement::GetElementsByTagName(String* n)
{
    auto* l = gcnew<XmlNodeList>();
    Collect(this, n, l);
    return l;
}
void XmlElement::SetAttribute(String* n, String* v)
{
    if (auto* a = attributes->get_Item(n)) { a->value = v; return; }
    auto* a = owner->CreateAttribute(n);
    a->value = v;
    attributes->Append(a);
}

XmlDocument::XmlDocument()
{
    nodeType = XmlNodeType::Document;
    name = TF_S(u"#document");
    owner = this;
    children = gcnew<Collections::Generic::List<XmlNode*>>();
}
void XmlDocument::Parse(const std::u16string& text)
{
    children->Clear();
    Parser ps(text, this);
    size_t s = 0;
    if (!text.empty() && text[0] == 0xFEFF) s = 1;
    ps.p = s;
    ps.ParseContent(this);
}
void XmlDocument::Load(String* path) { Parse(::sys::read_text_file(::sys::resolve_path(path))); }
void XmlDocument::Load(IO::Stream* s) { Parse(::sys::decode_text(s->ReadAllBytesNative())); }
void XmlDocument::LoadXml(String* xml) { Parse(xml->ToU16()); }
std::string XmlDocument::Serialize(bool declaration)
{
    std::u16string out;
    bool hasDecl = false;
    for (int32_t i = 0; i < children->get_Count(); i++) if ((*children)[i]->nodeType == XmlNodeType::XmlDeclaration) hasDecl = true;
    if (declaration && !hasDecl) out += u"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    for (int32_t i = 0; i < children->get_Count(); i++) WriteNode(out, (*children)[i], 0);
    return String::FromStd(out)->ToUtf8();
}
void XmlDocument::Save(String* path)
{
    std::string data = Serialize(false);
    FILE* f = fopen(::sys::resolve_path(path).c_str(), "wb");
    if (!f) ::sys::throw_msg("IOException", "XmlDocument.Save");
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}
void XmlDocument::Save(IO::Stream* s)
{
    std::string data = Serialize(false);
    auto* a = Array<uint8_t>::New((int32_t)data.size());
    memcpy(a->data, data.data(), data.size());
    s->Write(a, 0, a->length);
}
XmlElement* XmlDocument::CreateElement(String* n)
{
    auto* e = gcnew<XmlElement>();
    e->nodeType = XmlNodeType::Element;
    e->name = n;
    e->owner = this;
    e->children = gcnew<Collections::Generic::List<XmlNode*>>();
    e->attributes = gcnew<XmlAttributeCollection>();
    e->attributes->ownerNode = e;
    return e;
}
XmlAttribute* XmlDocument::CreateAttribute(String* n)
{
    auto* a = gcnew<XmlAttribute>();
    a->nodeType = XmlNodeType::Attribute;
    a->name = n;
    a->owner = this;
    a->value = String::Empty();
    a->children = gcnew<Collections::Generic::List<XmlNode*>>();
    return a;
}
XmlNode* XmlDocument::CreateTextNode(String* text)
{
    auto* n = gcnew<XmlNode>();
    n->nodeType = XmlNodeType::Text;
    n->name = TF_S(u"#text");
    n->value = text;
    n->owner = this;
    n->children = gcnew<Collections::Generic::List<XmlNode*>>();
    return n;
}
XmlNode* XmlDocument::CreateXmlDeclaration(String* v, String* enc, String* sa)
{
    auto* n = gcnew<XmlNode>();
    n->nodeType = XmlNodeType::XmlDeclaration;
    std::u16string d = u"version=\"" + (v ? v->ToU16() : u"1.0") + u"\"";
    if (enc && enc->length) d += u" encoding=\"" + enc->ToU16() + u"\"";
    if (sa && sa->length) d += u" standalone=\"" + sa->ToU16() + u"\"";
    n->value = String::FromStd(d);
    n->owner = this;
    n->children = gcnew<Collections::Generic::List<XmlNode*>>();
    return n;
}
XmlNode* XmlDocument::CreateComment(String* c)
{
    auto* n = gcnew<XmlNode>();
    n->nodeType = XmlNodeType::Comment;
    n->value = c;
    n->owner = this;
    n->children = gcnew<Collections::Generic::List<XmlNode*>>();
    return n;
}
XmlElement* XmlDocument::get_DocumentElement()
{
    for (int32_t i = 0; i < children->get_Count(); i++)
        if ((*children)[i]->nodeType == XmlNodeType::Element) return static_cast<XmlElement*>((*children)[i]);
    return nullptr;
}
XmlNodeList* XmlDocument::GetElementsByTagName(String* n)
{
    auto* l = gcnew<XmlNodeList>();
    Collect(this, n, l);
    return l;
}

// ------------------------------------------------------------------ XmlSerializer
namespace Serialization {

static std::u16string ItemName(Type* t)
{
    if (t->kind == TypeKind::String) return u"string";
    if (t->kind == TypeKind::Primitive)
    {
        if (t->tid == typeid(int32_t)) return u"int";
        if (t->tid == typeid(float)) return u"float";
        if (t->tid == typeid(double)) return u"double";
        if (t->tid == typeid(bool)) return u"boolean";
        if (t->tid == typeid(int64_t)) return u"long";
        if (t->tid == typeid(uint64_t)) return u"unsignedLong";
        if (t->tid == typeid(uint32_t)) return u"unsignedInt";
        if (t->tid == typeid(uint8_t)) return u"unsignedByte";
        if (t->tid == typeid(int16_t)) return u"short";
        if (t->tid == typeid(char16_t)) return u"char";
    }
    if (t->kind == TypeKind::Array || t->kind == TypeKind::List)
    {
        Type* e = t->elementType();
        std::u16string n = ItemName(e);
        if (!n.empty()) n[0] = Char::ToUpper(n[0]);
        return u"ArrayOf" + n;
    }
    return t->name->ToU16();
}

static String* FormatValue(Type* t, Object* v)
{
    if (t->kind == TypeKind::Primitive)
    {
        if (t->tid == typeid(bool)) return ::sys::unbox<bool>(v) ? TF_S(u"true") : TF_S(u"false");
        if (t->tid == typeid(float)) { char b[32]; snprintf(b, sizeof b, "%.9g", ::sys::unbox<float>(v)); return String::FromUtf8(b); }
        if (t->tid == typeid(double)) { char b[40]; snprintf(b, sizeof b, "%.17g", ::sys::unbox<double>(v)); return String::FromUtf8(b); }
        if (t->tid == typeid(char16_t)) return ::sys::ToString((int32_t)::sys::unbox<char16_t>(v));
    }
    return v->ToString();
}

static void WriteValue(XmlDocument* doc, XmlElement* el, Type* t, Object* v);

static void WriteMembers(XmlDocument* doc, XmlElement* el, Type* t, Object* obj)
{
    std::vector<Type*> chain;
    for (Type* c = t; c; c = c->baseType) chain.insert(chain.begin(), c);
    for (Type* c : chain)
    {
        for (auto* f : c->fields)
        {
            if (f->isStatic || f->isLiteral || !f->setter) continue;
            Object* v = f->getter(obj);
            if (!v) continue;
            auto* child = doc->CreateElement(f->name);
            WriteValue(doc, child, f->type, v);
            el->AppendChild(child);
        }
        for (auto* p : c->props)
        {
            Object* v = p->getter(obj);
            if (!v) continue;
            auto* child = doc->CreateElement(p->name);
            WriteValue(doc, child, p->type, v);
            el->AppendChild(child);
        }
    }
}

static void WriteValue(XmlDocument* doc, XmlElement* el, Type* t, Object* v)
{
    switch (t->kind)
    {
    case TypeKind::Primitive: case TypeKind::String: case TypeKind::Enum:
        el->set_InnerText(FormatValue(t, v));
        break;
    case TypeKind::Nullable:
        WriteValue(doc, el, t->elementType(), v);
        break;
    case TypeKind::Array: case TypeKind::List:
    {
        Type* e = t->elementType();
        int32_t n = t->count(v);
        for (int32_t i = 0; i < n; i++)
        {
            Object* item = t->arrayGet(v, i);
            auto* c = doc->CreateElement(String::FromStd(ItemName(e)));
            if (!item) c->SetAttribute(TF_S(u"xsi:nil"), TF_S(u"true"));
            else WriteValue(doc, c, e, item);
            el->AppendChild(c);
        }
        break;
    }
    case TypeKind::Class: case TypeKind::Struct:
    {
        Type* rt = ::sys::GetType(v);
        WriteMembers(doc, el, rt ? rt : t, v);
        break;
    }
    default: break;
    }
}

static Object* ReadValue(XmlElement* el, Type* t);

static void ReadMembers(XmlElement* el, Type* t, Object* obj)
{
    for (int32_t i = 0; i < el->children->get_Count(); i++)
    {
        auto* c = (*el->children)[i];
        if (c->nodeType != XmlNodeType::Element) continue;
        auto* ce = static_cast<XmlElement*>(c);
        bool done = false;
        for (Type* tt = t; tt && !done; tt = tt->baseType)
        {
            for (auto* f : tt->fields)
                if (!f->isStatic && !f->isLiteral && f->setter && String::op_Equality(f->name, ce->name))
                {
                    f->setter(obj, ReadValue(ce, f->type));
                    done = true; break;
                }
            if (done) break;
            for (auto* p : tt->props)
                if (String::op_Equality(p->name, ce->name))
                {
                    p->setter(obj, ReadValue(ce, p->type));
                    done = true; break;
                }
        }
    }
}

static Object* ReadValue(XmlElement* el, Type* t)
{
    auto* nil = el->attributes->get_Item(TF_S(u"xsi:nil"));
    if (nil && String::op_Equality(nil->value, TF_S(u"true"))) return nullptr;
    switch (t->kind)
    {
    case TypeKind::String: return el->get_InnerText();
    case TypeKind::Primitive: case TypeKind::Enum: return t->parse(el->get_InnerText());
    case TypeKind::Nullable: return ReadValue(el, t->elementType());
    case TypeKind::Array: case TypeKind::List:
    {
        Type* e = t->elementType();
        std::vector<Object*> items;
        for (int32_t i = 0; i < el->children->get_Count(); i++)
        {
            auto* c = (*el->children)[i];
            if (c->nodeType == XmlNodeType::Element) items.push_back(ReadValue(static_cast<XmlElement*>(c), e));
        }
        if (t->kind == TypeKind::Array)
        {
            Object* arr = t->newArray((int32_t)items.size());
            for (size_t i = 0; i < items.size(); i++) t->arraySet(arr, (int32_t)i, items[i]);
            return arr;
        }
        Object* list = t->factory();
        for (auto* it : items) t->listAdd(list, it);
        return list;
    }
    case TypeKind::Class: case TypeKind::Struct:
    {
        if (!t->factory) ::sys::throw_msg("InvalidOperationException", "type has no default constructor");
        Object* obj = t->factory();
        if (t->kind == TypeKind::Class) ReadMembers(el, t, obj);
        return obj;
    }
    default: return nullptr;
    }
}

void XmlSerializer::Serialize(IO::Stream* s, Object* o)
{
    auto* doc = gcnew<XmlDocument>();
    doc->AppendChild(doc->CreateXmlDeclaration(TF_S(u"1.0"), nullptr, nullptr));
    auto* root = doc->CreateElement(String::FromStd(ItemName(type)));
    root->SetAttribute(TF_S(u"xmlns:xsi"), TF_S(u"http://www.w3.org/2001/XMLSchema-instance"));
    root->SetAttribute(TF_S(u"xmlns:xsd"), TF_S(u"http://www.w3.org/2001/XMLSchema"));
    doc->AppendChild(root);
    WriteValue(doc, root, type, o);
    doc->Save(s);
}
Object* XmlSerializer::Deserialize(IO::Stream* s)
{
    auto* doc = gcnew<XmlDocument>();
    doc->Load(s);
    auto* root = doc->get_DocumentElement();
    if (!root) ::sys::throw_msg("InvalidOperationException", "There is an error in XML document (empty).");
    return ReadValue(root, type);
}

} // namespace Serialization

}} // namespace System::Xml

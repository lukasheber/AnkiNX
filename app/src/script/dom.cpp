#include "script/dom.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <gumbo.h>

namespace dom
{

// ---------------------------------------------------------------- nodes ----

const std::string* Node::attr(const std::string& name) const
{
    for (const auto& [k, v] : attrs)
        if (k == name)
            return &v;
    return nullptr;
}

void Node::setAttr(const std::string& name, const std::string& value)
{
    for (auto& [k, v] : attrs)
        if (k == name)
        {
            v = value;
            return;
        }
    attrs.emplace_back(name, value);
}

void Node::removeAttr(const std::string& name)
{
    attrs.erase(std::remove_if(attrs.begin(), attrs.end(), [&](const auto& a) { return a.first == name; }),
                attrs.end());
}

int Node::indexInParent() const
{
    NodePtr p = parent.lock();
    if (!p)
        return -1;
    for (size_t i = 0; i < p->children.size(); i++)
        if (p->children[i].get() == this)
            return (int)i;
    return -1;
}

namespace
{

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string tagName(const GumboElement& element)
{
    if (element.tag != GUMBO_TAG_UNKNOWN)
        return gumbo_normalized_tagname(element.tag);
    GumboStringPiece piece = element.original_tag;
    gumbo_tag_from_original_text(&piece);
    return lower(std::string(piece.data, piece.length));
}

NodePtr convert(const GumboNode* source)
{
    auto node = std::make_shared<Node>();
    switch (source->type)
    {
        case GUMBO_NODE_ELEMENT:
        case GUMBO_NODE_TEMPLATE:
        {
            const GumboElement& element = source->v.element;
            node->type                  = Node::Type::Element;
            node->tag                   = tagName(element);
            for (unsigned i = 0; i < element.attributes.length; i++)
            {
                auto* a = (const GumboAttribute*)element.attributes.data[i];
                node->attrs.emplace_back(lower(a->name), a->value);
            }
            for (unsigned i = 0; i < element.children.length; i++)
                if (NodePtr child = convert((const GumboNode*)element.children.data[i]))
                {
                    child->parent = node;
                    node->children.push_back(child);
                }
            return node;
        }
        case GUMBO_NODE_TEXT:
        case GUMBO_NODE_WHITESPACE:
        case GUMBO_NODE_CDATA:
            node->type = Node::Type::Text;
            node->text = source->v.text.text;
            return node;
        case GUMBO_NODE_COMMENT:
            node->type = Node::Type::Comment;
            node->text = source->v.text.text;
            return node;
        default:
            return nullptr;
    }
}

// A card side is a fragment: gumbo builds <html><head><body>, and moves a
// leading <style> or <script> into <head>. Take both, in order.
std::vector<NodePtr> parseNodes(const std::string& html)
{
    std::vector<NodePtr> out;
    GumboOutput* output = gumbo_parse_with_options(&kGumboDefaultOptions, html.data(), html.size());
    const GumboElement& root = output->root->v.element;
    for (unsigned i = 0; i < root.children.length; i++)
    {
        auto* section = (const GumboNode*)root.children.data[i];
        if (section->type != GUMBO_NODE_ELEMENT)
            continue;
        for (unsigned j = 0; j < section->v.element.children.length; j++)
            if (NodePtr node = convert((const GumboNode*)section->v.element.children.data[j]))
                out.push_back(node);
    }
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return out;
}

void forEach(const NodePtr& node, const std::function<bool(const NodePtr&)>& visit)
{
    for (const NodePtr& child : node->children)
    {
        if (!visit(child))
            return;
        forEach(child, visit);
    }
}

} // namespace

Document::Document(const std::string& html)
{
    rootNode       = std::make_shared<Node>();
    rootNode->type = Node::Type::Document;
    for (const NodePtr& node : parseNodes(html))
        appendChild(rootNode, node);
    adopt(rootNode);
}

NodePtr Document::createElement(const std::string& tag)
{
    auto node  = std::make_shared<Node>();
    node->type = Node::Type::Element;
    node->tag  = lower(tag);
    adopt(node);
    return node;
}

NodePtr Document::createText(const std::string& text)
{
    auto node  = std::make_shared<Node>();
    node->type = Node::Type::Text;
    node->text = text;
    adopt(node);
    return node;
}

std::vector<NodePtr> Document::parseFragment(const std::string& html)
{
    std::vector<NodePtr> nodes = parseNodes(html);
    for (const NodePtr& node : nodes)
        adopt(node);
    return nodes;
}

void Document::adopt(const NodePtr& node)
{
    if (node->serial == 0)
        node->serial = nextSerial++;
    for (const NodePtr& child : node->children)
        adopt(child);
}

NodePtr Document::bySerial(int serial) const
{
    NodePtr found;
    forEach(rootNode, [&](const NodePtr& n) {
        if (n->serial == serial)
            found = n;
        return !found;
    });
    return found;
}

NodePtr Document::byId(const std::string& id) const
{
    NodePtr found;
    forEach(rootNode, [&](const NodePtr& n) {
        if (n->isElement())
            if (const std::string* v = n->attr("id"); v && *v == id)
                found = n;
        return !found;
    });
    return found;
}

// ------------------------------------------------------------- editing ----

void removeFromParent(const NodePtr& node)
{
    if (NodePtr p = node->parent.lock())
    {
        auto& siblings = p->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
    }
    node->parent.reset();
}

void insertBefore(const NodePtr& parent, const NodePtr& child, const NodePtr& reference)
{
    if (child->type == Node::Type::Document)
    {
        // A fragment's children move (DocumentFragment semantics).
        std::vector<NodePtr> moved = child->children;
        for (const NodePtr& c : moved)
            insertBefore(parent, c, reference);
        return;
    }
    removeFromParent(child);
    auto& siblings = parent->children;
    auto at        = reference ? std::find(siblings.begin(), siblings.end(), reference) : siblings.end();
    siblings.insert(at, child);
    child->parent = parent;
}

void appendChild(const NodePtr& parent, const NodePtr& child) { insertBefore(parent, child, nullptr); }

void removeChild(const NodePtr& parent, const NodePtr& child)
{
    if (child->parent.lock() == parent)
        removeFromParent(child);
}

NodePtr cloneNode(const NodePtr& node, bool deep)
{
    auto copy      = std::make_shared<Node>();
    copy->type     = node->type;
    copy->tag      = node->tag;
    copy->attrs    = node->attrs;
    copy->text     = node->text;
    if (deep)
        for (const NodePtr& child : node->children)
            appendChild(copy, cloneNode(child, true));
    return copy;
}

std::string textContent(const NodePtr& node)
{
    if (node->type == Node::Type::Text)
        return node->text;
    if (node->type == Node::Type::Comment)
        return "";
    std::string out;
    for (const NodePtr& child : node->children)
        out += textContent(child);
    return out;
}

void setTextContent(Document& doc, const NodePtr& node, const std::string& text)
{
    if (node->type == Node::Type::Text || node->type == Node::Type::Comment)
    {
        node->text = text;
        return;
    }
    std::vector<NodePtr> old = node->children;
    for (const NodePtr& child : old)
        removeFromParent(child);
    if (!text.empty())
        appendChild(node, doc.createText(text));
}

// --------------------------------------------------------------- output ----

namespace
{

bool isVoid(const std::string& tag)
{
    static const char* VOID[] = { "area", "base", "br", "col", "embed", "hr", "img", "input",
                                  "link", "meta", "param", "source", "track", "wbr" };
    return std::find_if(std::begin(VOID), std::end(VOID), [&](const char* t) { return tag == t; }) != std::end(VOID);
}

bool isRawText(const std::string& tag) { return tag == "script" || tag == "style"; }

std::string escape(const std::string& s, bool attribute)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += attribute ? "<" : "&lt;"; break;
            case '>': out += attribute ? ">" : "&gt;"; break;
            case '"': out += attribute ? "&quot;" : "\""; break;
            case '\xa0': out += "&nbsp;"; break;
            default: out += c;
        }
    }
    return out;
}

void write(std::string& out, const NodePtr& node, bool serials, bool rawParent)
{
    switch (node->type)
    {
        case Node::Type::Text:
            out += rawParent ? node->text : escape(node->text, false);
            return;
        case Node::Type::Comment:
            out += "<!--" + node->text + "-->";
            return;
        case Node::Type::Document:
            for (const NodePtr& child : node->children)
                write(out, child, serials, false);
            return;
        case Node::Type::Element:
            out += "<" + node->tag;
            for (const auto& [k, v] : node->attrs)
                out += " " + k + "=\"" + escape(v, true) + "\"";
            if (serials)
                out += " data-ankinx-node=\"" + std::to_string(node->serial) + "\"";
            out += ">";
            if (isVoid(node->tag))
                return;
            for (const NodePtr& child : node->children)
                write(out, child, serials, isRawText(node->tag));
            out += "</" + node->tag + ">";
            return;
    }
}

} // namespace

std::string innerHtml(const NodePtr& node, bool serials)
{
    std::string out;
    bool raw = node->isElement() && isRawText(node->tag);
    for (const NodePtr& child : node->children)
        write(out, child, serials, raw);
    return out;
}

std::string outerHtml(const NodePtr& node, bool serials)
{
    std::string out;
    write(out, node, serials, false);
    return out;
}

// ------------------------------------------------------------ selectors ----

namespace
{

struct Simple
{
    enum class Kind
    {
        Type,
        Universal,
        Id,
        Class,
        Attr,
        FirstChild,
        LastChild,
        Not,
        Invalid,
    } kind;
    std::string name, op, value;
    std::vector<Simple> inner; // :not(...)
};

struct Compound
{
    std::vector<Simple> parts;
    char combinator = 0; // to the previous compound: ' ', '>', '+', '~'
};

using Complex = std::vector<Compound>;

struct Parser
{
    const std::string& s;
    size_t i = 0;

    bool done() const { return i >= s.size(); }
    char peek() const { return done() ? 0 : s[i]; }
    void spaces()
    {
        while (!done() && std::isspace((unsigned char)s[i]))
            i++;
    }
    std::string ident()
    {
        size_t start = i;
        while (!done() && (std::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_' || (unsigned char)s[i] >= 0x80))
            i++;
        return s.substr(start, i - start);
    }

    bool simple(Simple& out)
    {
        char c = peek();
        if (c == '#')
        {
            i++;
            out = { Simple::Kind::Id, ident() };
            return !out.name.empty();
        }
        if (c == '.')
        {
            i++;
            out = { Simple::Kind::Class, ident() };
            return !out.name.empty();
        }
        if (c == '*')
        {
            i++;
            out = { Simple::Kind::Universal };
            return true;
        }
        if (c == '[')
        {
            i++;
            spaces();
            out      = { Simple::Kind::Attr, lower(ident()) };
            spaces();
            if (peek() != ']')
            {
                if (std::string("~^$*|").find(peek()) != std::string::npos)
                    out.op += s[i++];
                if (peek() != '=')
                    return false;
                out.op += s[i++];
                spaces();
                char quote = peek();
                if (quote == '"' || quote == '\'')
                {
                    i++;
                    size_t end = s.find(quote, i);
                    if (end == std::string::npos)
                        return false;
                    out.value = s.substr(i, end - i);
                    i         = end + 1;
                }
                else
                    out.value = ident();
                spaces();
            }
            if (peek() != ']')
                return false;
            i++;
            return true;
        }
        if (c == ':')
        {
            i++;
            std::string name = lower(ident());
            if (name == "first-child")
                out = { Simple::Kind::FirstChild };
            else if (name == "last-child")
                out = { Simple::Kind::LastChild };
            else if (name == "not" && peek() == '(')
            {
                i++;
                spaces();
                out = { Simple::Kind::Not };
                Simple inner;
                while (peek() != ')' && simple(inner))
                    out.inner.push_back(inner);
                if (peek() != ')')
                    return false;
                i++;
            }
            else
                return false;
            return true;
        }
        if (std::isalpha((unsigned char)c))
        {
            out = { Simple::Kind::Type, lower(ident()) };
            return true;
        }
        return false;
    }

    bool compound(Compound& out)
    {
        Simple part;
        while (!done() && std::string(" ,>+~").find(peek()) == std::string::npos)
        {
            if (!simple(part))
                return false;
            out.parts.push_back(part);
        }
        return !out.parts.empty();
    }

    bool complex(Complex& out)
    {
        spaces();
        Compound first;
        if (!compound(first))
            return false;
        out.push_back(first);
        while (true)
        {
            size_t before = i;
            spaces();
            char comb = ' ';
            if (peek() == '>' || peek() == '+' || peek() == '~')
            {
                comb = s[i++];
                spaces();
            }
            else if (i == before || done() || peek() == ',')
                return true;
            Compound next;
            next.combinator = comb;
            if (!compound(next))
                return false;
            out.push_back(next);
        }
    }
};

std::vector<Complex> parseSelector(const std::string& selector)
{
    std::vector<Complex> list;
    Parser p { selector };
    while (true)
    {
        Complex c;
        if (!p.complex(c))
            return {};
        list.push_back(c);
        p.spaces();
        if (p.done())
            return list;
        if (p.peek() != ',')
            return {};
        p.i++;
    }
}

bool hasWord(const std::string& list, const std::string& word)
{
    size_t pos = 0;
    while (pos < list.size())
    {
        while (pos < list.size() && std::isspace((unsigned char)list[pos]))
            pos++;
        size_t end = pos;
        while (end < list.size() && !std::isspace((unsigned char)list[end]))
            end++;
        if (list.compare(pos, end - pos, word) == 0 && end - pos == word.size())
            return true;
        pos = end;
    }
    return false;
}

NodePtr elementSibling(const NodePtr& node, int direction)
{
    NodePtr p = node->parent.lock();
    if (!p)
        return nullptr;
    int i = node->indexInParent();
    for (i += direction; i >= 0 && i < (int)p->children.size(); i += direction)
        if (p->children[i]->isElement())
            return p->children[i];
    return nullptr;
}

bool matchSimple(const NodePtr& el, const Simple& s)
{
    switch (s.kind)
    {
        case Simple::Kind::Universal: return true;
        case Simple::Kind::Type: return el->tag == s.name;
        case Simple::Kind::Id:
        {
            const std::string* v = el->attr("id");
            return v && *v == s.name;
        }
        case Simple::Kind::Class:
        {
            const std::string* v = el->attr("class");
            return v && hasWord(*v, s.name);
        }
        case Simple::Kind::Attr:
        {
            const std::string* v = el->attr(s.name);
            if (!v)
                return false;
            if (s.op.empty())
                return true;
            if (s.op == "=")
                return *v == s.value;
            if (s.op == "~=")
                return hasWord(*v, s.value);
            if (s.op == "^=")
                return !s.value.empty() && v->rfind(s.value, 0) == 0;
            if (s.op == "$=")
                return !s.value.empty() && v->size() >= s.value.size() &&
                       v->compare(v->size() - s.value.size(), s.value.size(), s.value) == 0;
            if (s.op == "*=")
                return !s.value.empty() && v->find(s.value) != std::string::npos;
            if (s.op == "|=")
                return *v == s.value || v->rfind(s.value + "-", 0) == 0;
            return false;
        }
        case Simple::Kind::FirstChild: return !elementSibling(el, -1);
        case Simple::Kind::LastChild: return !elementSibling(el, 1);
        case Simple::Kind::Not:
            for (const Simple& inner : s.inner)
                if (!matchSimple(el, inner))
                    return true;
            return false;
        default: return false;
    }
}

bool matchCompound(const NodePtr& el, const Compound& c)
{
    if (!el->isElement())
        return false;
    for (const Simple& s : c.parts)
        if (!matchSimple(el, s))
            return false;
    return true;
}

// Does `el` match compounds [0..index] (right to left)?
bool matchFrom(const NodePtr& el, const Complex& c, int index)
{
    if (!matchCompound(el, c[index]))
        return false;
    if (index == 0)
        return true;
    char comb = c[index].combinator;
    if (comb == '>')
    {
        NodePtr p = el->parent.lock();
        return p && matchFrom(p, c, index - 1);
    }
    if (comb == ' ')
    {
        for (NodePtr p = el->parent.lock(); p; p = p->parent.lock())
            if (matchFrom(p, c, index - 1))
                return true;
        return false;
    }
    if (comb == '+')
    {
        NodePtr prev = elementSibling(el, -1);
        return prev && matchFrom(prev, c, index - 1);
    }
    for (NodePtr prev = elementSibling(el, -1); prev; prev = elementSibling(prev, -1))
        if (matchFrom(prev, c, index - 1))
            return true;
    return false;
}

bool matchList(const NodePtr& el, const std::vector<Complex>& list)
{
    for (const Complex& c : list)
        if (matchFrom(el, c, (int)c.size() - 1))
            return true;
    return false;
}

} // namespace

bool matches(const NodePtr& element, const std::string& selector)
{
    return element->isElement() && matchList(element, parseSelector(selector));
}

NodePtr querySelector(const NodePtr& scope, const std::string& selector)
{
    std::vector<Complex> list = parseSelector(selector);
    NodePtr found;
    if (list.empty())
        return found;
    forEach(scope, [&](const NodePtr& n) {
        if (matchList(n, list))
            found = n;
        return !found;
    });
    return found;
}

std::vector<NodePtr> querySelectorAll(const NodePtr& scope, const std::string& selector)
{
    std::vector<Complex> list = parseSelector(selector);
    std::vector<NodePtr> out;
    if (list.empty())
        return out;
    forEach(scope, [&](const NodePtr& n) {
        if (matchList(n, list))
            out.push_back(n);
        return true;
    });
    return out;
}

} // namespace dom

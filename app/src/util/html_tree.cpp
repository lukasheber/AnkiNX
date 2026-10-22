#include "util/html_tree.hpp"

#include <algorithm>
#include <cctype>
#include <gumbo.h>

namespace html
{

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
    // SVG names are case-sensitive: linearGradient, clipPath, foreignObject.
    if (element.tag_namespace == GUMBO_NAMESPACE_SVG)
        if (const char* svg = gumbo_normalize_svg_tagname(&piece))
            return svg;
    return lower(std::string(piece.data, piece.length));
}

// HTML attribute names are lowercased; SVG ones keep gumbo's adjusted case
// (viewBox) and their namespace prefix (xlink:href, xml:space).
std::string attributeName(const GumboElement& element, const GumboAttribute& a)
{
    switch (a.attr_namespace)
    {
        case GUMBO_ATTR_NAMESPACE_XLINK: return std::string("xlink:") + a.name;
        case GUMBO_ATTR_NAMESPACE_XML: return std::string("xml:") + a.name;
        case GUMBO_ATTR_NAMESPACE_XMLNS:
            return std::string(a.name) == "xmlns" ? "xmlns" : std::string("xmlns:") + a.name;
        default: break;
    }
    return element.tag_namespace == GUMBO_NAMESPACE_HTML ? lower(a.name) : std::string(a.name);
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
                node->attrs.emplace_back(attributeName(element, *a), a->value);
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
    GumboOutput* output      = gumbo_parse_with_options(&kGumboDefaultOptions, html.data(), html.size());
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
    for (size_t i = 0; i < s.size(); i++)
    {
        char c = s[i];
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += attribute ? "<" : "&lt;"; break;
            case '>': out += attribute ? ">" : "&gt;"; break;
            case '"': out += attribute ? "&quot;" : "\""; break;
            case '\xc2': // U+00A0 is C2 A0 in UTF-8; A0 alone continues other characters
                if (i + 1 < s.size() && s[i + 1] == '\xa0')
                {
                    out += "&nbsp;";
                    i++;
                    break;
                }
                out += c;
                break;
            default: out += c;
        }
    }
    return out;
}

void write(std::string& out, const NodePtr& node, bool rawParent)
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
                write(out, child, false);
            return;
        case Node::Type::Element:
            out += "<" + node->tag;
            for (const auto& [k, v] : node->attrs)
                out += " " + k + "=\"" + escape(v, true) + "\"";
            out += ">";
            if (isVoid(node->tag))
                return;
            for (const NodePtr& child : node->children)
                write(out, child, isRawText(node->tag));
            out += "</" + node->tag + ">";
            return;
    }
}

} // namespace

Document::Document(const std::string& html)
{
    rootNode       = std::make_shared<Node>();
    rootNode->type = Node::Type::Document;
    for (const NodePtr& node : parseNodes(html))
        appendChild(rootNode, node);
}

NodePtr Document::createElement(const std::string& tag)
{
    auto node  = std::make_shared<Node>();
    node->type = Node::Type::Element;
    node->tag  = lower(tag);
    return node;
}

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
    removeFromParent(child);
    auto& siblings = parent->children;
    auto at        = reference ? std::find(siblings.begin(), siblings.end(), reference) : siblings.end();
    siblings.insert(at, child);
    child->parent = parent;
}

void appendChild(const NodePtr& parent, const NodePtr& child) { insertBefore(parent, child, nullptr); }

std::string innerHtml(const NodePtr& node)
{
    std::string out;
    bool raw = node->isElement() && isRawText(node->tag);
    for (const NodePtr& child : node->children)
        write(out, child, raw);
    return out;
}

} // namespace html

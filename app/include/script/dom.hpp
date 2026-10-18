#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

// A small, mutable HTML tree for card scripts (script/card_script.cpp): built
// from the card's HTML with gumbo (litehtml's HTML5 parser), changed by
// JavaScript, and written back as HTML for litehtml to lay out again.
namespace dom
{

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node : std::enable_shared_from_this<Node>
{
    enum class Type
    {
        Document,
        Element,
        Text,
        Comment,
    };

    Type type = Type::Element;
    std::string tag;                                         // lower case, elements only
    std::vector<std::pair<std::string, std::string>> attrs; // in document order
    std::string text;                                        // text and comments
    std::weak_ptr<Node> parent;
    std::vector<NodePtr> children;
    // Stable id for this document, written as data-ankinx-node so taps on
    // litehtml's layout can be traced back here.
    int serial = 0;

    bool isElement() const { return type == Type::Element; }
    const std::string* attr(const std::string& name) const;
    void setAttr(const std::string& name, const std::string& value);
    void removeAttr(const std::string& name);
    NodePtr parentNode() const { return parent.lock(); }
    int indexInParent() const;
};

class Document
{
  public:
    // Parse an HTML fragment (a card side) into a fresh tree.
    explicit Document(const std::string& html);

    NodePtr root() const { return rootNode; }
    NodePtr createElement(const std::string& tag);
    NodePtr createText(const std::string& text);
    // Parse `html` into new nodes (innerHTML).
    std::vector<NodePtr> parseFragment(const std::string& html);
    // Give new nodes serials (after parsing or creation).
    void adopt(const NodePtr& node);
    NodePtr bySerial(int serial) const;
    NodePtr byId(const std::string& id) const;

    // Changed since the last html() call.
    bool dirty = false;

  private:
    NodePtr rootNode;
    int nextSerial = 1;
};

// Tree editing; all set the document dirty through the caller.
void appendChild(const NodePtr& parent, const NodePtr& child);
void insertBefore(const NodePtr& parent, const NodePtr& child, const NodePtr& reference);
void removeChild(const NodePtr& parent, const NodePtr& child);
void removeFromParent(const NodePtr& node);
NodePtr cloneNode(const NodePtr& node, bool deep);

std::string textContent(const NodePtr& node);
void setTextContent(Document& doc, const NodePtr& node, const std::string& text);

// HTML for the children of `node` (innerHTML) or the node itself
// (outerHTML). With `serials`, elements carry data-ankinx-node.
std::string innerHtml(const NodePtr& node, bool serials = false);
std::string outerHtml(const NodePtr& node, bool serials = false);

// CSS selectors: type, #id, .class, [attr], [attr=v], [attr~=v], [attr^=v],
// [attr$=v], [attr*=v], *, :first-child, :last-child, :not(simple), and the
// descendant, >, + and ~ combinators, in comma lists. Unsupported syntax
// matches nothing.
bool matches(const NodePtr& element, const std::string& selector);
NodePtr querySelector(const NodePtr& scope, const std::string& selector);
std::vector<NodePtr> querySelectorAll(const NodePtr& scope, const std::string& selector);

} // namespace dom

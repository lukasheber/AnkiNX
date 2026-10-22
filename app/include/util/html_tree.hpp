#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

// A small, editable HTML tree for rewriting card HTML before litehtml lays it
// out (util/ruby): parsed with gumbo (litehtml's HTML5 parser), changed in
// C++, and written back as HTML. Card scripts have a full DOM of their own
// (resources/script/runtime.js).
namespace html
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

    bool isElement() const { return type == Type::Element; }
    const std::string* attr(const std::string& name) const;
    void setAttr(const std::string& name, const std::string& value);
    NodePtr parentNode() const { return parent.lock(); }
};

class Document
{
  public:
    // Parse an HTML fragment (a card side) into a fresh tree.
    explicit Document(const std::string& html);

    NodePtr root() const { return rootNode; }
    NodePtr createElement(const std::string& tag);

  private:
    NodePtr rootNode;
};

void appendChild(const NodePtr& parent, const NodePtr& child);
void insertBefore(const NodePtr& parent, const NodePtr& child, const NodePtr& reference);
void removeFromParent(const NodePtr& node);

// HTML for the children of `node`.
std::string innerHtml(const NodePtr& node);

} // namespace html

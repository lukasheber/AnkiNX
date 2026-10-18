#include "util/ruby.hpp"

#include <functional>
#include <utility>
#include <vector>

#include "script/dom.hpp"

namespace
{

bool hasRuby(const std::string& html)
{
    for (size_t at = html.find('<'); at != std::string::npos; at = html.find('<', at + 1))
        if (at + 5 <= html.size() && (html[at + 1] | 0x20) == 'r' && (html[at + 2] | 0x20) == 'u' &&
            (html[at + 3] | 0x20) == 'b' && (html[at + 4] | 0x20) == 'y')
            return true;
    return false;
}

using Nodes = std::vector<dom::NodePtr>;

void rewrite(dom::Document& doc, const dom::NodePtr& ruby)
{
    // Base/reading pairs, in order; <rb> is unwrapped, <rp> dropped.
    std::vector<std::pair<Nodes, Nodes>> pairs;
    Nodes base;
    for (const dom::NodePtr& child : Nodes(ruby->children))
    {
        const std::string& tag = child->isElement() ? child->tag : std::string();
        if (tag == "rt" || tag == "rtc")
        {
            pairs.emplace_back(std::move(base), Nodes(child->children));
            base.clear();
        }
        else if (tag == "rb")
            base.insert(base.end(), child->children.begin(), child->children.end());
        else if (tag != "rp")
            base.push_back(child);
    }
    if (!base.empty())
        pairs.emplace_back(std::move(base), Nodes());

    dom::NodePtr parent = ruby->parentNode();
    if (!parent)
        return;
    for (auto& [baseNodes, reading] : pairs)
    {
        dom::NodePtr box = doc.createElement("ruby");
        box->attrs       = ruby->attrs; // keeps the deck's classes and styles
        std::string cls  = box->attr("class") ? *box->attr("class") + " ankinx-ruby" : "ankinx-ruby";
        box->setAttr("class", cls);
        dom::NodePtr rt = doc.createElement("rt");
        dom::NodePtr rb = doc.createElement("rb");
        for (const dom::NodePtr& n : reading)
        {
            dom::removeFromParent(n);
            dom::appendChild(rt, n);
        }
        for (const dom::NodePtr& n : baseNodes)
        {
            dom::removeFromParent(n);
            dom::appendChild(rb, n);
        }
        dom::appendChild(box, rt);
        dom::appendChild(box, rb);
        dom::insertBefore(parent, box, ruby);
    }
    dom::removeFromParent(ruby);
}

} // namespace

std::string layoutRuby(const std::string& html)
{
    if (!hasRuby(html))
        return html;
    dom::Document doc(html);
    std::vector<dom::NodePtr> rubies;
    std::function<void(const dom::NodePtr&)> find = [&](const dom::NodePtr& node) {
        for (const dom::NodePtr& child : node->children)
        {
            if (child->isElement() && child->tag == "ruby")
                rubies.push_back(child);
            else
                find(child);
        }
    };
    find(doc.root());
    for (const dom::NodePtr& ruby : rubies)
        rewrite(doc, ruby);
    return dom::innerHtml(doc.root());
}

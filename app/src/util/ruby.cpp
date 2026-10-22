#include "util/ruby.hpp"

#include <functional>
#include <utility>
#include <vector>

#include "util/html_tree.hpp"

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

using Nodes = std::vector<html::NodePtr>;

void rewrite(html::Document& doc, const html::NodePtr& ruby)
{
    // Base/reading pairs, in order; <rb> is unwrapped, <rp> dropped.
    std::vector<std::pair<Nodes, Nodes>> pairs;
    Nodes base;
    for (const html::NodePtr& child : Nodes(ruby->children))
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

    html::NodePtr parent = ruby->parentNode();
    if (!parent)
        return;
    for (auto& [baseNodes, reading] : pairs)
    {
        html::NodePtr box = doc.createElement("ruby");
        box->attrs       = ruby->attrs; // keeps the deck's classes and styles
        std::string cls  = box->attr("class") ? *box->attr("class") + " ankinx-ruby" : "ankinx-ruby";
        box->setAttr("class", cls);
        html::NodePtr rt = doc.createElement("rt");
        html::NodePtr rb = doc.createElement("rb");
        for (const html::NodePtr& n : reading)
        {
            html::removeFromParent(n);
            html::appendChild(rt, n);
        }
        for (const html::NodePtr& n : baseNodes)
        {
            html::removeFromParent(n);
            html::appendChild(rb, n);
        }
        html::appendChild(box, rt);
        html::appendChild(box, rb);
        html::insertBefore(parent, box, ruby);
    }
    html::removeFromParent(ruby);
}

} // namespace

std::string layoutRuby(const std::string& html)
{
    if (!hasRuby(html))
        return html;
    html::Document doc(html);
    std::vector<html::NodePtr> rubies;
    std::function<void(const html::NodePtr&)> find = [&](const html::NodePtr& node) {
        for (const html::NodePtr& child : node->children)
        {
            if (child->isElement() && child->tag == "ruby")
                rubies.push_back(child);
            else
                find(child);
        }
    };
    find(doc.root());
    for (const html::NodePtr& ruby : rubies)
        rewrite(doc, ruby);
    return html::innerHtml(doc.root());
}

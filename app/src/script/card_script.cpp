#include "script/card_script.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <quickjs.h>

using Clock = std::chrono::steady_clock;

namespace
{

// Budget for one entry into scripts (a side shown, a tap, a timer).
constexpr auto TIME_BUDGET = std::chrono::milliseconds(500);
constexpr size_t MEMORY_LIMIT = 32u << 20;
// Within the calling thread's stack (borealis' main thread).
constexpr size_t STACK_LIMIT = 256u << 10;

JSClassID nodeClass = 0;

// localStorage / sessionStorage: kept for the app session.
std::map<std::string, std::string>& storage(const std::string& kind)
{
    static std::map<std::string, std::map<std::string, std::string>> all;
    return all[kind];
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

} // namespace

std::function<void(int, const std::string&)> CardScript::log = [](int level, const std::string& msg) {
    std::fprintf(stderr, "card script [%d]: %s\n", level, msg.c_str());
};

struct CardScript::Native
{
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    std::unique_ptr<dom::Document> doc;
    std::string mediaDir;
    // One JS object per node, so === works; the nodes stay alive with them.
    std::unordered_map<dom::Node*, JSValue> objects;
    std::vector<dom::NodePtr> keep;
    JSValue n     = JS_UNDEFINED;
    JSValue proto = JS_UNDEFINED;
    std::vector<std::pair<int, int>> timers;
    std::vector<std::string> commands;
    Clock::time_point deadline;

    JSValue wrap(const dom::NodePtr& node)
    {
        if (!node)
            return JS_NULL;
        auto it = objects.find(node.get());
        if (it != objects.end())
            return JS_DupValue(ctx, it->second);
        JSValue obj = JS_NewObjectProtoClass(ctx, proto, nodeClass);
        JS_SetOpaque(obj, node.get());
        objects[node.get()] = JS_DupValue(ctx, obj);
        keep.push_back(node);
        return obj;
    }

    dom::NodePtr unwrap(JSValueConst value)
    {
        auto* raw = (dom::Node*)JS_GetOpaque(value, nodeClass);
        return raw ? raw->shared_from_this() : nullptr;
    }

    JSValue array(const std::vector<dom::NodePtr>& nodes)
    {
        JSValue list = JS_NewArray(ctx);
        for (size_t i = 0; i < nodes.size(); i++)
            JS_SetPropertyUint32(ctx, list, (uint32_t)i, wrap(nodes[i]));
        return list;
    }

    void changed() { doc->dirty = true; }
};

namespace
{

CardScript::Native& self(JSContext* ctx) { return *(CardScript::Native*)JS_GetContextOpaque(ctx); }

std::string str(JSContext* ctx, JSValueConst value)
{
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, value);
    std::string out = s ? std::string(s, len) : std::string();
    JS_FreeCString(ctx, s);
    return out;
}

std::string argStr(JSContext* ctx, int argc, JSValueConst* argv, int i)
{
    return i < argc ? str(ctx, argv[i]) : std::string();
}

dom::NodePtr argNode(JSContext* ctx, int argc, JSValueConst* argv, int i)
{
    return i < argc ? self(ctx).unwrap(argv[i]) : nullptr;
}

#define NATIVE(name) JSValue name(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
#define NODE(var, i)                                    \
    dom::NodePtr var = argNode(ctx, argc, argv, i);     \
    if (!var)                                           \
        return JS_ThrowTypeError(ctx, "not a DOM node")

NATIVE(nNodeType)
{
    NODE(node, 0);
    switch (node->type)
    {
        case dom::Node::Type::Element: return JS_NewInt32(ctx, 1);
        case dom::Node::Type::Text: return JS_NewInt32(ctx, 3);
        case dom::Node::Type::Comment: return JS_NewInt32(ctx, 8);
        default: return JS_NewInt32(ctx, 11);
    }
}

NATIVE(nTagName)
{
    NODE(node, 0);
    return JS_NewString(ctx, node->tag.c_str());
}

NATIVE(nParent)
{
    NODE(node, 0);
    return self(ctx).wrap(node->parentNode());
}

NATIVE(nChildren)
{
    NODE(node, 0);
    return self(ctx).array(node->children);
}

NATIVE(nText)
{
    NODE(node, 0);
    return JS_NewString(ctx, dom::textContent(node).c_str());
}

NATIVE(nSetText)
{
    NODE(node, 0);
    dom::setTextContent(*self(ctx).doc, node, argStr(ctx, argc, argv, 1));
    self(ctx).changed();
    return JS_UNDEFINED;
}

NATIVE(nHtml)
{
    NODE(node, 0);
    return JS_NewString(ctx, dom::innerHtml(node).c_str());
}

NATIVE(nSetHtml)
{
    NODE(node, 0);
    auto& s                          = self(ctx);
    std::vector<dom::NodePtr> old    = node->children;
    for (const dom::NodePtr& child : old)
        dom::removeFromParent(child);
    for (const dom::NodePtr& child : s.doc->parseFragment(argStr(ctx, argc, argv, 1)))
        dom::appendChild(node, child);
    s.changed();
    return JS_UNDEFINED;
}

NATIVE(nOuter)
{
    NODE(node, 0);
    return JS_NewString(ctx, dom::outerHtml(node).c_str());
}

NATIVE(nGetAttr)
{
    NODE(node, 0);
    const std::string* value = node->attr(argStr(ctx, argc, argv, 1));
    return value ? JS_NewString(ctx, value->c_str()) : JS_NULL;
}

NATIVE(nSetAttr)
{
    NODE(node, 0);
    node->setAttr(argStr(ctx, argc, argv, 1), argStr(ctx, argc, argv, 2));
    self(ctx).changed();
    return JS_UNDEFINED;
}

NATIVE(nRemoveAttr)
{
    NODE(node, 0);
    node->removeAttr(argStr(ctx, argc, argv, 1));
    self(ctx).changed();
    return JS_UNDEFINED;
}

NATIVE(nAttrNames)
{
    NODE(node, 0);
    JSValue list = JS_NewArray(ctx);
    for (size_t i = 0; i < node->attrs.size(); i++)
        JS_SetPropertyUint32(ctx, list, (uint32_t)i, JS_NewString(ctx, node->attrs[i].first.c_str()));
    return list;
}

NATIVE(nAppend)
{
    NODE(parent, 0);
    NODE(child, 1);
    dom::appendChild(parent, child);
    self(ctx).changed();
    return JS_UNDEFINED;
}

NATIVE(nInsertBefore)
{
    NODE(parent, 0);
    NODE(child, 1);
    dom::insertBefore(parent, child, argNode(ctx, argc, argv, 2));
    self(ctx).changed();
    return JS_UNDEFINED;
}

NATIVE(nRemove)
{
    NODE(parent, 0);
    NODE(child, 1);
    dom::removeChild(parent, child);
    self(ctx).changed();
    return JS_UNDEFINED;
}

NATIVE(nClone)
{
    NODE(node, 0);
    dom::NodePtr copy = dom::cloneNode(node, argc > 1 && JS_ToBool(ctx, argv[1]));
    self(ctx).doc->adopt(copy);
    return self(ctx).wrap(copy);
}

NATIVE(nQuery)
{
    NODE(scope, 0);
    return self(ctx).wrap(dom::querySelector(scope, argStr(ctx, argc, argv, 1)));
}

NATIVE(nQueryAll)
{
    NODE(scope, 0);
    return self(ctx).array(dom::querySelectorAll(scope, argStr(ctx, argc, argv, 1)));
}

NATIVE(nMatches)
{
    NODE(node, 0);
    return JS_NewBool(ctx, dom::matches(node, argStr(ctx, argc, argv, 1)));
}

NATIVE(nBody) { return self(ctx).wrap(self(ctx).doc->root()); }

NATIVE(nById) { return self(ctx).wrap(self(ctx).doc->byId(argStr(ctx, argc, argv, 0))); }

NATIVE(nCreateElement) { return self(ctx).wrap(self(ctx).doc->createElement(argStr(ctx, argc, argv, 0))); }

NATIVE(nCreateText) { return self(ctx).wrap(self(ctx).doc->createText(argStr(ctx, argc, argv, 0))); }

NATIVE(nCreateFragment)
{
    auto fragment  = std::make_shared<dom::Node>();
    fragment->type = dom::Node::Type::Document;
    self(ctx).doc->adopt(fragment);
    return self(ctx).wrap(fragment);
}

NATIVE(nParse) { return self(ctx).array(self(ctx).doc->parseFragment(argStr(ctx, argc, argv, 0))); }

NATIVE(nLog)
{
    int level       = 0;
    if (argc > 0)
        JS_ToInt32(ctx, &level, argv[0]);
    std::string msg = argStr(ctx, argc, argv, 1);
    CardScript::log(level, msg);
    return JS_UNDEFINED;
}

NATIVE(nTimer)
{
    int id = 0, ms = 0;
    if (argc > 1)
    {
        JS_ToInt32(ctx, &id, argv[0]);
        JS_ToInt32(ctx, &ms, argv[1]);
    }
    self(ctx).timers.emplace_back(id, ms);
    return JS_UNDEFINED;
}

NATIVE(nStorage)
{
    auto& map      = storage(argStr(ctx, argc, argv, 0));
    std::string op = argStr(ctx, argc, argv, 1);
    std::string key = argStr(ctx, argc, argv, 2);
    if (op == "get")
    {
        auto it = map.find(key);
        return it == map.end() ? JS_NULL : JS_NewString(ctx, it->second.c_str());
    }
    if (op == "set")
        map[key] = argStr(ctx, argc, argv, 3);
    else if (op == "remove")
        map.erase(key);
    else if (op == "clear")
        map.clear();
    else if (op == "length")
        return JS_NewString(ctx, std::to_string(map.size()).c_str());
    else if (op == "key")
    {
        size_t index = std::strtoul(key.c_str(), nullptr, 10);
        if (index < map.size())
            return JS_NewString(ctx, std::next(map.begin(), (long)index)->first.c_str());
        return JS_NULL;
    }
    return JS_UNDEFINED;
}

NATIVE(nCommand)
{
    self(ctx).commands.push_back(argStr(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

int interrupt(JSRuntime*, void* opaque)
{
    return Clock::now() > ((CardScript::Native*)opaque)->deadline ? 1 : 0;
}

std::string exceptionText(JSContext* ctx)
{
    JSValue exc  = JS_GetException(ctx);
    std::string text = str(ctx, exc);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (JS_IsString(stack))
        text += "\n" + str(ctx, stack);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
    return text;
}

} // namespace

CardScript::CardScript(const std::string& prelude, std::string mediaDir)
    : native(std::make_unique<Native>())
{
    Native& s  = *native;
    s.mediaDir = std::move(mediaDir);
    s.rt       = JS_NewRuntime();
    JS_SetMemoryLimit(s.rt, MEMORY_LIMIT);
    JS_SetMaxStackSize(s.rt, STACK_LIMIT);
    JS_SetInterruptHandler(s.rt, interrupt, &s);
    JS_NewClassID(s.rt, &nodeClass);
    JSClassDef def = {};
    def.class_name = "Node";
    JS_NewClass(s.rt, nodeClass, &def);
    s.ctx = JS_NewContext(s.rt);
    JS_SetContextOpaque(s.ctx, &s);
    s.doc = std::make_unique<dom::Document>("");

    s.proto = JS_NewObject(s.ctx);
    JS_SetClassProto(s.ctx, nodeClass, JS_DupValue(s.ctx, s.proto));
    s.n = JS_NewObject(s.ctx);
    JS_SetPropertyStr(s.ctx, s.n, "proto", JS_DupValue(s.ctx, s.proto));
    struct
    {
        const char* name;
        JSCFunction* fn;
        int length;
    } functions[] = {
        { "nodeType", nNodeType, 1 },       { "tagName", nTagName, 1 },
        { "parent", nParent, 1 },           { "children", nChildren, 1 },
        { "text", nText, 1 },               { "setText", nSetText, 2 },
        { "html", nHtml, 1 },               { "setHtml", nSetHtml, 2 },
        { "outer", nOuter, 1 },             { "getAttr", nGetAttr, 2 },
        { "setAttr", nSetAttr, 3 },         { "removeAttr", nRemoveAttr, 2 },
        { "attrNames", nAttrNames, 1 },     { "append", nAppend, 2 },
        { "insertBefore", nInsertBefore, 3 }, { "remove", nRemove, 2 },
        { "clone", nClone, 2 },             { "query", nQuery, 2 },
        { "queryAll", nQueryAll, 2 },       { "matches", nMatches, 2 },
        { "body", nBody, 0 },               { "byId", nById, 1 },
        { "createElement", nCreateElement, 1 }, { "createText", nCreateText, 1 },
        { "createFragment", nCreateFragment, 0 }, { "parse", nParse, 1 },
        { "log", nLog, 2 },                 { "timer", nTimer, 2 },
        { "storage", nStorage, 4 },         { "command", nCommand, 1 },
    };
    for (const auto& f : functions)
        JS_SetPropertyStr(s.ctx, s.n, f.name, JS_NewCFunction(s.ctx, f.fn, f.name, f.length));
    JSValue global = JS_GetGlobalObject(s.ctx);
    JS_SetPropertyStr(s.ctx, global, "__n", JS_DupValue(s.ctx, s.n));
    JS_FreeValue(s.ctx, global);

    run("prelude.js", prelude);
}

CardScript::~CardScript()
{
    Native& s = *native;
    for (auto& [_, value] : s.objects)
        JS_FreeValue(s.ctx, value);
    s.objects.clear();
    JS_FreeValue(s.ctx, s.n);
    JS_FreeValue(s.ctx, s.proto);
    JS_FreeContext(s.ctx);
    JS_FreeRuntime(s.rt);
}

bool CardScript::wanted(const std::string& html)
{
    if (html.find("<script") != std::string::npos)
        return true;
    // An on...= attribute, e.g. onclick="...".
    for (size_t at = html.find(" on"); at != std::string::npos; at = html.find(" on", at + 3))
    {
        size_t i = at + 3;
        while (i < html.size() && std::isalpha((unsigned char)html[i]))
            i++;
        if (i > at + 3 && i < html.size() && html[i] == '=')
            return true;
    }
    return false;
}

void CardScript::show(const std::string& html, const std::string& bodyClass)
{
    Native& s = *native;
    // As in Anki's reviewer: the side inside <div id="qa"> in the body.
    s.doc = std::make_unique<dom::Document>("<div id=\"qa\">" + html + "</div>");
    dom::NodePtr body = s.doc->root();
    body->type        = dom::Node::Type::Element;
    body->tag         = "body";
    body->setAttr("class", bodyClass);

    for (const dom::NodePtr& script : dom::querySelectorAll(body, "script"))
    {
        const std::string* type = script->attr("type");
        if (type && !type->empty() && *type != "text/javascript" && *type != "application/javascript" &&
            *type != "module")
            continue;
        std::string code;
        if (const std::string* src = script->attr("src"))
        {
            // Media files only (e.g. "_myscript.js"), as Anki serves them.
            if (src->find('/') != std::string::npos || src->find(':') != std::string::npos)
                continue;
            code = readFile(s.mediaDir + "/" + *src);
        }
        else
            code = dom::textContent(script);
        if (!code.empty())
            run("card", code);
    }
    JSValue fn = JS_GetPropertyStr(s.ctx, s.n, "shown");
    s.deadline = Clock::now() + TIME_BUDGET;
    JSValue r  = JS_Call(s.ctx, fn, JS_UNDEFINED, 0, nullptr);
    if (JS_IsException(r))
        error = exceptionText(s.ctx);
    JS_FreeValue(s.ctx, r);
    JS_FreeValue(s.ctx, fn);
    runJobs();
    s.doc->dirty = true;
}

std::string CardScript::html()
{
    native->doc->dirty = false;
    return dom::innerHtml(native->doc->root(), true);
}

bool CardScript::dirty() const { return native->doc->dirty; }

bool CardScript::click(int serial)
{
    Native& s = *native;
    dom::NodePtr node = s.doc->bySerial(serial);
    if (!node)
        return false;
    JSValue fn     = JS_GetPropertyStr(s.ctx, s.n, "dispatch");
    JSValue args[] = { s.wrap(node), JS_NewString(s.ctx, "click") };
    s.deadline     = Clock::now() + TIME_BUDGET;
    JSValue r      = JS_Call(s.ctx, fn, JS_UNDEFINED, 2, args);
    bool handled   = false;
    if (JS_IsException(r))
    {
        error = exceptionText(s.ctx);
        CardScript::log(2, error);
        handled = true; // a handler was there, even if it failed
    }
    else
        handled = JS_ToBool(s.ctx, r);
    JS_FreeValue(s.ctx, r);
    JS_FreeValue(s.ctx, args[0]);
    JS_FreeValue(s.ctx, args[1]);
    JS_FreeValue(s.ctx, fn);
    runJobs();
    return handled;
}

void CardScript::eval(const std::string& code) { run("app", code); }

std::vector<std::pair<int, int>> CardScript::takeTimers()
{
    std::vector<std::pair<int, int>> out;
    out.swap(native->timers);
    return out;
}

std::vector<std::string> CardScript::takeCommands()
{
    std::vector<std::string> out;
    out.swap(native->commands);
    return out;
}

void CardScript::fireTimer(int id)
{
    Native& s     = *native;
    JSValue fn    = JS_GetPropertyStr(s.ctx, s.n, "fireTimer");
    JSValue arg   = JS_NewInt32(s.ctx, id);
    s.deadline    = Clock::now() + TIME_BUDGET;
    JSValue r     = JS_Call(s.ctx, fn, JS_UNDEFINED, 1, &arg);
    if (JS_IsException(r))
    {
        error = exceptionText(s.ctx);
        CardScript::log(2, error);
    }
    JS_FreeValue(s.ctx, r);
    JS_FreeValue(s.ctx, fn);
    runJobs();
}

void CardScript::run(const char* what, const std::string& code)
{
    Native& s  = *native;
    s.deadline = Clock::now() + TIME_BUDGET;
    JS_UpdateStackTop(s.rt);
    JSValue r = JS_Eval(s.ctx, code.c_str(), code.size(), what, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
    {
        error = exceptionText(s.ctx);
        CardScript::log(2, std::string(what) + ": " + error);
    }
    JS_FreeValue(s.ctx, r);
    runJobs();
}

// Promise callbacks (async functions, .then) queued by the last run.
void CardScript::runJobs()
{
    JSContext* jobCtx;
    for (int i = 0; i < 1000 && JS_ExecutePendingJob(native->rt, &jobCtx) > 0; i++)
        ;
}

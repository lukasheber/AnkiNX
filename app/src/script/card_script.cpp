#include "script/card_script.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <quickjs.h>

using Clock = std::chrono::steady_clock;

namespace
{

// Budget for one entry into the page (a side shown, a tap, timers), including
// what it starts at once.
constexpr auto TIME_BUDGET = std::chrono::milliseconds(500);
// The runtime (linkedom, jQuery once used) takes a few MB of this.
constexpr size_t MEMORY_LIMIT = 64u << 20;
// Within the calling thread's stack (borealis' main thread).
constexpr size_t STACK_LIMIT = 256u << 10;
// Rounds of "run what is due now" after an entry (zero-delay timers chained).
constexpr int SETTLE_ROUNDS = 64;

// localStorage, sessionStorage, cookies: kept for the app session.
std::map<std::string, std::string>& storage(const std::string& kind)
{
    static std::map<std::string, std::map<std::string, std::string>> all;
    return all[kind];
}

// The runtime compiled once per app session (parsing ~600 KB of JavaScript
// per card would cost more than running it).
struct Compiled
{
    std::string source;
    std::vector<uint8_t> bytecode;
};
Compiled& compiled()
{
    static Compiled c;
    return c;
}

// A media file's path inside the media folder, or "" if `name` leaves it.
std::string mediaFile(const std::string& dir, std::string name)
{
    while (name.rfind("./", 0) == 0)
        name.erase(0, 2);
    if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos || name.find('\0') != std::string::npos)
        return "";
    for (size_t start = 0; start <= name.size();)
    {
        size_t end = name.find('/', start);
        if (end == std::string::npos)
            end = name.size();
        std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
            return "";
        start = end + 1;
    }
    return dir + "/" + name;
}

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

} // namespace

std::function<void(int, const std::string&)> CardScript::log = [](int level, const std::string& msg) {
    std::fprintf(stderr, "card script [%d]: %s\n", level, msg.c_str());
};

struct CardScript::Native
{
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    JSValue entry = JS_UNDEFINED; // globalThis.__ankinx
    std::string mediaDir;
    Host host;
    std::vector<std::string> commands;
    Clock::time_point deadline;
    std::string error;
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

std::string exceptionText(JSContext* ctx)
{
    JSValue exc      = JS_GetException(ctx);
    std::string text = str(ctx, exc);
    JSValue stack    = JS_GetPropertyStr(ctx, exc, "stack");
    if (JS_IsString(stack))
        text += "\n" + str(ctx, stack);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
    return text;
}

#define NATIVE(name) JSValue name(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)

NATIVE(nLog)
{
    int level = 0;
    if (argc > 0)
        JS_ToInt32(ctx, &level, argv[0]);
    CardScript::log(level, argStr(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

// A classic script in the global scope (its top-level let/const/class are
// shared with the page's other scripts, as in browsers). Errors are thrown to
// the caller, which reports them.
NATIVE(nEvalScript)
{
    std::string code = argStr(ctx, argc, argv, 0);
    std::string name = argStr(ctx, argc, argv, 1);
    return JS_Eval(ctx, code.c_str(), code.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
}

// A module script; returns its evaluation promise.
NATIVE(nEvalModule)
{
    std::string code = argStr(ctx, argc, argv, 0);
    std::string name = argStr(ctx, argc, argv, 1);
    return JS_Eval(ctx, code.c_str(), code.size(), name.c_str(), JS_EVAL_TYPE_MODULE);
}

JSModuleDef* loadModule(JSContext* ctx, const char* name, void*)
{
    std::string path = mediaFile(self(ctx).mediaDir, name);
    std::string code;
    if (path.empty() || !readFile(path, code))
    {
        JS_ThrowReferenceError(ctx, "could not load module '%s' (modules come from the media folder)", name);
        return nullptr;
    }
    JSValue fn = JS_Eval(ctx, code.c_str(), code.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn))
        return nullptr;
    auto* module = (JSModuleDef*)JS_VALUE_GET_PTR(fn);
    JS_FreeValue(ctx, fn);
    return module;
}

NATIVE(nReadMedia)
{
    std::string path = mediaFile(self(ctx).mediaDir, argStr(ctx, argc, argv, 0));
    std::string data;
    if (path.empty() || !readFile(path, data))
        return JS_NULL;
    return JS_NewStringLen(ctx, data.data(), data.size());
}

NATIVE(nReadMediaBytes)
{
    std::string path = mediaFile(self(ctx).mediaDir, argStr(ctx, argc, argv, 0));
    std::string data;
    if (path.empty() || !readFile(path, data))
        return JS_NULL;
    return JS_NewArrayBufferCopy(ctx, (const uint8_t*)data.data(), data.size());
}

NATIVE(nStorage)
{
    auto& map       = storage(argStr(ctx, argc, argv, 0));
    std::string op  = argStr(ctx, argc, argv, 1);
    std::string key = argStr(ctx, argc, argv, 2);
    if (op == "get")
    {
        auto it = map.find(key);
        return it == map.end() ? JS_NULL : JS_NewStringLen(ctx, it->second.data(), it->second.size());
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

// The body's layout: {serial: [x, y, w, h, clientLeft, clientTop,
// clientWidth, clientHeight, {property: value}]}, or null without a renderer.
NATIVE(nMeasure)
{
    auto& s = self(ctx);
    if (!s.host.layout)
        return JS_NULL;
    CardScript::Layout layout =
        s.host.layout(argStr(ctx, argc, argv, 0), argStr(ctx, argc, argv, 1), argStr(ctx, argc, argv, 2));
    JSValue out = JS_NewObject(ctx);
    for (const auto& [serial, box] : layout)
    {
        JSValue entry = JS_NewArray(ctx);
        const float numbers[] = { box.x, box.y, box.width, box.height, box.clientLeft, box.clientTop, box.clientWidth, box.clientHeight };
        for (uint32_t i = 0; i < 8; i++)
            JS_SetPropertyUint32(ctx, entry, i, JS_NewFloat64(ctx, numbers[i]));
        JSValue style = JS_NewObject(ctx);
        for (const auto& [name, value] : box.style)
            JS_SetPropertyStr(ctx, style, name.c_str(), JS_NewStringLen(ctx, value.data(), value.size()));
        JS_SetPropertyUint32(ctx, entry, 8, style);
        JS_SetPropertyUint32(ctx, out, (uint32_t)serial, entry);
    }
    return out;
}

NATIVE(nViewport)
{
    auto& s                     = self(ctx);
    CardScript::Viewport view = s.host.viewport ? s.host.viewport() : CardScript::Viewport {};
    JSValue out                 = JS_NewArray(ctx);
    const float values[]        = { view.width, view.height, view.scrollY, view.pixelRatio };
    for (uint32_t i = 0; i < 4; i++)
        JS_SetPropertyUint32(ctx, out, i, JS_NewFloat64(ctx, values[i]));
    return out;
}

NATIVE(nScrollTo)
{
    auto& s  = self(ctx);
    double y = 0;
    if (argc > 0)
        JS_ToFloat64(ctx, &y, argv[0]);
    if (s.host.scrollTo)
        s.host.scrollTo((float)y);
    return JS_UNDEFINED;
}

// The shown card's scheduling states and context (JSON), or null.
NATIVE(nSchedulingStates)
{
    auto& s          = self(ctx);
    std::string json = s.host.schedulingStates ? s.host.schedulingStates() : std::string();
    return json.empty() ? JS_NULL : JS_NewStringLen(ctx, json.data(), json.size());
}

NATIVE(nSetSchedulingStates)
{
    auto& s = self(ctx);
    if (s.host.setSchedulingStates)
        s.host.setSchedulingStates(argStr(ctx, argc, argv, 0), argStr(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

int interrupt(JSRuntime*, void* opaque)
{
    return Clock::now() > ((CardScript::Native*)opaque)->deadline ? 1 : 0;
}

// Start an entry into the page: a fresh time budget.
void begin(CardScript::Native& s)
{
    s.deadline = Clock::now() + TIME_BUDGET;
    JS_UpdateStackTop(s.rt);
}

// Call __ankinx[name](args) (args are freed). The result is freed unless
// `result` takes it; false after an exception.
bool callEntry(CardScript::Native& s, const char* name, std::vector<JSValue> args = {}, JSValue* result = nullptr)
{
    JSValue fn = JS_GetPropertyStr(s.ctx, s.entry, name);
    JSValue r  = JS_Call(s.ctx, fn, s.entry, (int)args.size(), args.data());
    for (JSValue& a : args)
        JS_FreeValue(s.ctx, a);
    JS_FreeValue(s.ctx, fn);
    if (JS_IsException(r))
    {
        s.error = exceptionText(s.ctx);
        CardScript::log(2, std::string(name) + ": " + s.error);
        return false;
    }
    if (result)
        *result = r;
    else
        JS_FreeValue(s.ctx, r);
    return true;
}

void runJobs(CardScript::Native& s)
{
    JSContext* jobCtx;
    for (int i = 0; i < 10000; i++)
    {
        int r = JS_ExecutePendingJob(s.rt, &jobCtx);
        if (r == 0)
            break;
        if (r < 0)
        {
            s.error = exceptionText(jobCtx);
            CardScript::log(2, s.error);
        }
    }
}

} // namespace

CardScript::CardScript(const std::string& runtime, std::string mediaDir, Host host)
    : native(std::make_unique<Native>())
{
    Native& s  = *native;
    s.mediaDir = std::move(mediaDir);
    s.host     = std::move(host);
    s.rt       = JS_NewRuntime();
    JS_SetMemoryLimit(s.rt, MEMORY_LIMIT);
    JS_SetMaxStackSize(s.rt, STACK_LIMIT);
    JS_SetInterruptHandler(s.rt, interrupt, &s);
    JS_SetModuleLoaderFunc(s.rt, nullptr, loadModule, &s);
    s.ctx = JS_NewContext(s.rt);
    JS_SetContextOpaque(s.ctx, &s);
    begin(s);

    JSValue bridge = JS_NewObject(s.ctx);
    struct
    {
        const char* name;
        JSCFunction* fn;
        int length;
    } functions[] = {
        { "log", nLog, 2 },           { "evalScript", nEvalScript, 2 },
        { "evalModule", nEvalModule, 2 }, { "readMedia", nReadMedia, 1 },
        { "readMediaBytes", nReadMediaBytes, 1 }, { "storage", nStorage, 4 },
        { "command", nCommand, 1 },   { "measure", nMeasure, 3 },
        { "viewport", nViewport, 0 }, { "scrollTo", nScrollTo, 1 },
        { "schedulingStates", nSchedulingStates, 0 }, { "setSchedulingStates", nSetSchedulingStates, 2 },
    };
    for (const auto& f : functions)
        JS_SetPropertyStr(s.ctx, bridge, f.name, JS_NewCFunction(s.ctx, f.fn, f.name, f.length));
    JSValue global = JS_GetGlobalObject(s.ctx);
    JS_SetPropertyStr(s.ctx, global, "__native", bridge);

    Compiled& c = compiled();
    JSValue fn;
    if (!c.bytecode.empty() && c.source == runtime)
        fn = JS_ReadObject(s.ctx, c.bytecode.data(), c.bytecode.size(), JS_READ_OBJ_BYTECODE);
    else
    {
        fn = JS_Eval(s.ctx, runtime.c_str(), runtime.size(), "runtime.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(fn))
        {
            size_t size   = 0;
            uint8_t* data = JS_WriteObject(s.ctx, &size, fn, JS_WRITE_OBJ_BYTECODE);
            if (data)
            {
                c.source = runtime;
                c.bytecode.assign(data, data + size);
                js_free(s.ctx, data);
            }
        }
    }
    JSValue r = JS_IsException(fn) ? JS_EXCEPTION : JS_EvalFunction(s.ctx, fn);
    if (JS_IsException(r))
    {
        s.error = exceptionText(s.ctx);
        log(2, "runtime.js: " + s.error);
    }
    JS_FreeValue(s.ctx, r);
    s.entry = JS_GetPropertyStr(s.ctx, global, "__ankinx");
    JS_FreeValue(s.ctx, global);
    runJobs(s);
}

CardScript::~CardScript()
{
    Native& s = *native;
    JS_FreeValue(s.ctx, s.entry);
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

const std::string& CardScript::lastError() const { return native->error; }

// Run what the last entry started and is due now: promise callbacks,
// zero-delay timers, script files, until nothing is due (within the entry's
// budget). Later timers are the reviewer's to schedule (nextTimer()).
void CardScript::settle()
{
    Native& s = *native;
    pendingValid = false;
    for (int round = 0; round < SETTLE_ROUNDS && Clock::now() < s.deadline; round++)
    {
        runJobs(s);
        JSValue next = JS_UNDEFINED;
        int ms       = -1;
        if (callEntry(s, "nextTimer", {}, &next))
            JS_ToInt32(s.ctx, &ms, next);
        JS_FreeValue(s.ctx, next);
        if (ms != 0)
            break;
        callEntry(s, "runTimers");
    }
    runJobs(s);
}

void CardScript::show(bool answer, const std::string& html, const std::string& bodyClass)
{
    Native& s = *native;
    begin(s);
    callEntry(s, "show",
              { JS_NewBool(s.ctx, answer), JS_NewStringLen(s.ctx, html.data(), html.size()), JS_NewString(s.ctx, ""),
                JS_NewStringLen(s.ctx, bodyClass.data(), bodyClass.size()) });
    settle();
}

void CardScript::place(const std::string& html, const std::string& bodyClass)
{
    Native& s = *native;
    begin(s);
    callEntry(s, "place",
              { JS_NewStringLen(s.ctx, html.data(), html.size()), JS_NewStringLen(s.ctx, bodyClass.data(), bodyClass.size()) });
    settle();
}

bool CardScript::mutateStates(const std::string& key, const std::string& code)
{
    Native& s = *native;
    begin(s);
    // RUN_STATE_MUTATION in Anki's qt/aqt/reviewer.py: the code on lines of its
    // own, so a comment on its last line doesn't swallow what follows.
    std::string run = "anki.mutateNextCardStates('" + key + "', async (states, customData, ctx) => {\n" + code +
                      "\n}).finally(() => bridgeCommand('statesMutated'));";
    JSValue r    = JS_Eval(s.ctx, run.c_str(), run.size(), "custom scheduling", JS_EVAL_TYPE_GLOBAL);
    bool started = !JS_IsException(r);
    if (!started)
    {
        s.error = exceptionText(s.ctx);
        log(2, "custom scheduling: " + s.error);
    }
    JS_FreeValue(s.ctx, r);
    settle();
    return started;
}

std::string CardScript::serialize()
{
    Native& s = *native;
    begin(s);
    JSValue r = JS_UNDEFINED;
    std::string out;
    if (callEntry(s, "html", {}, &r))
        out = str(s.ctx, r);
    JS_FreeValue(s.ctx, r);
    return out;
}

std::string CardScript::html()
{
    if (!pendingValid)
        pending = serialize();
    pendingValid = false;
    rendered     = pending;
    return rendered;
}

bool CardScript::dirty()
{
    pending      = serialize();
    pendingValid = true;
    return pending != rendered;
}

std::string CardScript::bodyClass()
{
    Native& s = *native;
    begin(s);
    JSValue r = JS_UNDEFINED;
    std::string out;
    if (callEntry(s, "bodyClass", {}, &r))
        out = str(s.ctx, r);
    JS_FreeValue(s.ctx, r);
    return out;
}

std::string CardScript::extraCss()
{
    Native& s = *native;
    begin(s);
    JSValue r = JS_UNDEFINED;
    std::string out;
    if (callEntry(s, "extraCss", {}, &r))
        out = str(s.ctx, r);
    JS_FreeValue(s.ctx, r);
    return out;
}

bool CardScript::tap(int serial, float x, float y)
{
    Native& s = *native;
    begin(s);
    JSValue r    = JS_UNDEFINED;
    bool handled = false;
    if (callEntry(s, "tap", { JS_NewInt32(s.ctx, serial), JS_NewFloat64(s.ctx, x), JS_NewFloat64(s.ctx, y) }, &r))
        handled = JS_ToBool(s.ctx, r);
    else
        handled = true; // card code ran, even if it failed
    JS_FreeValue(s.ctx, r);
    settle();
    return handled;
}

void CardScript::typed(int serial, const std::string& text)
{
    Native& s = *native;
    begin(s);
    JSValue r = JS_UNDEFINED;
    callEntry(s, "typed", { JS_NewInt32(s.ctx, serial), JS_NewStringLen(s.ctx, text.data(), text.size()) }, &r);
    JS_FreeValue(s.ctx, r);
    settle();
}

bool CardScript::key(const std::string& key, const std::string& code, int keyCode)
{
    Native& s     = *native;
    begin(s);
    bool handled  = true;
    for (const char* type : { "keydown", "keyup" })
    {
        JSValue r = JS_UNDEFINED;
        if (callEntry(s, "key",
                      { JS_NewString(s.ctx, type), JS_NewString(s.ctx, key.c_str()), JS_NewString(s.ctx, code.c_str()),
                        JS_NewInt32(s.ctx, keyCode) },
                      &r) &&
            std::strcmp(type, "keydown") == 0)
            handled = !JS_ToBool(s.ctx, r); // cancelled: the page took it
        JS_FreeValue(s.ctx, r);
    }
    settle();
    return handled;
}

void CardScript::eval(const std::string& code)
{
    Native& s = *native;
    begin(s);
    JSValue r = JS_Eval(s.ctx, code.c_str(), code.size(), "app", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
    {
        s.error = exceptionText(s.ctx);
        log(2, "app: " + s.error);
    }
    JS_FreeValue(s.ctx, r);
    settle();
}

int CardScript::nextTimer()
{
    Native& s = *native;
    begin(s);
    JSValue r = JS_UNDEFINED;
    int ms    = -1;
    if (callEntry(s, "nextTimer", {}, &r))
        JS_ToInt32(s.ctx, &ms, r);
    JS_FreeValue(s.ctx, r);
    return ms;
}

void CardScript::runTimers()
{
    Native& s = *native;
    begin(s);
    callEntry(s, "runTimers");
    settle();
}

std::vector<std::string> CardScript::takeCommands()
{
    std::vector<std::string> out;
    out.swap(native->commands);
    return out;
}

#include "forest/forest_script.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <quickjs.h>

#include "forest/pixel_canvas.hpp"

using Clock = std::chrono::steady_clock;

namespace
{

// A build lays out every tree and draws each sprite: seconds on the console.
constexpr auto BUILD_BUDGET = std::chrono::seconds(60);
// A scene draws some layers on its first frame (misty valley's beams, pixel by
// pixel: 3 s on the console), and it runs on its own thread: only a script
// stuck for good is stopped.
constexpr auto FRAME_BUDGET = std::chrono::seconds(20);
// Sprites, layers and the scenes' tables; the forest thread's own runtime.
constexpr size_t MEMORY_LIMIT = 96u << 20;
// Within the forest thread's stack (see app/src/forest/forest_view.cpp).
constexpr size_t STACK_LIMIT = 512u << 10;

JSClassID canvasClass = 0;

void finalizeCanvas(JSRuntime*, JSValueConst value) { delete (PixelCanvas*)JS_GetOpaque(value, canvasClass); }

std::string str(JSContext* ctx, JSValueConst value)
{
    size_t len;
    const char* s = JS_ToCStringLen(ctx, &len, value);
    std::string out = s ? std::string(s, len) : std::string();
    JS_FreeCString(ctx, s);
    return out;
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

PixelCanvas* canvasOf(JSContext* ctx, JSValueConst value)
{
    return (PixelCanvas*)JS_GetOpaque(value, canvasClass);
}

double num(JSContext* ctx, int argc, JSValueConst* argv, int i)
{
    double v = 0;
    if (i < argc)
        JS_ToFloat64(ctx, &v, argv[i]);
    return v;
}

int integer(JSContext* ctx, int argc, JSValueConst* argv, int i)
{
    int32_t v = 0;
    if (i < argc)
        JS_ToInt32(ctx, &v, argv[i]);
    return v;
}

// A typed array's bytes.
uint8_t* bytes(JSContext* ctx, JSValueConst value, size_t& size)
{
    size_t offset = 0, length = 0, per = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &per);
    if (JS_IsException(buffer))
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        size = 0;
        return nullptr;
    }
    size_t whole;
    uint8_t* data = JS_GetArrayBuffer(ctx, &whole, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data || offset + length > whole)
    {
        size = 0;
        return nullptr;
    }
    size = length;
    return data + offset;
}

// ---------- __pc: the canvases ----------

#define CANVAS(i)                                                                                                     \
    PixelCanvas* c = i < argc ? canvasOf(ctx, argv[i]) : nullptr;                                                     \
    if (!c)                                                                                                           \
        return JS_ThrowTypeError(ctx, "not a canvas");

JSValue pcCreate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    int w = integer(ctx, argc, argv, 0), h = integer(ctx, argc, argv, 1);
    if (w < 0 || h < 0 || (int64_t)w * h > (16 << 20))
        return JS_ThrowRangeError(ctx, "canvas too large");
    JSValue obj = JS_NewObjectClass(ctx, canvasClass);
    if (JS_IsException(obj))
        return obj;
    JS_SetOpaque(obj, new PixelCanvas(w, h));
    return obj;
}

JSValue pcResize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    int w = integer(ctx, argc, argv, 1), h = integer(ctx, argc, argv, 2);
    if (w < 0 || h < 0 || (int64_t)w * h > (16 << 20))
        return JS_ThrowRangeError(ctx, "canvas too large");
    c->resize(w, h);
    return JS_UNDEFINED;
}

JSValue pcFill(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    return JS_NewBool(ctx, argc > 1 && c->setFill(str(ctx, argv[1])));
}

JSValue pcAlpha(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    c->setAlpha(num(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

JSValue pcOp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    return JS_NewBool(ctx, argc > 1 && c->setOp(str(ctx, argv[1])));
}

JSValue pcSave(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    c->save();
    return JS_UNDEFINED;
}

JSValue pcRestore(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    c->restore();
    return JS_UNDEFINED;
}

PixelCanvas::Transform matrix(JSContext* ctx, int argc, JSValueConst* argv)
{
    return { num(ctx, argc, argv, 1), num(ctx, argc, argv, 2), num(ctx, argc, argv, 3),
             num(ctx, argc, argv, 4), num(ctx, argc, argv, 5), num(ctx, argc, argv, 6) };
}

JSValue pcSetTransform(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    c->setTransform(matrix(ctx, argc, argv));
    return JS_UNDEFINED;
}

JSValue pcTransform(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    c->transform(matrix(ctx, argc, argv));
    return JS_UNDEFINED;
}

JSValue pcFillRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    c->fillRect(num(ctx, argc, argv, 1), num(ctx, argc, argv, 2), num(ctx, argc, argv, 3), num(ctx, argc, argv, 4));
    return JS_UNDEFINED;
}

JSValue pcClearRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    c->clearRect(num(ctx, argc, argv, 1), num(ctx, argc, argv, 2), num(ctx, argc, argv, 3), num(ctx, argc, argv, 4));
    return JS_UNDEFINED;
}

JSValue pathOp(JSContext* ctx, int argc, JSValueConst* argv, bool clip)
{
    CANVAS(0);
    size_t size  = 0;
    uint8_t* ops = argc > 1 ? bytes(ctx, argv[1], size) : nullptr;
    if (!ops)
        return JS_UNDEFINED;
    if (clip)
        c->clipPath((const double*)ops, size / sizeof(double));
    else
        c->fillPath((const double*)ops, size / sizeof(double));
    return JS_UNDEFINED;
}

JSValue pcFillPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return pathOp(ctx, argc, argv, false); }

JSValue pcClipPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return pathOp(ctx, argc, argv, true); }

JSValue pcDrawImage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    PixelCanvas* src = argc > 1 ? canvasOf(ctx, argv[1]) : nullptr;
    if (!src)
        return JS_ThrowTypeError(ctx, "drawImage: not a canvas");
    double v[8];
    for (int i = 0; i < 8; i++)
        v[i] = num(ctx, argc, argv, 2 + i);
    c->drawImage(*src, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    return JS_UNDEFINED;
}

JSValue pcGetImageData(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    int x = integer(ctx, argc, argv, 1), y = integer(ctx, argc, argv, 2);
    int w = integer(ctx, argc, argv, 3), h = integer(ctx, argc, argv, 4);
    if (w <= 0 || h <= 0 || (int64_t)w * h > (16 << 20))
        return JS_ThrowRangeError(ctx, "getImageData: bad size");
    std::vector<uint8_t> out((size_t)w * h * 4);
    c->getImageData(x, y, w, h, out.data());
    return JS_NewArrayBufferCopy(ctx, out.data(), out.size());
}

JSValue pcPutImageData(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS(0);
    size_t size = 0;
    uint8_t* in = argc > 1 ? bytes(ctx, argv[1], size) : nullptr;
    int iw = integer(ctx, argc, argv, 2), ih = integer(ctx, argc, argv, 3);
    if (!in || iw <= 0 || ih <= 0 || size < (size_t)iw * ih * 4)
        return JS_ThrowTypeError(ctx, "putImageData: bad image");
    c->putImageData(in, iw, ih, integer(ctx, argc, argv, 4), integer(ctx, argc, argv, 5),
                    integer(ctx, argc, argv, 6), integer(ctx, argc, argv, 7), integer(ctx, argc, argv, 8),
                    integer(ctx, argc, argv, 9));
    return JS_UNDEFINED;
}

// ---------- __native ----------

JSValue nLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    int level = integer(ctx, argc, argv, 0);
    ForestScript::log(level, argc > 1 ? str(ctx, argv[1]) : std::string());
    return JS_UNDEFINED;
}

JSValue nNow(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    static const Clock::time_point start = Clock::now();
    return JS_NewFloat64(ctx, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
}

} // namespace

std::function<void(int, const std::string&)> ForestScript::log = [](int level, const std::string& msg) {
    std::fprintf(stderr, "forest [%d]: %s\n", level, msg.c_str());
};

struct ForestScript::Native
{
    JSRuntime* rt   = nullptr;
    JSContext* ctx  = nullptr;
    JSValue entry   = JS_UNDEFINED; // globalThis.__forest
    JSValue canvas  = JS_UNDEFINED; // the scene's canvas, from build()
    Clock::time_point deadline;
    std::atomic<bool> cancelled{ false };
    std::string error;
};

namespace
{

int interrupt(JSRuntime*, void* opaque)
{
    auto& s = *(ForestScript::Native*)opaque;
    return s.cancelled || Clock::now() > s.deadline;
}

// Call __forest[name](...args); false (and s.error) if it threw.
bool callEntry(ForestScript::Native& s, const char* name, int argc, JSValue* argv, JSValue* result,
               Clock::duration budget)
{
    s.deadline = Clock::now() + budget;
    JS_UpdateStackTop(s.rt);
    JSValue fn = JS_GetPropertyStr(s.ctx, s.entry, name);
    JSValue r  = JS_Call(s.ctx, fn, s.entry, argc, argv);
    JS_FreeValue(s.ctx, fn);
    if (JS_IsException(r))
    {
        s.error = exceptionText(s.ctx);
        ForestScript::log(2, std::string(name) + ": " + s.error);
        return false;
    }
    if (result)
        *result = r;
    else
        JS_FreeValue(s.ctx, r);
    return true;
}

} // namespace

ForestScript::ForestScript(const std::string& bundle) : native(new Native)
{
    Native& s = *native;
    s.rt      = JS_NewRuntime();
    JS_SetMemoryLimit(s.rt, MEMORY_LIMIT);
    JS_SetMaxStackSize(s.rt, STACK_LIMIT);
    JS_SetInterruptHandler(s.rt, interrupt, &s);
    JS_NewClassID(s.rt, &canvasClass);
    JSClassDef def = {};
    def.class_name = "PixelCanvas";
    def.finalizer  = finalizeCanvas;
    JS_NewClass(s.rt, canvasClass, &def);
    s.ctx = JS_NewContext(s.rt);

    struct
    {
        const char* name;
        JSCFunction* fn;
        int length;
    } canvas[] = {
        { "create", pcCreate, 2 },           { "resize", pcResize, 3 },
        { "fill", pcFill, 2 },               { "alpha", pcAlpha, 2 },
        { "op", pcOp, 2 },                   { "save", pcSave, 1 },
        { "restore", pcRestore, 1 },         { "setTransform", pcSetTransform, 7 },
        { "transform", pcTransform, 7 },     { "fillRect", pcFillRect, 5 },
        { "clearRect", pcClearRect, 5 },     { "fillPath", pcFillPath, 2 },
        { "clipPath", pcClipPath, 2 },       { "drawImage", pcDrawImage, 10 },
        { "getImageData", pcGetImageData, 5 }, { "putImageData", pcPutImageData, 10 },
    };
    JSValue global = JS_GetGlobalObject(s.ctx);
    JSValue pc     = JS_NewObject(s.ctx);
    for (const auto& f : canvas)
        JS_SetPropertyStr(s.ctx, pc, f.name, JS_NewCFunction(s.ctx, f.fn, f.name, f.length));
    JS_SetPropertyStr(s.ctx, global, "__pc", pc);
    JSValue bridge = JS_NewObject(s.ctx);
    JS_SetPropertyStr(s.ctx, bridge, "log", JS_NewCFunction(s.ctx, nLog, "log", 2));
    JS_SetPropertyStr(s.ctx, bridge, "now", JS_NewCFunction(s.ctx, nNow, "now", 0));
    JS_SetPropertyStr(s.ctx, global, "__native", bridge);

    s.deadline = Clock::now() + BUILD_BUDGET;
    JS_UpdateStackTop(s.rt);
    JSValue r = JS_Eval(s.ctx, bundle.c_str(), bundle.size(), "forest.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
    {
        s.error = exceptionText(s.ctx);
        log(2, "forest.js: " + s.error);
    }
    JS_FreeValue(s.ctx, r);
    s.entry = JS_GetPropertyStr(s.ctx, global, "__forest");
    JS_FreeValue(s.ctx, global);
}

ForestScript::~ForestScript()
{
    Native& s = *native;
    JS_FreeValue(s.ctx, s.canvas);
    JS_FreeValue(s.ctx, s.entry);
    JS_FreeContext(s.ctx);
    JS_FreeRuntime(s.rt);
}

bool ForestScript::build(const std::string& data, int width, int height)
{
    Native& s = *native;
    if (!JS_IsObject(s.entry))
        return false;
    JSValue args[3] = { JS_NewStringLen(s.ctx, data.c_str(), data.size()), JS_NewInt32(s.ctx, width),
                        JS_NewInt32(s.ctx, height) };
    JSValue result  = JS_UNDEFINED;
    bool ok         = callEntry(s, "build", 3, args, &result, BUILD_BUDGET);
    JS_FreeValue(s.ctx, args[0]);
    JS_FreeValue(s.ctx, s.canvas);
    s.canvas = ok && canvasOf(s.ctx, result) ? result : JS_UNDEFINED;
    if (ok && JS_IsUndefined(s.canvas))
    {
        JS_FreeValue(s.ctx, result);
        s.error = "build: no canvas";
        return false;
    }
    return ok;
}

bool ForestScript::frame(double seconds)
{
    Native& s = *native;
    if (JS_IsUndefined(s.canvas))
        return false;
    JSValue arg = JS_NewFloat64(s.ctx, seconds);
    return callEntry(s, "frame", 1, &arg, nullptr, FRAME_BUDGET);
}

bool ForestScript::busy()
{
    Native& s = *native;
    if (JS_IsUndefined(s.canvas))
        return false;
    JSValue r = JS_UNDEFINED;
    if (!callEntry(s, "busy", 0, nullptr, &r, FRAME_BUDGET))
        return false;
    bool b = JS_ToBool(s.ctx, r) > 0;
    JS_FreeValue(s.ctx, r);
    return b;
}

const uint8_t* ForestScript::pixels() const
{
    PixelCanvas* c = canvasOf(native->ctx, native->canvas);
    return c ? c->data() : nullptr;
}

int ForestScript::width() const
{
    PixelCanvas* c = canvasOf(native->ctx, native->canvas);
    return c ? c->width() : 0;
}

int ForestScript::height() const
{
    PixelCanvas* c = canvasOf(native->ctx, native->canvas);
    return c ? c->height() : 0;
}

size_t ForestScript::memoryUsed() const
{
    JSMemoryUsage use;
    JS_ComputeMemoryUsage(native->rt, &use);
    return (size_t)use.malloc_size;
}

const std::string& ForestScript::error() const { return native->error; }

void ForestScript::cancel() { native->cancelled = true; }

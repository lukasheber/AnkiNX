#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A canvas 2D context for pixel art: what Memory Forest's scenes draw with
// (app/forest). Its pixels are premultiplied RGBA, 8 bits a channel, and
// every shape covers the pixels whose centres it contains, with no
// antialiasing: the scenes draw on a canvas a few hundred pixels across in
// whole pixels, and the app scales it up without smoothing. Images are
// sampled nearest-neighbour.
//
// Covered: fillRect, clearRect, filled paths (rectangles and polygons,
// nonzero), clipping to a path, drawImage, getImageData/putImageData, any
// affine transform, globalAlpha and the composite operations below. Not
// covered: strokes, text, gradients and patterns, shadows, filters.
class PixelCanvas
{
  public:
    enum class Op
    {
        SourceOver,
        Lighter,
        SourceAtop,
        SourceIn,
        DestinationOut,
        DestinationIn,
        DestinationOver,
        Copy,
    };
    struct Transform
    {
        double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    };
    // A path as the context records it: for each subpath, RECT x y w h, or
    // MOVE x y followed by LINE x y ... (CLOSE optional; fills close anyway).
    enum PathOp
    {
        RECT  = 0,
        MOVE  = 1,
        LINE  = 2,
        CLOSE = 3,
    };

    PixelCanvas(int width, int height);

    // Resizing clears the pixels and resets the state, as setting a canvas'
    // width or height does.
    void resize(int width, int height);
    int width() const { return w; }
    int height() const { return h; }
    const uint8_t* data() const { return px.data(); }

    // A CSS colour (#rgb, #rgba, #rrggbb, #rrggbbaa, rgb(), rgba(), hsl(),
    // hsla(), a few names). False, and the fill unchanged, if it isn't one.
    bool setFill(const std::string& css);
    void setAlpha(double alpha);
    bool setOp(const std::string& name);
    void save();
    void restore();
    void setTransform(const Transform& t);
    void transform(const Transform& t); // multiplied onto the current one

    void fillRect(double x, double y, double w, double h);
    void clearRect(double x, double y, double w, double h);
    void fillPath(const double* ops, size_t n);
    void clipPath(const double* ops, size_t n);
    // sx..sh in `src`'s pixels, dx..dh in user space.
    void drawImage(const PixelCanvas& src, double sx, double sy, double sw, double sh, double dx, double dy,
                   double dw, double dh);
    // Straight (not premultiplied) RGBA, as ImageData has it; outside the
    // canvas reads as transparent.
    void getImageData(int x, int y, int w, int h, uint8_t* out) const;
    // `in` is iw x ih straight RGBA; only its dirty rectangle is written, at
    // (dx, dy) plus its offset, ignoring the transform, clip, alpha and op.
    void putImageData(const uint8_t* in, int iw, int ih, int dx, int dy, int dirtyX, int dirtyY, int dirtyW,
                      int dirtyH);

  private:
    struct State
    {
        uint8_t fill[4] = { 0, 0, 0, 255 }; // straight RGBA
        double alpha    = 1;
        Op op           = Op::SourceOver;
        Transform t;
        std::shared_ptr<std::vector<uint8_t>> clip; // w*h coverage, or none
    };
    struct Span
    {
        int y, x0, x1; // pixels [x0, x1) of row y
    };

    // The pixels a path covers, row by row.
    void cover(const double* ops, size_t n, std::vector<Span>& out) const;
    void coverRect(double x, double y, double w, double h, std::vector<Span>& out) const;
    void coverPolygon(const std::vector<double>& pts, const std::vector<size_t>& starts, std::vector<Span>& out) const;
    void paint(const std::vector<Span>& spans);
    // Blend one premultiplied source pixel into dst under the state's op.
    void blend(uint8_t* dst, const uint8_t* src, uint8_t clip) const;

    int w = 0, h = 0;
    std::vector<uint8_t> px;
    State st;
    std::vector<State> stack;
    mutable std::vector<Span> spans; // scratch
};

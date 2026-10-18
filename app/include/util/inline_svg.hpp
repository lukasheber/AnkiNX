#pragma once

#include <string>

// litehtml draws SVG only as image files (<img src="x.svg">, via lunasvg),
// but card templates often embed icons as inline <svg> elements. Write each
// inline SVG to `cacheDir` (named by its content's hash) and put an <img> in
// its place with the same size, id, class, style and data-ankinx-node.
// <script> and <style> contents are left alone (templates keep SVG markup in
// script strings, e.g. to insert later).
std::string inlineSvgToImages(const std::string& html, const std::string& cacheDir);

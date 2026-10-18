#include "util/inline_svg.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sys/stat.h>

namespace
{

bool startsWithTag(const std::string& html, size_t at, const char* tag)
{
    size_t len = std::char_traits<char>::length(tag);
    if (at + 1 + len > html.size() || html[at] != '<')
        return false;
    for (size_t i = 0; i < len; i++)
        if (std::tolower((unsigned char)html[at + 1 + i]) != tag[i])
            return false;
    char next = at + 1 + len < html.size() ? html[at + 1 + len] : '>';
    return std::isspace((unsigned char)next) || next == '>' || next == '/';
}

size_t findCaseless(const std::string& html, const std::string& needle, size_t from)
{
    for (size_t i = from; i + needle.size() <= html.size(); i++)
    {
        size_t k = 0;
        while (k < needle.size() && std::tolower((unsigned char)html[i + k]) == needle[k])
            k++;
        if (k == needle.size())
            return i;
    }
    return std::string::npos;
}

// Attribute of the element whose start tag is `tag` ("<svg a=... >").
std::string attribute(const std::string& tag, const std::string& name)
{
    for (size_t at = findCaseless(tag, name, 0); at != std::string::npos; at = findCaseless(tag, name, at + 1))
    {
        if (at == 0 || !std::isspace((unsigned char)tag[at - 1]))
            continue;
        size_t i = at + name.size();
        while (i < tag.size() && std::isspace((unsigned char)tag[i]))
            i++;
        if (i >= tag.size() || tag[i] != '=')
            continue;
        i++;
        while (i < tag.size() && std::isspace((unsigned char)tag[i]))
            i++;
        char quote = i < tag.size() ? tag[i] : 0;
        if (quote == '"' || quote == '\'')
        {
            size_t end = tag.find(quote, i + 1);
            return end == std::string::npos ? "" : tag.substr(i + 1, end - i - 1);
        }
        size_t end = i;
        while (end < tag.size() && !std::isspace((unsigned char)tag[end]) && tag[end] != '>')
            end++;
        return tag.substr(i, end - i);
    }
    return "";
}

// "50", "50px" -> "50px"; other units kept; empty stays empty.
std::string cssLength(const std::string& value)
{
    if (value.empty())
        return "";
    bool number = std::all_of(value.begin(), value.end(), [](char c) { return std::isdigit((unsigned char)c) || c == '.'; });
    return number ? value + "px" : value;
}

void makeDirs(const std::string& path)
{
    for (size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1))
        mkdir(path.substr(0, at).c_str(), 0777);
    mkdir(path.c_str(), 0777);
}

std::string escapeAttr(const std::string& s)
{
    std::string out;
    for (char c : s)
        out += c == '"' ? std::string("&quot;") : std::string(1, c);
    return out;
}

std::string toImage(const std::string& svg, const std::string& cacheDir)
{
    size_t tagEnd    = svg.find('>');
    std::string tag  = svg.substr(0, tagEnd + 1);
    std::string body = svg;
    // lunasvg needs the SVG namespace; inline SVG in HTML may omit it.
    if (findCaseless(tag, "xmlns=", 0) == std::string::npos)
        body.insert(4, " xmlns=\"http://www.w3.org/2000/svg\"");

    char name[32];
    std::snprintf(name, sizeof name, "%016zx.svg", std::hash<std::string> {}(body));
    std::string path = cacheDir + "/" + name;
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
    {
        makeDirs(cacheDir);
        std::ofstream(path, std::ios::binary) << body;
    }

    std::string style;
    if (std::string w = cssLength(attribute(tag, "width")); !w.empty())
        style += "width: " + w + "; ";
    if (std::string h = cssLength(attribute(tag, "height")); !h.empty())
        style += "height: " + h + "; ";
    if (style.empty())
        style = "width: 1em; height: 1em; ";
    style += attribute(tag, "style");

    std::string img = "<img src=\"" + path + "\" style=\"" + escapeAttr(style) + "\"";
    for (const char* keep : { "id", "class", "data-ankinx-node" })
        if (std::string v = attribute(tag, keep); !v.empty())
            img += std::string(" ") + keep + "=\"" + escapeAttr(v) + "\"";
    return img + ">";
}

} // namespace

std::string inlineSvgToImages(const std::string& html, const std::string& cacheDir)
{
    if (findCaseless(html, "<svg", 0) == std::string::npos)
        return html;
    std::string out;
    size_t i = 0;
    while (i < html.size())
    {
        size_t lt = html.find('<', i);
        if (lt == std::string::npos)
        {
            out.append(html, i, std::string::npos);
            break;
        }
        out.append(html, i, lt - i);
        // Script and style text is copied as is.
        bool raw = startsWithTag(html, lt, "script") || startsWithTag(html, lt, "style");
        if (raw)
        {
            std::string close = startsWithTag(html, lt, "script") ? "</script" : "</style";
            size_t end        = findCaseless(html, close, lt + 1);
            end               = end == std::string::npos ? html.size() : html.find('>', end);
            end               = end == std::string::npos ? html.size() : end + 1;
            out.append(html, lt, end - lt);
            i = end;
            continue;
        }
        if (startsWithTag(html, lt, "svg"))
        {
            // The matching </svg>, allowing nested <svg>.
            int depth  = 0;
            size_t pos = lt;
            size_t end = std::string::npos;
            while (pos < html.size())
            {
                size_t open  = findCaseless(html, "<svg", pos + 1);
                size_t close = findCaseless(html, "</svg", pos + 1);
                if (close == std::string::npos)
                    break;
                if (open != std::string::npos && open < close)
                {
                    depth++;
                    pos = open;
                    continue;
                }
                if (depth == 0)
                {
                    end = html.find('>', close);
                    break;
                }
                depth--;
                pos = close;
            }
            if (end != std::string::npos)
            {
                out += toImage(html.substr(lt, end + 1 - lt), cacheDir);
                i = end + 1;
                continue;
            }
        }
        out += '<';
        i = lt + 1;
    }
    return out;
}

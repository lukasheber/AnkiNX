#include "anki/api.hpp"

#include "anki_switch_core.h"

#include <cstdlib>

namespace anki
{

std::string dataDir()
{
#ifdef __SWITCH__
    return "/switch/ankinx";
#else
    const char* dir = std::getenv("ANKINX_DATA_DIR");
    return dir && *dir ? dir : "ankinx-data";
#endif
}

json call(const std::string& cmd, const json& args)
{
    const std::string input = args.dump();
    char* output            = ankinx_call(cmd.c_str(), input.c_str());
    json response           = json::parse(output ? output : "", nullptr, false);
    ankinx_free(output);

    if (response.is_discarded() || !response.is_object())
        throw Error("Invalid response from the Anki backend", "backend");
    if (response.contains("ok"))
        return response["ok"];
    throw Error(response.value("error", "Unknown error"), response.value("kind", "backend"));
}

} // namespace anki

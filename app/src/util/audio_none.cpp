// Silent audio backend for builds without SDL2_mixer (ANKINX_AUDIO=OFF).
#include "util/audio.hpp"

namespace audio
{
void play(const std::vector<std::string>&) { }
void stop() { }
void shutdown() { }
} // namespace audio

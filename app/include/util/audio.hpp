#pragma once

#include <string>
#include <vector>

// Plays card audio ([sound:...] files) with SDL2_mixer: MP3, OGG/Vorbis,
// Opus, WAV and tracker formats. Files are played one after another.
namespace audio
{

// Replace the queue with `paths` and start playing. Missing files are skipped.
void play(const std::vector<std::string>& paths);
void stop();
// Release the audio device (call before exit).
void shutdown();

} // namespace audio

#include "util/audio.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <atomic>
#include <borealis.hpp>
#include <cstdint>
#include <deque>
#include <memory>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

// Two decoders, tried in order for each file:
//  1. SDL2_mixer: MP3 (mpg123), OGG/Vorbis, Opus, WAV, tracker formats. Streams.
//  2. FFmpeg: everything else Anki decks contain, notably M4A/AAC, FLAC and the
//     audio track of WebM/MP4. Decoded fully to PCM (card clips are short) on a
//     worker thread, then played as a Mix_Chunk.
namespace audio
{

static void playNextFromQueue();
static void onFinished();

namespace
{

bool initialized   = false;
bool available     = false;
int outRate        = 48000;
int outChannels    = 2;
Mix_Music* music   = nullptr;
Mix_Chunk* chunk   = nullptr;
std::shared_ptr<std::vector<int16_t>> pcm; // backing store of `chunk`
std::deque<std::string> queue;
// Bumped by stop()/play(): decodes that finish late for an old queue are
// dropped, and so are "finished" notices of a sound that has been replaced
// (read on SDL's audio thread, hence atomic).
std::atomic<unsigned> generation { 0 };

// Upper bound for fully decoded clips: 10 minutes.
constexpr size_t MAX_SAMPLES = 10 * 60 * 48000 * 2;

bool ensureInit()
{
    if (initialized)
        return available;
    initialized = true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        brls::Logger::error("audio: SDL_InitSubSystem failed: {}", SDL_GetError());
        return false;
    }
    int flags = Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_OPUS | MIX_INIT_MOD);
    brls::Logger::info("audio: SDL_mixer decoders mp3={} ogg={} opus={} mod={}", !!(flags & MIX_INIT_MP3),
                       !!(flags & MIX_INIT_OGG), !!(flags & MIX_INIT_OPUS), !!(flags & MIX_INIT_MOD));
    if (Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 2048) != 0)
    {
        brls::Logger::error("audio: Mix_OpenAudio failed: {}", Mix_GetError());
        return false;
    }
    Uint16 format;
    Mix_QuerySpec(&outRate, &format, &outChannels);
    Mix_HookMusicFinished(onFinished);
    Mix_ChannelFinished([](int) { onFinished(); });
    av_log_set_level(AV_LOG_ERROR);
    available = true;
    return true;
}

void freeCurrent()
{
    if (music)
    {
        Mix_FreeMusic(music);
        music = nullptr;
    }
    if (chunk)
    {
        Mix_FreeChunk(chunk); // QuickLoad chunk: does not free `pcm`
        chunk = nullptr;
    }
    pcm.reset();
}

// Decode `path` to interleaved signed 16-bit PCM at the mixer's output format.
std::shared_ptr<std::vector<int16_t>> ffmpegDecode(const std::string& path, int rate, int channels)
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
        return nullptr;
    std::unique_ptr<AVFormatContext*, void (*)(AVFormatContext**)> fmtGuard(&fmt, avformat_close_input);
    if (avformat_find_stream_info(fmt, nullptr) < 0)
        return nullptr;

    const AVCodec* codec = nullptr;
    int stream           = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream < 0 || !codec)
        return nullptr;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    std::unique_ptr<AVCodecContext*, void (*)(AVCodecContext**)> ctxGuard(&ctx, avcodec_free_context);
    if (!ctx || avcodec_parameters_to_context(ctx, fmt->streams[stream]->codecpar) < 0 ||
        avcodec_open2(ctx, codec, nullptr) < 0)
        return nullptr;

    SwrContext* swr = nullptr;
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, channels);
    if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, rate, &ctx->ch_layout, ctx->sample_fmt,
                            ctx->sample_rate, 0, nullptr) < 0 ||
        swr_init(swr) < 0)
    {
        swr_free(&swr);
        return nullptr;
    }
    std::unique_ptr<SwrContext*, void (*)(SwrContext**)> swrGuard(&swr, swr_free);

    auto out = std::make_shared<std::vector<int16_t>>();
    auto convert = [&](const uint8_t** in, int inSamples) {
        int maxOut = swr_get_out_samples(swr, inSamples);
        if (maxOut <= 0)
            return;
        size_t old = out->size();
        out->resize(old + (size_t)maxOut * channels);
        uint8_t* dst = (uint8_t*)(out->data() + old);
        int got      = swr_convert(swr, &dst, maxOut, in, inSamples);
        out->resize(old + (size_t)std::max(got, 0) * channels);
    };

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame   = av_frame_alloc();
    auto drain = [&] {
        while (avcodec_receive_frame(ctx, frame) == 0)
        {
            convert((const uint8_t**)frame->extended_data, frame->nb_samples);
            av_frame_unref(frame);
        }
    };
    while (out->size() < MAX_SAMPLES && av_read_frame(fmt, packet) >= 0)
    {
        if (packet->stream_index == stream && avcodec_send_packet(ctx, packet) == 0)
            drain();
        av_packet_unref(packet);
    }
    avcodec_send_packet(ctx, nullptr); // flush the decoder
    drain();
    convert(nullptr, 0); // flush the resampler
    av_frame_free(&frame);
    av_packet_free(&packet);
    return out->empty() ? nullptr : out;
}

} // namespace

// Runs on SDL's audio thread, where SDL_mixer must not be called: hop to the UI thread.
// A sound that ends just before play() starts another posts this first; without
// the generation check it would free the new sound as soon as it started.
static void onFinished()
{
    unsigned gen = generation.load();
    brls::sync([gen] {
        if (gen == generation.load())
            playNextFromQueue();
    });
}

static void playNextFromQueue()
{
    freeCurrent();
    if (queue.empty())
        return;
    std::string path = queue.front();
    queue.pop_front();

    music = Mix_LoadMUS(path.c_str());
    if (music && Mix_PlayMusic(music, 0) == 0)
        return;
    freeCurrent();

    // Not a format SDL_mixer handles: decode with FFmpeg off the UI thread.
    unsigned gen = generation;
    int rate = outRate, channels = outChannels;
    brls::async([path, gen, rate, channels] {
        auto decoded = ffmpegDecode(path, rate, channels);
        brls::sync([path, gen, decoded] {
            if (gen != generation)
                return; // stopped or replaced meanwhile
            if (!decoded)
            {
                brls::Logger::warning("audio: cannot decode {}", path);
                playNextFromQueue();
                return;
            }
            pcm   = decoded;
            chunk = Mix_QuickLoad_RAW((Uint8*)pcm->data(), (Uint32)(pcm->size() * sizeof(int16_t)));
            if (!chunk || Mix_PlayChannel(0, chunk, 0) < 0)
                playNextFromQueue();
        });
    });
}

void play(const std::vector<std::string>& paths)
{
    if (!ensureInit())
        return;
    stop();
    queue.assign(paths.begin(), paths.end());
    playNextFromQueue();
}

void stop()
{
    generation++;
    queue.clear();
    if (available)
    {
        // Halting fires the finished hooks; unhook so they can't advance a new queue.
        Mix_HookMusicFinished(nullptr);
        Mix_ChannelFinished(nullptr);
        Mix_HaltMusic();
        Mix_HaltChannel(-1);
        Mix_HookMusicFinished(onFinished);
        Mix_ChannelFinished([](int) { onFinished(); });
    }
    freeCurrent();
}

void shutdown()
{
    if (!available)
        return;
    stop();
    Mix_HookMusicFinished(nullptr);
    Mix_ChannelFinished(nullptr);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    available = false;
}

} // namespace audio

// Audio output through SDL3 audio streams (WASAPI on Windows, PipeWire/PulseAudio/ALSA on
// Linux). The AI's DMA blocks go to one stream; each Wii Remote speaker gets its own.
#include "audio_out.h"
#include "../log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

static bool s_ok = false;
static SDL_AudioStream* s_stream = nullptr;
static uint32_t s_rate = 0;
static std::vector<int16_t> s_staging;
static constexpr uint32_t BATCH_FRAMES_PER_SEC_DIV = 100;   // 10 ms batches
static constexpr uint32_t MAX_QUEUED_BATCHES = 8;            // more than this is dropped (latency)
float g_audioVolume = 1.0f;
int g_audioPeak = 0;                           // max |sample| since last status line
int g_audioUnderruns = 0, g_audioDrops = 0;   // since last status line
uint64_t g_audioFrames = 0;                    // stereo frames produced (total)
int g_audioRepeats = 0;                        // AI blocks identical to the previous one
FILE* g_audioDump = nullptr;   // --dump-audio: raw interleaved s16 stereo (host order)
bool g_audioMute = false;      // --mute (test runs)

static SDL_AudioStream* OpenStream(int channels, uint32_t rate)
{
    SDL_AudioSpec spec{ SDL_AUDIO_S16, channels, (int)rate };
    SDL_AudioStream* st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!st)
    {
        LOG_ERROR("SDL audio: cannot open a {} Hz stream: {}", rate, SDL_GetError());
        return nullptr;
    }
    if (g_audioMute) SDL_SetAudioStreamGain(st, 0.0f);
    SDL_ResumeAudioStreamDevice(st);
    return st;
}

void AudioOut_Init()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        LOG_ERROR("SDL audio init failed ({}); audio disabled", SDL_GetError());
        return;
    }
    s_ok = true;
    LOG_INFO("SDL audio ({}) initialised{}", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
             g_audioMute ? " (muted)" : "");
}

void AudioOut_PushDSP(const uint8_t* data, uint32_t bytes, uint32_t sampleRate)
{
    if (!s_ok) return;
    if (!s_stream || s_rate != sampleRate)
    {
        if (s_stream) SDL_DestroyAudioStream(s_stream);
        s_stream = OpenStream(2, sampleRate);
        s_rate = sampleRate;
        s_staging.clear();
    }
    if (!s_stream) return;
    uint32_t frames = bytes / 4;
    g_audioFrames += frames;
    // DMA data is big-endian, right channel first
    for (uint32_t i = 0; i < frames; i++)
    {
        int16_t r = (int16_t)((data[4 * i] << 8) | data[4 * i + 1]);
        int16_t l = (int16_t)((data[4 * i + 2] << 8) | data[4 * i + 3]);
        g_audioPeak = std::max(g_audioPeak, std::max(std::abs((int)l), std::abs((int)r)));
        s_staging.push_back((int16_t)(l * g_audioVolume));
        s_staging.push_back((int16_t)(r * g_audioVolume));
        if (g_audioDump) { int16_t lr[2] = { l, r }; fwrite(lr, 2, 2, g_audioDump); }
    }
    uint32_t batch = sampleRate / BATCH_FRAMES_PER_SEC_DIV;
    if (s_staging.size() / 2 < batch) return;

    int queuedFrames = SDL_GetAudioStreamQueued(s_stream) / 4;
    if (queuedFrames <= 0)
    {
        // ran dry: rebuild a 30 ms cushion before resuming so jitter is absorbed
        if (s_staging.size() / 2 < batch * 3) return;
        g_audioUnderruns++;
    }
    if ((uint32_t)queuedFrames > batch * MAX_QUEUED_BATCHES)
    {
        g_audioDrops++;   // too much latency: drop this batch
        s_staging.clear();
        return;
    }
    SDL_PutAudioStreamData(s_stream, s_staging.data(), (int)(s_staging.size() * sizeof(int16_t)));
    s_staging.clear();
}

// ---------------------------------------------------------------- Wii Remote speakers
// The SDK configures the speaker for 4-bit ADPCM at 12 MHz / 2000 = 6000 Hz.
static constexpr uint32_t SPEAKER_RATE = 6000;
float g_speakerVolume = 0.6f;   // the remote speaker is quieter than the TV
static SDL_AudioStream* s_spk[4];
static std::vector<int16_t> s_spkStaging[4];

void AudioOut_PushSpeaker(int chan, const int16_t* pcm, uint32_t samples)
{
    if (!s_ok || chan < 0 || chan > 3) return;
    if (!s_spk[chan] && !(s_spk[chan] = OpenStream(1, SPEAKER_RATE))) return;
    if (g_audioDump)
    {
        static FILE* f = fopen("speaker_dump.raw", "wb");
        if (f) { fwrite(pcm, 2, samples, f); fflush(f); }
    }
    auto& st = s_spkStaging[chan];
    for (uint32_t i = 0; i < samples; i++) st.push_back((int16_t)(pcm[i] * g_speakerVolume * g_audioVolume));
    if (st.size() < SPEAKER_RATE / 50) return;   // 20 ms batches
    if (SDL_GetAudioStreamQueued(s_spk[chan]) / 2 > (int)(SPEAKER_RATE / 50 * 6))
    {
        st.clear();
        return;
    }
    SDL_PutAudioStreamData(s_spk[chan], st.data(), (int)(st.size() * sizeof(int16_t)));
    st.clear();
}

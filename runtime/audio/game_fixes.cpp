// Fixes for bugs in the game itself (each one switchable in config.ini).
//
// Music in the rear channel: the Wii SDK's MIXInitChannel takes an auxC send before pan and
// span, but ENGCAudioStreamer's constructor still passes the GameCube argument order
// (input, auxA, auxB = -90.4 dB, then pan, span 127 = front, fader 0). Shifted by one, its
// pan lands in auxC, 127 in pan and 0 in span, so every music voice is mixed fully into the
// surround channel. The game always mixes for Dolby Pro Logic II (ENgcAudio::InitAudio), so on
// stereo the music plays in antiphase (left = -right): hollow, and cancelling out in mono.
// The sound effect streamer passes the Wii order (pan 64, span 127).
#include "../config.h"
#include "../game_addrs.h"
#include "../log.h"

PPC_OVERRIDE(MIXInitChannel)
{
    // r3 voice, r4 mode, r5 input, r6 auxA, r7 auxB, r8 auxC, r9 pan, r10 span, fader on the stack
    uint32_t ctor = GameAddr(GA::MusicStreamerCtor);
    if (g_config.musicFront && ctor && ctx.lr - ctor < 0xF4)
    {
        static bool logged = false;
        if (!logged) LOG_INFO("music mixer arguments fixed (front, pan {})", ctx.r[8]);
        logged = true;
        ctx.r[9] = ctx.r[8];        // the stream's pan
        ctx.r[8] = (uint32_t)-904;  // no auxC send
        ctx.r[10] = 127;            // front
    }
    PPC_ORIGINAL(MIXInitChannel);
}

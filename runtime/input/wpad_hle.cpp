// Wii Remote library (WPAD) replaced at the API level: one Wii Remote is always connected
// on channel 0. KPAD still runs recompiled on top of it.
//
// The remote has a Nunchuk (the game asks for one) but carries only the mouse: the pointer goes straight into the game's
// EWiiController::GetControllerCursorPosition (no IR camera simulation), the left and
// right mouse buttons are A and B. Everything else comes through the GameCube controller
// port (pad_hle.cpp). Speaker, sync and LED requests are accepted and ignored.
#include "input.h"
#include "../game_addrs.h"
#include "../kernel/memory.h"
#include "../log.h"

namespace
{
constexpr int kChannels = 4;
constexpr int kRemote = 0;   // the connected channel

constexpr int32_t WPAD_ERR_NONE = 0, WPAD_ERR_NO_CONTROLLER = -1;
constexpr uint32_t WPAD_STATE_SETUP = 3;
constexpr uint32_t WPAD_DEV_FREESTYLE = 1, WPAD_DEV_NOT_FOUND = 253;   // FREESTYLE: with a Nunchuk
constexpr uint16_t WPAD_BUTTON_B = 0x0400, WPAD_BUTTON_A = 0x0800;

struct Channel
{
    uint32_t samplingCb = 0;
    uint32_t connectCb = 0;
    uint32_t extensionCb = 0;
    uint32_t format = 0;
    bool dpd = false;
};
Channel s_chan[kChannels];
bool s_motorEnabled = true;
uint8_t s_speakerVolume = 0x40;

bool Connected(uint32_t chan) { return chan == kRemote; }

// Calls a guest function with up to three arguments; the caller's registers are kept.
void GuestCall(PPCContext& ctx, uint8_t* base, uint32_t fn, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0)
{
    if (!fn) return;
    PPCContext saved = ctx;
    ctx.r[3] = a0;
    ctx.r[4] = a1;
    ctx.r[5] = a2;
    PPC_CALL_INDIRECT(fn);
    ctx = saved;
}

// The remote carries the same buttons as the GameCube port: the safety, splash and system
// screens (and the HOME menu) only read Wii Remotes. The game itself plays on the GameCube
// port (pad_hle.cpp maps the player there), so nothing is pressed twice.
uint16_t RemoteButtons()
{
    static const struct { uint32_t btn; uint16_t wii; } kMap[] = {
        { BTN_A, 0x0800 }, { BTN_B, 0x0400 }, { BTN_1, 0x0200 }, { BTN_2, 0x0100 }, { BTN_PLUS, 0x0010 },
        { BTN_MINUS, 0x1000 }, { BTN_HOME, 0x8000 }, { BTN_Z, 0x2000 }, { BTN_C, 0x4000 },
        { BTN_LEFT, 0x0001 }, { BTN_RIGHT, 0x0002 }, { BTN_DOWN, 0x0004 }, { BTN_UP, 0x0008 },
    };
    uint32_t pad = Input_Pad().buttons;
    uint16_t wii = 0;
    for (auto& m : kMap)
        if (pad & m.btn) wii |= m.wii;
    return wii;
}

uint32_t SetCallback(uint32_t& slot, uint32_t cb)
{
    uint32_t old = slot;
    slot = cb;
    return old;
}
}   // namespace

PPC_OVERRIDE(WPADInit) {}
PPC_OVERRIDE(WPADGetStatus) { ctx.r[3] = WPAD_STATE_SETUP; }

// s32 WPADProbe(s32 chan, u32* type)
PPC_OVERRIDE(WPADProbe)
{
    uint32_t chan = ctx.r[3], type = ctx.r[4];
    bool on = chan < kChannels && Connected(chan);
    if (type) Write32(type, on ? WPAD_DEV_FREESTYLE : WPAD_DEV_NOT_FOUND);
    ctx.r[3] = (uint32_t)(on ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
}

// callback setters return the previous callback
PPC_OVERRIDE(WPADSetSamplingCallback) { ctx.r[3] = ctx.r[3] < kChannels ? SetCallback(s_chan[ctx.r[3]].samplingCb, ctx.r[4]) : 0; }
PPC_OVERRIDE(WPADSetConnectCallback) { ctx.r[3] = ctx.r[3] < kChannels ? SetCallback(s_chan[ctx.r[3]].connectCb, ctx.r[4]) : 0; }
PPC_OVERRIDE(WPADSetExtensionCallback)
{
    LOG_DEBUG("WPAD: channel {} extension callback {:08x}", ctx.r[3], ctx.r[4]);
    ctx.r[3] = ctx.r[3] < kChannels ? SetCallback(s_chan[ctx.r[3]].extensionCb, ctx.r[4]) : 0;
}

// s32 WPADSetDataFormat(s32 chan, u32 format), u32 WPADGetDataFormat(s32 chan)
PPC_OVERRIDE(WPADSetDataFormat)
{
    LOG_DEBUG("WPAD: channel {} data format {}", ctx.r[3], ctx.r[4]);
    if (ctx.r[3] < kChannels) s_chan[ctx.r[3]].format = ctx.r[4];
    ctx.r[3] = WPAD_ERR_NONE;
}
PPC_OVERRIDE(WPADGetDataFormat) { ctx.r[3] = ctx.r[3] < kChannels ? s_chan[ctx.r[3]].format : 0; }

// void WPADRead(s32 chan, WPADStatus* status). WPADStatus: u16 button; s16 acc x, y, z;
// DPDObject obj[4] (s16 x, y; u16 size; u8 traceId; pad); u8 dev; s8 err. The Nunchuk
// formats (3..5) append s16 fsAcc x, y, z and s8 fsStick x, y.
PPC_OVERRIDE(WPADRead)
{
    uint32_t chan = ctx.r[3], s = ctx.r[4];
    if (chan >= kChannels || !s) return;
    uint32_t fmt = s_chan[chan].format;
    uint32_t size = fmt >= 3 && fmt <= 5 ? 50 : 42;
    memset(GuestPtr(s), 0, size);
    if (!Connected(chan))
    {
        Write8(s + 40, (uint8_t)WPAD_DEV_NOT_FOUND);
        Write8(s + 41, (uint8_t)WPAD_ERR_NO_CONTROLLER);
        return;
    }
    Write16(s + 0, RemoteButtons());
    // lying flat, face up: 1 g on +Z (KPAD's gravity unit is 100 counts)
    Write16(s + 6, 100);
    for (int i = 0; i < 4; i++)
    {
        uint32_t o = s + 8 + i * 8;
        Write16(o + 0, 0x3FF);
        Write16(o + 2, 0x3FF);
    }
    Write8(s + 40, (uint8_t)WPAD_DEV_FREESTYLE);
    Write8(s + 41, (uint8_t)WPAD_ERR_NONE);
    if (size == 50)
        Write16(s + 46, 100);   // Nunchuk lying flat too; its stick stays centred
}

// s32 WPADControlDpd(s32 chan, u32 command, WPADCallback cb), BOOL WPADIsDpdEnabled(s32 chan)
PPC_OVERRIDE(WPADControlDpd)
{
    uint32_t chan = ctx.r[3], cb = ctx.r[5];
    if (chan < kChannels) s_chan[chan].dpd = ctx.r[4] != 0;
    int32_t res = chan < kChannels && Connected(chan) ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
    GuestCall(ctx, base, cb, chan, (uint32_t)res);
    ctx.r[3] = (uint32_t)res;
}
PPC_OVERRIDE(WPADIsDpdEnabled) { ctx.r[3] = ctx.r[3] < kChannels && s_chan[ctx.r[3]].dpd; }
PPC_OVERRIDE(WPADGetSensorBarPosition) { ctx.r[3] = 0; }   // below the screen
PPC_OVERRIDE(WPADGetDpdSensitivity) { ctx.r[3] = 3; }

// void WPADGetAccGravityUnit(s32 chan, u32 type, WPADAcc* acc): s16 x, y, z
PPC_OVERRIDE(WPADGetAccGravityUnit)
{
    uint32_t acc = ctx.r[5];
    if (!acc) return;
    Write16(acc + 0, 100);
    Write16(acc + 2, 100);
    Write16(acc + 4, 100);
}

// void WPADControlMotor(s32 chan, u32 command), void WPADEnableMotor(BOOL), BOOL WPADIsMotorEnabled(void).
// The Wii Remote only carries the pointer; rumble goes to the gamepad from the PAD side.
PPC_OVERRIDE(WPADControlMotor) {}
PPC_OVERRIDE(WPADEnableMotor) { s_motorEnabled = ctx.r[3] != 0; }
PPC_OVERRIDE(WPADIsMotorEnabled) { ctx.r[3] = s_motorEnabled; }

// s32 WPADGetInfoAsync(s32 chan, WPADInfo* info, WPADCallback cb). WPADInfo: BOOL dpd,
// speaker, attach, lowBat, nearempty; u8 battery, led, protocol, firmware.
PPC_OVERRIDE(WPADGetInfoAsync)
{
    uint32_t chan = ctx.r[3], info = ctx.r[4], cb = ctx.r[5];
    if (chan >= kChannels || !Connected(chan))
    {
        ctx.r[3] = (uint32_t)WPAD_ERR_NO_CONTROLLER;
        return;
    }
    if (info)
    {
        memset(GuestPtr(info), 0, 24);
        Write32(info + 0, s_chan[chan].dpd);
        Write32(info + 8, 1);      // extension (the Nunchuk) attached
        Write8(info + 20, 4);      // battery full
        Write8(info + 21, 1 << chan);   // player LED
    }
    GuestCall(ctx, base, cb, chan, (uint32_t)WPAD_ERR_NONE);
    ctx.r[3] = WPAD_ERR_NONE;
}

PPC_OVERRIDE(WPADDisconnect) {}
PPC_OVERRIDE(WPADGetRadioSensitivity) { ctx.r[3] = 100; }
PPC_OVERRIDE(WPADSaveConfig) { ctx.r[3] = 1; }
PPC_OVERRIDE(WPADControlLed) { GuestCall(ctx, base, ctx.r[5], ctx.r[3], WPAD_ERR_NONE); ctx.r[3] = WPAD_ERR_NONE; }

// Speaker: no sound is sent to the remote
PPC_OVERRIDE(WPADCanSendStreamData) { ctx.r[3] = 0; }
PPC_OVERRIDE(WPADSendStreamData) { ctx.r[3] = WPAD_ERR_NONE; }
PPC_OVERRIDE(WPADIsSpeakerEnabled) { ctx.r[3] = 0; }
PPC_OVERRIDE(WPADControlSpeaker) { GuestCall(ctx, base, ctx.r[5], ctx.r[3], WPAD_ERR_NONE); ctx.r[3] = WPAD_ERR_NONE; }
PPC_OVERRIDE(WPADGetSpeakerVolume) { ctx.r[3] = s_speakerVolume; }
PPC_OVERRIDE(WPADSetSpeakerVolume) { s_speakerVolume = (uint8_t)ctx.r[3]; }

// Pairing (the HOME menu's SYNC handling): nothing to pair
PPC_OVERRIDE(WPADSetSimpleSyncCallback) { ctx.r[3] = 0; }
PPC_OVERRIDE(WPADStartFastSimpleSync) { ctx.r[3] = 0; }
PPC_OVERRIDE(WPADStopSimpleSync) { ctx.r[3] = 0; }

// s32 KPADRead(s32 chan, KPADStatus* samples, u32 length): on hardware WPAD hands KPAD a
// new sample every few milliseconds through the sampling callback; here one sample per read.
PPC_OVERRIDE(KPADRead)
{
    uint32_t chan = ctx.r[3];
    if (chan < kChannels && Connected(chan))
    {
        Input_Poll();
        GuestCall(ctx, base, s_chan[chan].samplingCb, chan);
    }
    PPC_ORIGINAL(KPADRead);
}

// void EWiiController::GetControllerCursorPosition(const KPADStatus&, float* x, float* y):
// the pointer in KPAD's -1..1 screen coordinates
PPC_OVERRIDE(WiiCursorPosition)
{
    PointerState p = Input_Pointer();
    float x = p.valid ? p.x : kPointerAway, y = p.valid ? p.y : kPointerAway;
    Write32(ctx.r[5], std::bit_cast<uint32_t>(x));
    Write32(ctx.r[6], std::bit_cast<uint32_t>(y));
}

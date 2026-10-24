// GameCube controller library (PAD) replaced at the API level.
//
// The game polls a GameCube controller on port 4 through ENgcControllerManager (PADRead
// every frame) and maps it like a Wii Remote through its EController command maps. That
// port is fed from the PC's gamepads and keyboard (input.cpp); the other ports report no
// controller.
#include "input.h"
#include "../game_addrs.h"
#include "../kernel/memory.h"

namespace
{
constexpr int kPort = 3;   // ENgcControllerManager reads channel 3 (port 4)
constexpr int8_t PAD_ERR_NONE = 0, PAD_ERR_NO_CONTROLLER = -1;
constexpr uint32_t kStatusSize = 12;
}   // namespace

// BOOL PADInit(void)
PPC_OVERRIDE(PADInit)
{
    Input_Init();
    ctx.r[3] = 1;
}

// u32 PADRead(PADStatus status[4]): {u16 button; s8 stickX, stickY, substickX, substickY;
// u8 triggerL, triggerR, analogA, analogB; s8 err; pad}
PPC_OVERRIDE(PADRead)
{
    Input_Poll();
    uint32_t out = ctx.r[3];
    PadState p = Input_Pad();
    for (int chan = 0; chan < 4; chan++)
    {
        uint32_t s = out + chan * kStatusSize;
        memset(GuestPtr(s), 0, kStatusSize);
        if (chan != kPort || !p.connected)
        {
            Write8(s + 10, (uint8_t)PAD_ERR_NO_CONTROLLER);
            continue;
        }
        Write16(s + 0, p.buttons);
        Write8(s + 2, (uint8_t)p.stickX);
        Write8(s + 3, (uint8_t)p.stickY);
        Write8(s + 4, (uint8_t)p.substickX);
        Write8(s + 5, (uint8_t)p.substickY);
        Write8(s + 6, p.triggerL);
        Write8(s + 7, p.triggerR);
        Write8(s + 10, (uint8_t)PAD_ERR_NONE);
    }
    ctx.r[3] = 0;   // no channel needs a reset
}

// BOOL PADReset(u32 mask), BOOL PADRecalibrate(u32 mask): nothing to do
PPC_OVERRIDE(PADReset) { ctx.r[3] = 1; }
PPC_OVERRIDE(PADRecalibrate) { ctx.r[3] = 1; }

// void PADControlMotor(s32 chan, u32 command): 0 stop, 1 rumble, 2 stop hard
PPC_OVERRIDE(PADControlMotor)
{
    if ((int32_t)ctx.r[3] == kPort) Input_SetRumble(ctx.r[4] == 1);
}

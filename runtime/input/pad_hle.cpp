// GameCube controller library (PAD) replaced at the API level.
//
// The game polls a GameCube controller on port 4 through ENgcControllerManager (PADRead
// every frame) and maps it like a Wii Remote through its EController command maps. That
// port is fed from the PC's gamepads and keyboard (input.cpp); the other ports report no
// controller.
#include "input.h"
#include "../game_addrs.h"
#include "../kernel/memory.h"
#include "../log.h"

#include <cstdlib>
#include <initializer_list>
#include <string>

namespace
{
constexpr int kPort = 3;   // ENgcControllerManager reads channel 3 (port 4)
constexpr int8_t PAD_ERR_NONE = 0, PAD_ERR_NO_CONTROLLER = -1;
constexpr uint32_t kStatusSize = 12;

// GameCube PAD_BUTTON_* bits for PADStatus (only the PAD library's view; the game's button
// codes come from the GetButtons override below)
uint16_t PadButtons(const PadState& p)
{
    static const struct { uint32_t btn; uint16_t pad; } kMap[] = {
        { BTN_A, 0x0100 }, { BTN_B, 0x0200 }, { BTN_1, 0x0400 }, { BTN_2, 0x0800 }, { BTN_PLUS, 0x1000 }, { BTN_Z, 0x0010 },
        { BTN_LEFT, 0x0001 }, { BTN_RIGHT, 0x0002 }, { BTN_DOWN, 0x0004 }, { BTN_UP, 0x0008 },
    };
    uint16_t v = 0;
    for (auto& m : kMap)
        if (p.buttons & m.btn) v |= m.pad;
    if (p.triggerL > 200) v |= 0x0040;
    if (p.triggerR > 200) v |= 0x0020;
    return v;
}

// The game's controller button codes (the low half of EController button masks), as
// EWiiController::GetButtons produces them for the Wii Remote and Nunchuk. The command
// maps are written for these; the game's own GameCube conversion has no C, Z or 2.
uint32_t GameButtons(const PadState& p)
{
    static const struct { uint32_t btn, code; } kMap[] = {
        { BTN_A, 0x0040 }, { BTN_B, 0x0010 }, { BTN_C, 0x0200 }, { BTN_Z, 0x0400 }, { BTN_1, 0x0002 }, { BTN_2, 0x0100 },
        { BTN_PLUS, 0x0800 }, { BTN_MINUS, 0x0080 }, { BTN_UP, 0x1000 }, { BTN_DOWN, 0x4000 }, { BTN_LEFT, 0x8000 },
        { BTN_RIGHT, 0x2000 },
    };
    uint32_t v = 0;
    for (auto& m : kMap)
        if (p.buttons & m.btn) v |= m.code;
    // the GameCube triggers keep the game's L/R codes
    if (p.triggerL > 10) v |= 0x0004;
    if (p.triggerR > 10) v |= 0x0008;
    return v;
}
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
        Write16(s + 0, PadButtons(p));
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

// u32 ENgcController::GetButtons(const PADStatus&): the original adds the stick directions
// in the high half (main stick 0x1..0x8, C-stick 0x10..0x80 << 16); the buttons below them
// get the Wii codes (GameButtons).
PPC_OVERRIDE(NgcGetButtons)
{
    PPC_ORIGINAL(NgcGetButtons);
    ctx.r[3] = (ctx.r[3] & 0xFFFF0000u) | GameButtons(Input_Pad());
}

// void EControllerManager::MapPlayerToController(u32 index). The game gives the player to
// whichever controller presses first, the Wii Remote included (it carries the same buttons
// so the Wii-only safety and splash screens work): the player is always the GameCube port.
PPC_OVERRIDE(MapPlayerToController)
{
    ctx.r[4] = kPort;
    PPC_ORIGINAL(MapPlayerToController);
}

// void EControllerManager::MapControllerCommands(EBtnToCmdAssoc* map, int count): the game
// installs its button -> command maps on _ctrlPads[0] only (EController::SetCommandMap
// stores the pointer at +420 and the count at +424, and sorts the shared array), so the
// GameCube controller would produce no commands; give it the same map.
PPC_OVERRIDE(MapControllerCommands)
{
    PPC_ORIGINAL(MapControllerCommands);
    uint32_t pads = GameAddr(GA::CtrlPads);
    uint32_t first = Read32(pads), ours = Read32(pads + 4 * kPort);
    if (!first || !ours || first == ours) return;
    Write32(ours + 420, Read32(first + 420));
    Write32(ours + 424, Read32(first + 424));
}

// void ENgcControllerManager::Update(void): the game only fills in a cursor for Wii
// Remotes; give the GameCube controller the mouse pointer as well. An EController keeps
// two data buffers, the one being filled at +508 (EController::UpdateCursorData writes
// floats at +108/+112) and the one read at +512; this runs after the swap (the original
// ends with EControllerManager::Update), so both get the pointer.
PPC_OVERRIDE(NgcControllerUpdate)
{
    PPC_ORIGINAL(NgcControllerUpdate);
    // CWR_DEBUG_INPUT=1: once a second, the game's view of its controllers
    static const bool debug = getenv("CWR_DEBUG_INPUT") != nullptr;
    static int calls = 0;
    if (debug && ++calls % 60 == 0)
    {
        uint32_t man = Read32(GameAddr(GA::CtrlMan));
        std::string s = std::format("input: player {} mapped {} |", man ? (int32_t)Read32(man + 4) : -9, man ? Read8(man + 8) : 0);
        for (int i = 0; i < 4; i++)
        {
            uint32_t c = Read32(GameAddr(GA::CtrlPads) + 4 * i);
            uint32_t rd = c ? Read32(c + 512) : 0;
            s += std::format(" [{}] vt {:08x} btn {:04x} cur {:.2f},{:.2f} |", i, c ? Read32(c) : 0, rd ? Read32(rd) : 0,
                             rd ? std::bit_cast<float>(Read32(rd + 108)) : 0.0f, rd ? std::bit_cast<float>(Read32(rd + 112)) : 0.0f);
        }
        LOG_INFO("{}", s);
    }
    PointerState p = Input_Pointer();
    uint32_t ctrl = Read32(GameAddr(GA::CtrlPads) + 4 * kPort);
    if (!ctrl || !p.valid) return;
    for (uint32_t buf : { 508u, 512u })
        if (uint32_t data = Read32(ctrl + buf))
        {
            Write32(data + 108, std::bit_cast<uint32_t>(p.x));
            Write32(data + 112, std::bit_cast<uint32_t>(p.y));
        }
}

// Read-only game hooks for TAS scripts: they report what the game does and change nothing.
#include "tas.h"
#include "../game_addrs.h"
#include "../kernel/memory.h"

// StateMachineStatus::SetState(StateMachineState*): every state machine (boot screens, main
// menu, Create a Sim, loading screens, the island) enters its states through it.
PPC_OVERRIDE(StateMachineStatusSetState)
{
    uint32_t state = ctx.r[4];
    uint32_t vtable = 0;
    if (state >> 28 == 0x8 && (state & 0x0FFFFFFF) + 4 <= MEM1_SIZE) vtable = Read32(state);
    else if (state >> 28 == 0x9 && (state & 0x0FFFFFFF) + 4 <= MEM2_SIZE) vtable = Read32(state);
    Tas_OnState(ctx.r[3], vtable);
    PPC_ORIGINAL(StateMachineStatusSetState);
}

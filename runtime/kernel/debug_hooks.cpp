// Temporary diagnostics hooks (logged at --verbose).
#include "../game_addrs.h"
#include "../log.h"
#include "memory.h"

// void StateMachine::SetNextState(int state, float delay)
PPC_OVERRIDE(SetNextState)
{
    LOG_DEBUG("StateMachine {:08x} -> state {} (vtable {:08x}, lr {:08x})", ctx.r[3], (int32_t)ctx.r[4], Read32(ctx.r[3]), ctx.lr);
    PPC_ORIGINAL(SetNextState);
}

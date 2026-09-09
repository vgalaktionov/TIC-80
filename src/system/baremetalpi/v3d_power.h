// Shared by the renderer and the deterministic MMIO regression test.
// Requires V3D registers, elapsed(), CTimer, and DataSyncBarrier from the caller.
#pragma once

static bool powerOn()
{
    V3D_write(PM_GRAFX, (V3D_read(PM_GRAFX) | PM_V3DRSTN) | PM_PASSWORD);
    const unsigned bridges[] = {ASB_V3D_M_CTRL, ASB_V3D_S_CTRL};
    bool success = true;
    for (unsigned bridge : bridges)
    {
        V3D_write(bridge, (V3D_read(bridge) & ~ASB_REQ_STOP) | PM_PASSWORD);
        const unsigned start = CTimer::GetClockTicks();
        while (V3D_read(bridge) & ASB_ACK)
        {
            if (elapsed(start, PowerTimeoutUs))
            {
                success = false;
                break;
            }
        }
    }
    DataSyncBarrier();
    return success;
}

static bool resetGpu()
{
    // BCM2711 reset: isolate slave then master; reopen master then slave.
    // This resets V3D only, not the VCHI/HDMI display or audio blocks.
    const unsigned bridges[] = {ASB_V3D_S_CTRL, ASB_V3D_M_CTRL};
    for (unsigned bridge : bridges)
    {
        V3D_write(bridge, V3D_read(bridge) | ASB_REQ_STOP | PM_PASSWORD);
        const unsigned start = CTimer::GetClockTicks();
        while (!(V3D_read(bridge) & ASB_ACK))
        {
            if (elapsed(start, PowerTimeoutUs))
            {
                powerOn();
                return false;
            }
        }
    }
    V3D_write(PM_GRAFX, (V3D_read(PM_GRAFX) & ~PM_V3DRSTN) | PM_PASSWORD);
    DataSyncBarrier();
    const unsigned start = CTimer::GetClockTicks();
    while (!elapsed(start, 10)) {}
    return powerOn();
}

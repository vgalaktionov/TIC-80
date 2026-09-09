#include <cassert>
#include <cstdint>
#include <vector>
#include <utility>
constexpr unsigned PM_GRAFX = 0, ASB_V3D_S_CTRL = 1, ASB_V3D_M_CTRL = 2;
constexpr unsigned PM_V3DRSTN = 64, PM_PASSWORD = 0x5a000000;
constexpr unsigned ASB_REQ_STOP = 1, ASB_ACK = 2, PowerTimeoutUs = 100000;
static unsigned regs[3], ticks, failStop, failOpen;
static std::vector<std::pair<unsigned, unsigned>> writes;
struct CTimer { static unsigned GetClockTicks() { return ticks++; } };
static bool elapsed(unsigned start, unsigned timeout) {
    return unsigned(CTimer::GetClockTicks() - start) >= timeout;
}
static void DataSyncBarrier() {}
static unsigned V3D_read(unsigned r) { return regs[r]; }
static void V3D_write(unsigned r, unsigned v) {
    assert((v & 0xff000000) == PM_PASSWORD);
    writes.emplace_back(r, v);
    regs[r] = v & 0xffffff;
    if (r != PM_GRAFX) {
        bool stopped = (v & ASB_REQ_STOP) != 0;
        if (stopped && r != failStop) regs[r] |= ASB_ACK;
        else if (!stopped && r != failOpen) regs[r] &= ~ASB_ACK;
    }
}
#include "../../src/system/baremetalpi/v3d_power.h"
static void setup() {
    regs[0] = PM_V3DRSTN | 0x100; regs[1] = regs[2] = 0;
    ticks = 0; failStop = failOpen = 99; writes.clear();
}
int main() {
    setup(); assert(resetGpu());
    assert(writes.size() == 6);
    const unsigned order[] = {1, 2, 0, 0, 2, 1};
    for (unsigned i = 0; i < 6; ++i) assert(writes[i].first == order[i]);
    assert(!(writes[2].second & PM_V3DRSTN));
    assert(writes[3].second & PM_V3DRSTN);
    assert(regs[0] & 0x100); // preserve unrelated power bits
    assert(ticks >= 10 && ticks < 100);
    for (unsigned bridge = 1; bridge <= 2; ++bridge) {
        setup(); failStop = bridge; assert(!resetGpu());
        for (auto w : writes) if (w.first == 0) assert(w.second & PM_V3DRSTN);
        assert(!(regs[1] & ASB_REQ_STOP) && !(regs[2] & ASB_REQ_STOP));
        assert(ticks < 3 * PowerTimeoutUs);
        setup(); failOpen = bridge; assert(!resetGpu());
        assert(!(regs[1] & ASB_REQ_STOP) && !(regs[2] & ASB_REQ_STOP));
        assert(ticks < 3 * PowerTimeoutUs);
    }
    setup(); ticks = UINT32_MAX - 5; assert(resetGpu());
}

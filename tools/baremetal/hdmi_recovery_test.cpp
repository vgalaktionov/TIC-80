// Exercise the production state machine with a deterministic firmware/clock shim.
#include <cassert>
#include <cstdint>
#include <string>
using boolean = bool;
constexpr bool TRUE = true, FALSE = false;
using VCHI_INSTANCE_T = void*;
using VCHI_CONNECTION_T = void;
constexpr unsigned VC_HDMI_ATTACHED = 2, VC_HDMI_UNPLUGGED = 1, VC_HDMI_HDMI = 8;
constexpr int HDMI_MODE_HDMI = 1, HDMI_RES_GROUP_CEA = 1;
struct TV_DISPLAY_STATE_T {
    uint32_t state;
    struct { struct { uint32_t width, height; unsigned frame_rate; } hdmi; } display;
};
static unsigned ticks, offCalls, onCalls;
static int modeResult;
static bool active = true;
static std::string logs;
struct CTimer { static unsigned GetClockTicks() { return ticks; } };
int vchi_initialise(void**) { return 0; }
int vchi_connect(void*, int, void*) { return 0; }
int vc_vchi_tv_init(void*, void**, int) { return 0; }
void vc_tv_register_callback(void (*)(void*, uint32_t, uint32_t, uint32_t), void*) {}
int vc_tv_hdmi_power_on_explicit(int, int, int) { ++onCalls; return modeResult; }
int vc_tv_power_off() { ++offCalls; return 0; }
int vc_tv_get_display_state(TV_DISPLAY_STATE_T* s) {
    *s = {active ? VC_HDMI_HDMI : 0, {{1920, 1080, 60}}};
    return 0;
}
void tic80DebugLogWrite(const char* s) { logs += s; }
void tic80HdmiRecoveryRequest(boolean);
#define TIC80_HDMI_RECOVERY_TEST
#include "../../src/system/baremetalpi/hdmi_recovery.cpp"
static bool advance(unsigned us) { ticks += us; return tic80HdmiRecoveryPoll(); }
static void reset() {
    State = RecoveryIdle; PendingEvent = 0; PendingRequest = 0;
    InputPending = false; DisplayCanVsync = true; Initialized = true;
    ticks = offCalls = onCalls = 0; modeResult = 0; active = true;
}
int main() {
    reset();
    // No HDMI callback, simultaneous keyboard/mouse attachments, one reset.
    tic80HdmiRecoveryInputAttached(); tic80HdmiRecoveryInputAttached();
    advance(0); advance(250000); assert(offCalls == 1);
    assert(!tic80HdmiRecoveryCanWaitForVsync());
    tvEvent(nullptr, VC_HDMI_UNPLUGGED, 0, 0); // our own power-off callback
    advance(500000); assert(onCalls == 1);
    assert(advance(500000)); assert(tic80HdmiRecoveryCanWaitForVsync());
    advance(1000000); advance(1000000); assert(offCalls == 1);
    // A real attachment during cooldown is preserved.
    State = RecoveryCallbackCooldown; RecoveryDeadline = ticks + 100;
    tic80HdmiRecoveryInputAttached(); advance(100); advance(1); advance(250000);
    assert(offCalls == 2);
    reset(); modeResult = -1;
    tic80HdmiRecoveryInputAttached(); advance(0); advance(250000);
    advance(500000); advance(500000); advance(500000); advance(1000000);
    assert(onCalls == 3 && State == RecoveryIdle);
    assert(!tic80HdmiRecoveryCanWaitForVsync());
    // Accepted command but inactive firmware must not count as restored.
    reset(); active = false; tic80HdmiRecoveryInputAttached();
    advance(0); advance(250000); advance(500000);
    assert(!advance(500000)); assert(!advance(3000000));
    assert(!tic80HdmiRecoveryCanWaitForVsync());
    // Timer wraparound and diagnostic-only LAN request.
    reset(); ticks = UINT32_MAX - 100000;
    tic80HdmiRecoveryRequest(false); advance(0); assert(offCalls == 0);
    tic80HdmiRecoveryInputAttached(); advance(0); advance(250000);
    assert(offCalls == 1);
}

#include "hdmi_recovery.h"

#include "debuglog.h"

#include <circle/timer.h>
#include <vc4/vchi/vchi.h>
#include <vc4/interface/vmcs_host/vc_tvservice.h>

#include <stdio.h>

namespace
{
constexpr unsigned AttachDebounceUs = 250000;
constexpr unsigned PowerOffHoldUs = 500000;
constexpr unsigned ModeSettleUs = 500000;
constexpr unsigned CallbackCooldownUs = 1000000;
constexpr uint32_t HdmiEventNone = 0;
constexpr uint32_t HdmiEventAttached = 1;
constexpr uint32_t HdmiEventUnplugged = 2;

enum RecoveryState
{
    RecoveryIdle,
    RecoveryAttachDebounce,
    RecoveryPowerOffHold,
    RecoveryModeSettle,
    RecoveryCallbackCooldown,
};

VCHI_INSTANCE_T VchiInstance = nullptr;
volatile uint32_t PendingEvent = HdmiEventNone;
unsigned RecoveryDeadline = 0;
RecoveryState State = RecoveryIdle;
bool Initialized = false;

void logResult(const char* operation, int result)
{
    char message[128];
    snprintf(message, sizeof message, "[tic80] HDMI recovery: %s (%d)\n", operation, result);
    tic80DebugLogWrite(message);
}

void tvEvent(void*, uint32_t reason, uint32_t param1, uint32_t param2)
{
    (void)param1;
    (void)param2;
    if (reason == VC_HDMI_ATTACHED)
    {
        __atomic_store_n(&PendingEvent, HdmiEventAttached, __ATOMIC_RELEASE);
    }
    else if (reason == VC_HDMI_UNPLUGGED)
    {
        __atomic_store_n(&PendingEvent, HdmiEventUnplugged, __ATOMIC_RELEASE);
    }
}

int assertFixedMode()
{
    return vc_tv_hdmi_power_on_explicit(HDMI_MODE_HDMI, HDMI_RES_GROUP_CEA, 16);
}

void logDisplayState(const char* label)
{
    TV_DISPLAY_STATE_T state = {};
    const int result = vc_tv_get_display_state(&state);
    if (result != 0)
    {
        logResult(label, result);
        return;
    }

    char message[160];
    snprintf(message, sizeof message,
             "[tic80] HDMI recovery: %s state=%08lx %lux%lu@%u\n",
             label,
             static_cast<unsigned long>(state.state),
             static_cast<unsigned long>(state.display.hdmi.width),
             static_cast<unsigned long>(state.display.hdmi.height),
             state.display.hdmi.frame_rate);
    tic80DebugLogWrite(message);
}
}

boolean tic80HdmiRecoveryInitialize()
{
    if (Initialized)
    {
        return TRUE;
    }

    int result = vchi_initialise(&VchiInstance);
    if (result != 0)
    {
        logResult("VCHI initialization failed", result);
        return FALSE;
    }

    result = vchi_connect(nullptr, 0, VchiInstance);
    if (result != 0)
    {
        logResult("VCHI connection failed", result);
        return FALSE;
    }

    VCHI_CONNECTION_T* connection = nullptr;
    result = vc_vchi_tv_init(VchiInstance, &connection, 1);
    if (result != 0)
    {
        logResult("TV service initialization failed", result);
        return FALSE;
    }

    vc_tv_register_callback(tvEvent, nullptr);
    Initialized = true;

    logDisplayState("initial");

    result = assertFixedMode();
    logResult("initial 1080p60 mode assertion", result);
    return TRUE;
}

boolean tic80HdmiRecoveryPoll()
{
    if (!Initialized)
    {
        return FALSE;
    }

    const uint32_t event = __atomic_exchange_n(&PendingEvent, HdmiEventNone, __ATOMIC_ACQ_REL);
    if (event == HdmiEventUnplugged)
    {
        if (State == RecoveryIdle || State == RecoveryAttachDebounce)
        {
            State = RecoveryIdle;
            tic80DebugLogWrite("[tic80] HDMI recovery: display detached\n");
        }
    }
    else if (event == HdmiEventAttached)
    {
        if (State == RecoveryIdle || State == RecoveryAttachDebounce)
        {
            RecoveryDeadline = CTimer::GetClockTicks() + AttachDebounceUs;
            State = RecoveryAttachDebounce;
            tic80DebugLogWrite("[tic80] HDMI recovery: display attached; scheduling link reset\n");
        }
    }

    const unsigned now = CTimer::GetClockTicks();
    if (State == RecoveryAttachDebounce
        && static_cast<int>(now - RecoveryDeadline) >= 0)
    {
        const int result = vc_tv_power_off();
        logResult("hotplug link power off", result);
        if (result == 0)
        {
            RecoveryDeadline = now + PowerOffHoldUs;
            State = RecoveryPowerOffHold;
        }
        else
        {
            RecoveryDeadline = now;
            State = RecoveryPowerOffHold;
        }
    }
    else if (State == RecoveryPowerOffHold
             && static_cast<int>(now - RecoveryDeadline) >= 0)
    {
        const int result = assertFixedMode();
        logResult("hotplug 1080p60 mode restore", result);
        if (result == 0)
        {
            RecoveryDeadline = now + ModeSettleUs;
            State = RecoveryModeSettle;
        }
        else
        {
            RecoveryDeadline = now + PowerOffHoldUs;
        }
    }
    else if (State == RecoveryModeSettle
             && static_cast<int>(now - RecoveryDeadline) >= 0)
    {
        logDisplayState("settled");
        RecoveryDeadline = now + CallbackCooldownUs;
        State = RecoveryCallbackCooldown;
        return TRUE;
    }
    else if (State == RecoveryCallbackCooldown
             && static_cast<int>(now - RecoveryDeadline) >= 0)
    {
        State = RecoveryIdle;
    }

    return FALSE;
}

boolean tic80HdmiRecoveryCanWaitForVsync()
{
    return State != RecoveryPowerOffHold && State != RecoveryModeSettle;
}

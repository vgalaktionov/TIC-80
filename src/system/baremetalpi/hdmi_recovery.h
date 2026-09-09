#pragma once

#include <circle/types.h>

boolean tic80HdmiRecoveryInitialize();
boolean tic80HdmiRecoveryPoll();
boolean tic80HdmiRecoveryCanWaitForVsync();
void tic80HdmiRecoveryInputAttached();
void tic80HdmiRecoveryRequest(boolean reset);

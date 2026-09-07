#pragma once

#include <circle/types.h>

class CNetSubSystem;

void tic80DebugLogWrite(const char* message);
void tic80DebugLogWriteBytes(const void* data, unsigned length);
unsigned tic80DebugLogSnapshot(char* output, unsigned capacity,
                               unsigned* droppedBytes, unsigned* writeCalls);
boolean tic80DebugLogServerStart(CNetSubSystem* network, unsigned port = 8080);

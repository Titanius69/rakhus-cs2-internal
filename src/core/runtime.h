#pragma once
// Runtime lifecycle — shared across modules
#include <Windows.h>

extern HMODULE g_hModule;
extern volatile bool g_running;
extern volatile bool g_unloadRequested;

// Call when END is pressed or shutdown is requested
inline void RequestUnload() {
    g_unloadRequested = true;
    g_running = false;
}

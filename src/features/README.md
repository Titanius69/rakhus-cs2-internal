# features/

Intended split targets (current logic still mostly in `dllmain.cpp`):

| Module | Responsibility |
|--------|----------------|
| aim | DoLegitAim, soft RCS |
| trigger | DoTriggerbot (threadless state machine) |
| esp | DrawFrame ESP path, skeleton, glow |
| misc | flash/smoke/NVR/bomb/spectators/hitmarker |
| thirdperson | hold-key TP + unload-safe patch restore |

`core/runtime.h` — `g_running` / unload flags used by Present + MainLoop.

# OpenRGB 1.1 ZOTAC RTX 5070 Ti Smooth Engine Plugin (v2.1.0)

## Fixes in v2.1.0 (Fixes Stuck-Color Freeze + Keeps Smooth Transitions)
1. **Safe 10 kHz Auto Default for Zotac 4A (0x4A)**: v2.0.0 tried to auto-boost `0x4A` to 100 kHz, which can hang the Zotac 4A MCU on 32-byte writes. v2.1.0 keeps `0x4A` on the verified FireStorm 10 kHz clock in Auto mode while pacing at ~43 ms (~23 Hz, 4x faster than v1.0.0's 180 ms) with a mandatory 12 ms bus-idle recovery gap.
2. **Mutex-Guarded Worker Wakeups**: Fixes a condition_variable race condition where `sw_cycle_active` / `mirrored_cycle_active` updates outside `target_mutex` could miss a wakeup and leave the worker sleeping.
3. **Full Blackwell Staging Block**: Restores complete `0x20..0x2B` staging writes + 2 ms commit settle for `0x4B` cards.
4. **Non-Conflicting Preview Sync**: Prevents `OnSyncTimerTick` from overwriting `virt_ctrl` colors when OpenRGB Direct/Effects is actively driving the device.
5. **In-Tab Diagnostics**: Displays live `Frames OK`, `Failed`, `rc`, and adds a **Toggle Smooth Cycle** test button directly in the plugin tab.

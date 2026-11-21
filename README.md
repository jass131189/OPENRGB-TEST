# OpenRGB 1.1 ZOTAC RTX 5070 Ti Smooth Engine Plugin (v2.2.0)

## What v2.2.0 Fixes (Restores Full Working Hardware Control + Smooth ~25 Hz Cycles & Mirroring)
1. **Fixes Stuck/Dead Static & Mirroring (Main-Thread NVAPI)**: v2.0/v2.1 moved runtime NVAPI writes onto a background `std::thread`, causing Windows `nvapi64.dll` I2C calls initialized on the Qt main thread to stall/fail after boot. v2.2.0 restores 100% Qt-Main-Thread NVAPI execution (matching the working v1.0.0 baseline) and safely marshals any off-thread OpenRGB Effects/Resource callbacks via coalesced `Qt::QueuedConnection`.
2. **Restores Proven Hardware Compatibility**: Restores the full `0x08..0x77` fallback I2C sweep in `ScanNVAPIBus()` and the 16-register `0x20..0x2F` + `Sleep(3)`/`Sleep(10)` commit sequence for Blackwell cards, plus 10 kHz Auto Safe clock on `0x4A`.
3. **Eliminates Color Stuttering (~25 Hz Smooth Cycles & Interpolation)**: Replaces v1.0.0's 180 ms throttle (~5.5 FPS) and 50 ms beat jitter with a 20 ms `Qt::PreciseTimer` paced at 40 ms (~25 Hz) with an 8 ms bus/UI recovery gap, plus linear-light exponential color interpolation when mirroring other OpenRGB devices.
4. **Simple, Reliable 3-Mode Workflow**:
   - **Static Color**: `Pick GPU Color...` or OpenRGB Devices `Static`/`Direct` writes synchronously & immediately.
   - **Mirror Other Devices**: Automatically follows active OpenRGB devices smoothly.
   - **Smooth Color Cycles**: `Toggle Smooth Cycle` button or OpenRGB `Spectrum Cycle`/`Rainbow Wave`.

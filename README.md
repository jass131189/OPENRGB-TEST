# OpenRGB 1.1 ZOTAC RTX 5070 Ti Smooth Engine Plugin (v2.0.0)

## What Changed in v2.0.0 (Anti-Stutter Redesign)
1. **Removed 180 ms NVAPI Throttle & 200 ms Native Sleep**: Replaced the 5.5 FPS cap with an adaptive frame pacer (default 16 ms / 63 Hz target).
2. **Dedicated Background Worker Thread**: All NVAPI I2C transactions now run off the Qt GUI thread using `std::thread` + `std::condition_variable`.
3. **Auto Bus-Speed Negotiation (100 kHz -> 40 kHz -> 10 kHz)**: Benchmarks and selects the fastest ACKed DDC clock with automatic write-error fallback.
4. **Linear-Light Temporal Interpolation (120 ms)**: Smoothly interpolates stepped colors from OpenRGB device mirroring or Effects in Gamma 2.2 linear light space.
5. **Blackwell Dirty-Register Diffing**: Caches register shadows to cut transition writes from 16 byte calls per zone down to 3–4 bytes.

## Build via GitHub Actions
1. Replace the files in your `OPENRGB-TEST` repository with this package.
2. Push to `main` and download the `OpenRGBZotac5070TiPlugin.dll` artifact from the **Actions** tab.
3. Close ZOTAC FireStorm, launch OpenRGB 1.1 as Administrator, and install the DLL under **Settings -> Plugins -> Install Plugin**.

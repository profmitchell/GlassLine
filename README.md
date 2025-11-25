# SoundAtlas Library Manager (OBS macOS Plugin)

SoundAtlas Library Manager is an OBS Studio source plugin tailored for macOS. It wraps the PRD in `prd.md` into a concrete starting point so you can drop a glass-inspired audio visualizer straight into your scenes while keeping installation, signing, and future packaging aligned with Apple Silicon workflows.

## What’s included

- **New source identity:** Project metadata is prefilled for SoundAtlas (name, display label, bundle ID, support contact).
- **macOS focus:** Buildspec defaults to Apple Silicon-safe identifiers and leaves space for codesigning/notarization steps used by OBS’s macOS toolchain.
- **Visualizer blueprint:** The PRD covers single- and multi-source audio capture, multiple visual modes (line, bars, circular), styling controls, and preset handling so you can continue implementing the rendering and DSP path inside this boilerplate.

## Working from the PRD

The full product requirements live in [`prd.md`](prd.md). Use it as your implementation checklist:

- Add a **GlassLine Visualizer** source type that can bind to single or multiple OBS audio sources.
- Implement the three visual families with presets (Ad Line Minimal, Bass-Heavy Bars, Circular Meter).
- Keep UI strings approachable (e.g., Smoothness, Height, Detail, Bass Focus) and group them into Audio Source, Visual Type, Style, and Advanced sections.
- Favor GPU-friendly rendering inside OBS’s abstraction layer and be resilient when audio sources disappear or go silent.

## Building on macOS (local)

1. Install OBS dependencies per the OBS plugin template guidance (CMake 3.28+, Xcode 16 for generators).
2. Configure a build directory:
   ```bash
   cmake -S . -B build -D CMAKE_BUILD_TYPE=RelWithDebInfo
   ```
3. Build the plugin module:
   ```bash
   cmake --build build --config RelWithDebInfo
   ```
4. The resulting module will appear in the build output; package/sign according to your codesigning profile when you are ready to distribute.

## Next implementation steps

- Wire up OBS source registration for the GlassLine Visualizer and connect to OBS audio callbacks.
- Implement FFT-based spectrum analysis with smoothing and peak handling.
- Render line, bar, and circular variants using OBS’s graphics API with optional glow/double-draw for the glass aesthetic.
- Persist presets to a JSON config (e.g., `glassline_presets.json`) in OBS’s plugin config directory.

This repository now carries the SoundAtlas title and macOS-focused metadata so you can move directly into feature development without reworking the boilerplate.

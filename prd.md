Here’s a full PRD you can hand to an AI-coder or human dev for a macOS visualizer plugin for OBS (Spectralizer/Waveform replacement, tailored to your preset-ad workflow).

⸻

1. Product Overview

Name (working): GlassLine (Audio Visualizer for OBS)
Type: OBS Studio source plugin (C++), focused on clean, minimal line + bar visualizers for music production ads and streaming.

Primary goal:
Provide an easy-to-use, native macOS-friendly visualizer source for OBS that:
	•	Hooks directly into existing OBS audio sources (desktop audio, DAW loopback, mic, etc.).
	•	Renders modern, minimal visualizers (line, bars, circular) that can sit under glass-style UI panels (your preset labels, logos, etc.).
	•	Is stable and easy to install on Apple Silicon macOS with proper signing/notarization.

⸻

2. Target Platform & Environment
	•	Host Application: OBS Studio (latest LTS + current stable)
	•	OS: macOS 13+ (Ventura, Sonoma, Sequoia)
	•	CPU: Apple Silicon (M1+). Optional support for Intel Macs if easy.
	•	Graphics: Use OBS’s cross-platform rendering system (prefer GPU-accelerated, e.g., via OpenGL/Metal abstraction provided by OBS).

⸻

3. Personas & Core Use Cases

Persona A – Sound Designer / Producer (you)
	•	Creates Serum 2 preset ads (9:16 and 16:9).
	•	Uses OBS to composite:
	•	Background video (blurred)
	•	Serum UI capture
	•	Preset label HTML panel
	•	Needs a simple line visualizer reacting to DAW audio (desktop audio / loopback).

Key needs:
	•	Fast setup, minimal tweaking.
	•	Clean look that fits “Cohen Concepts” aesthetic (glass, neon, thin lines).
	•	Reliable audio sync with the recorded video.

Persona B – Streamer / Music Educator
	•	Streams production or mixing sessions.
	•	Wants a bar visualizer at screen bottom reacting to full mix or music bus.
	•	Changes scenes often, needs the visualizer to “just work” once configured.

Key needs:
	•	Easy to pick which audio source(s) to follow.
	•	Scene-agnostic operation (works across scenes when reused / duplicated).
	•	Low CPU/GPU overhead.

⸻

4. Goals & Non-Goals

Goals
	•	Provide a new source type in OBS: GlassLine Visualizer.
	•	Allow the user to bind it to:
	•	A single OBS audio source, or
	•	A mix of selected sources (e.g., “Desktop Audio” + “DAW Loopback”).
	•	Offer 3 main visualization modes:
	1.	Line (continuous “curve” across width).
	2.	Bars (classic spectrum bars).
	3.	Circular (bars or line arranged around a circle).
	•	Provide deep styling options:
	•	Colors, width, smoothing, falloff, dynamic range.
	•	Background transparency (for overlay use).
	•	Implement good presets for “look-nice-instantly” usage:
	•	“Ad Line Minimal”
	•	“Bass-Heavy Bars”
	•	“Circular Meter”

Non-Goals
	•	Not a full VJ engine (no video clips, no image sequences).
	•	No built-in audio playback (plugin only visualizes audio already in OBS).
	•	No external network control in v1 (WebSocket/REST can be future work).
	•	No editor/GUI outside of OBS’s standard source properties.

⸻

5. High-Level UX & Workflow in OBS

5.1 Adding the visualizer
	1.	User opens OBS, selects a Scene (e.g., “Serum Ad 9x16”).
	2.	In Sources panel → + → selects GlassLine Visualizer.
	3.	Names it (e.g., “Preset Visualizer”).

5.2 Configuring audio

In the source properties dialog:
	•	Dropdown: Audio Mode
	•	Single Source (default)
	•	Multi Source Mix
	•	If Single Source:
	•	Dropdown listing all OBS audio sources in the current scene + global sources (e.g., “Desktop Audio”, “Loopback 1”, “Mic”).
	•	If Multi Source Mix:
	•	Multi-select list of audio sources with checkboxes.

5.3 Choosing a visualization style

Still in source properties:
	•	Visualization Type
	•	Line
	•	Bars
	•	Circular Line
	•	Circular Bars

Depending on type, show relevant options (see feature list below).

5.4 Styling

User configures:
	•	Color(s)
	•	Thickness / bar width
	•	Smoothing / decay
	•	Dynamic range and scale

User can click Apply & Preview to see changes live in the scene.

⸻

6. Functional Requirements

6.1 Audio Input & Analysis
	•	FR-1: Plugin must receive PCM audio data from selected OBS audio sources.
	•	FR-2: Support mono & stereo; for stereo:
	•	Option to analyze:
	•	Sum (L+R)
	•	Left only
	•	Right only
	•	Split (Optional: e.g., line per channel – future).
	•	FR-3: Perform real-time FFT analysis to produce frequency bins.
	•	Configurable FFT size (e.g., 512, 1024, 2048).
	•	Configurable overlap/smoothing window.
	•	FR-4: Provide automatic level scaling:
	•	Default auto-gain to keep visuals active over a wide range.
	•	Manual scale override (user sets min/max amplitude).

6.2 Visualization Modes

6.2.1 Line Mode
	•	FR-5: Draw a continuous polyline across the width, sampling N frequency bins.
	•	FR-6: Options:
	•	Line thickness (px)
	•	Line color (solid, gradient across X)
	•	Smoothing (temporal; 0–1)
	•	Frequency scale:
	•	Linear
	•	Logarithmic
	•	FR-7: Optional Floor line / baseline opacity.

6.2.2 Bar Mode
	•	FR-8: Draw vertical bars representing bins or groups of bins.
	•	FR-9: Options:
	•	Bar count (e.g., 32, 64, 96)
	•	Bar gap (px)
	•	Bar corner radius (for rounded ends)
	•	Bar color (single color or gradient)
	•	FR-10: Optional peak indicator:
	•	Dots at the top of bars with independent falloff speed.

6.2.3 Circular Modes
	•	FR-11: Render line or bars arranged on a circle.
	•	FR-12: Options:
	•	Radius (inner & outer or single + thickness)
	•	Start angle, End angle (e.g., full 360° or arc).
	•	Rotation (orientation in degrees).
	•	FR-13: Must support transparency around the circle so it can overlay on any background.

6.3 Styling & Aesthetic Controls
	•	FR-14: Colors:
	•	Single solid color.
	•	Two-color gradient (start/end).
	•	Optional background color with alpha (e.g., subtle glassy rectangle behind bars).
	•	FR-15: Opacity:
	•	Global alpha for the visualizer.
	•	Separate alpha for background fill (if used).
	•	FR-16: “Glass” mode:
	•	Predefined style preset aligning with liquid-glass UI:
	•	Soft white line/bars.
	•	Slight glow (simulated via blur or duplicate layer).
	•	High smoothing, low aggressiveness.

6.4 Presets & Profiles
	•	FR-17: Provide built-in style presets with names, e.g.:
	•	Ad – Minimal Line
	•	Ad – Bass Bars
	•	Stream – Loudness Meter
	•	Circular – Logo Ring
	•	FR-18: Allow user presets:
	•	User can Save Current Settings as Preset (name stored in plugin config).
	•	User can select preset from dropdown to instantly recall settings.

6.5 Source Transform & Performance
	•	FR-19: Visualizer should respect standard OBS transforms:
	•	Scaling
	•	Cropping
	•	Positioning
	•	Filters (e.g., additional blur or color correction).
	•	FR-20: Performance:
	•	Designed to run at 60 FPS on typical Apple Silicon machines while OBS is recording/streaming.
	•	Provide a Quality dropdown:
	•	Low (smaller FFT, lower FPS, heavy smoothing)
	•	Medium (default)
	•	High (higher FFT size/sampling, best detail).

6.6 Config Persistence
	•	FR-21: Each source remembers its settings with the Scene/Collection.
	•	FR-22: Presets are stored in OBS’s plugin config folder and accessible across scenes.

⸻

7. Non-Functional Requirements

7.1 Performance
	•	Target CPU overhead per instance: < 3–5% on an M-class CPU at 1080p60 in typical conditions.
	•	Minimal GPU overhead:
	•	Use batched drawing and avoid excessive state switches.
	•	Use OBS’s recommended rendering APIs (no per-frame texture uploads bigger than needed).

7.2 Stability & Robustness
	•	Must not crash OBS if:
	•	The assigned audio source disappears (user deletes it).
	•	Scene collection is switched.
	•	Audio goes silent for long periods.
	•	Handle invalid settings gracefully (clamp ranges, etc.).

7.3 macOS-Specific
	•	Signed and Notarized installer / plugin:
	•	Avoid the “cannot be opened because the developer cannot be verified” friction as much as possible.
	•	Apple Silicon optimized build; universal binary if Intel is supported.
	•	Compatible with both Intel + Apple Silicon OBS builds where applicable.

7.4 Usability
	•	No jargon in the UI. Use simple labels like:
	•	“Smoothness”
	•	“Height”
	•	“Detail”
	•	“Bass Focus (log scale)”
	•	Group options into collapsible sections:
	•	Audio Source
	•	Visual Type
	•	Style
	•	Advanced

⸻

8. Technical Design (High-Level)

8.1 Architecture
	•	Implement as an OBS source plugin using the OBS plugin API:
	•	Source type: OBS_SOURCE_TYPE_INPUT.
	•	Receives audio data via OBS source audio callbacks.
	•	Renders via OBS’s graphics subsystem (OpenGL/Direct3D abstraction – use what OBS provides on macOS).

8.2 Audio Processing Pipeline
	1.	Audio Capture
	•	Plugin registers to receive audio frames from the selected OBS source(s).
	•	If Multi Source Mix is active, sum or mix the audio buffers.
	2.	Pre-processing
	•	Convert to mono (sum L+R) unless user selected another mode.
	•	Apply windowing (Hann, Hamming, etc.).
	3.	FFT / Spectrum
	•	Use FFT library (e.g., FFTW or OBS’s internal DSP if available) to compute frequency bins.
	•	Map bins to N drawing points (line or bars) using:
	•	Linear or logarithmic mapping.
	4.	Post-processing & Smoothing
	•	Temporal smoothing filter:
	•	Exponential moving average.
	•	Peak hold logic for bars (optional).
	5.	Render Data Cache
	•	Maintain a small data structure (arrays of amplitudes, colors) passed to render function.

8.3 Rendering
	•	Use OBS’s graphics API to:
	•	Draw filled quads for bars.
	•	Draw line strips for the line visualizer.
	•	For circular modes, calculate polar coordinates and convert to screen positions.
	•	For glow/glass effect:
	•	Optional “double-draw” of the line with:
	•	Lower alpha, slightly thicker width underneath.

⸻

9. Configuration & Files
	•	Config Storage:
	•	Per-source configuration embedded in the OBS scene JSON (standard OBS behavior).
	•	Presets stored in a small JSON file in the plugin config directory:
	•	e.g., glassline_presets.json.
	•	Installer:
	•	macOS .pkg or .dmg that places:
	•	Plugin binary in ~/Library/Application Support/obs-studio/plugins or equivalent.
	•	Config / resources where OBS expects them.

⸻

10. Testing & QA

10.1 Functional Tests
	•	Verify:
	•	Each visual type renders correctly.
	•	Audio source selection works (including when sources are renamed).
	•	Presets can be created, loaded, and deleted.
	•	Edge cases:
	•	No audio / low audio level.
	•	Muted source(s).
	•	Removing and re-adding the plugin.

10.2 Performance Tests
	•	Run on:
	•	M1/M2 MacBooks and desktop.
	•	Scenarios:
	•	1080p60 recording with:
	•	1 visualizer.
	•	3–4 visualizers in the same scene.
	•	Monitor CPU & GPU overhead.

10.3 Compatibility Tests
	•	Different OBS versions:
	•	Current stable.
	•	Latest beta (if applicable).
	•	Different audio setups:
	•	Desktop audio.
	•	Loopback from DAW (BlackHole, Loopback, etc.).
	•	Mic + backing track.

⸻

11. Future Enhancements (Out of Scope for v1, but nice to note)
	•	WebSocket/HTTP control:
	•	Receive external messages to change color preset or style live.
	•	Per-band coloring (e.g., different color for bass, mids, highs).
	•	Side-chain mode (e.g., separate visualizers for kick vs full mix).
	•	Support for “audio from file” mode (plugin plays an audio file directly without OBS audio routing).
	•	Ability to export visualization frames as PNG/WebM for offline render (like your Orphic workflow).

⸻

If you give this PRD to your AI coder, they’ll know:
	•	What to build (an OBS source plugin, not a standalone app).
	•	How it should behave on macOS specifically.
	•	Which modes and styling options matter most for your Serum preset ad pipeline.
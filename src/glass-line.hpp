#pragma once

#include <obs.h>
#include <vector>
#include <string>
#include <mutex>

struct GlassLineSource {
	obs_source_t *source;

	// Settings
	std::string audio_source_name;
	int mode; // 0: Centered Waveform, 1: Symmetric Waveform, 2: Mirrored Bars, 3: Filled Mirror, 4: Pulse Line, 5: Multi-Wave, 6: Symetric Dots
	uint32_t color;
	uint32_t color_start; // Gradient start color
	uint32_t color_end;   // Gradient end color
	uint32_t glow_color;  // Glow/shadow color
	float glow_strength;  // Glow intensity (0.0-1.0)
	float thickness;
	float line_width; // Line width for waveform modes
	float smoothing;
	float amp_scale; // Audio amplitude scaling

	// Audio Data
	std::mutex audio_mutex;
	std::vector<float> audio_data; // Raw samples for line mode (optional)

	// FFT State
	std::vector<float> fft_input_buffer;
	std::vector<float> fft_output_magnitudes;
	std::vector<float> smoothed_magnitudes;
	obs_source_t *audio_source_obj = nullptr;
	obs_source_t *parent_source = nullptr; // The source itself

	GlassLineSource(obs_source_t *source);
	~GlassLineSource();

	void Update(obs_data_t *settings);
	void Render(gs_effect_t *effect);
	void AudioCallback(const struct audio_data *data);

	// Helper to attach/detach audio source
	void SetAudioSource(const char *name);
};

extern struct obs_source_info glass_line_source;

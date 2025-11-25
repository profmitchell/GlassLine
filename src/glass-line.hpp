#pragma once

#include <obs.h>
#include <vector>
#include <string>
#include <mutex>

struct GlassLineSource {
	obs_source_t *source;

	// Settings
	std::string audio_source_name;
	int mode; // 0: Line, 1: Bars, 2: Circular Line, 3: Circular Bars
	uint32_t color;
	float thickness;
	float smoothing;
	float amp_scale; // Audio amplitude scaling
	int bar_count;
	float radius;

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

#include "glass-line.hpp"
#include "fft-utils.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <util/circlebuf.h>
#include <util/threading.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#define S_SOURCE "source"
#define S_MODE "mode"
#define S_COLOR "color"
#define S_COLOR_START "color_start"
#define S_COLOR_END "color_end"
#define S_GLOW_COLOR "glow_color"
#define S_GLOW_STRENGTH "glow_strength"
#define S_THICKNESS "thickness"
#define S_LINE_WIDTH "line_width"
#define S_SMOOTHING "smoothing"
#define S_AMP_SCALE "amp_scale"

#define T_SOURCE "Audio Source"
#define T_MODE "Visual Mode"
#define T_COLOR "Color"
#define T_COLOR_START "Gradient Start Color"
#define T_COLOR_END "Gradient End Color"
#define T_GLOW_COLOR "Glow Color"
#define T_GLOW_STRENGTH "Glow Strength"
#define T_THICKNESS "Thickness"
#define T_LINE_WIDTH "Line Width"
#define T_SMOOTHING "Smoothing"
#define T_AMP_SCALE "Amplitude Scale"

static void audio_capture_callback(void *param, obs_source_t *source, const struct audio_data *audio_data, bool muted)
{
	(void)source;
	(void)muted;
	GlassLineSource *context = (GlassLineSource *)param;
	context->AudioCallback(audio_data);
}

GlassLineSource::GlassLineSource(obs_source_t *source) : source(source)
{
	// Initialize defaults
	mode = 0;
	color = 0xFFFFFFFF;
	color_start = 0xFFFFE7C1; // Light orange/cream
	color_end = 0xFFB63814;   // Dark orange/red
	glow_color = 0xFFFF7832;  // Orange glow
	glow_strength = 0.5f;
	thickness = 2.0f;
	line_width = 4.0f;
	smoothing = 0.5f;
	amp_scale = 1.0f;

	parent_source = source;

	// FFT Buffer Init
	fft_input_buffer.reserve(2048);
}

GlassLineSource::~GlassLineSource()
{
	if (audio_source_obj) {
		obs_source_remove_audio_capture_callback(audio_source_obj, audio_capture_callback, this);
		obs_source_release(audio_source_obj);
	}
}

void GlassLineSource::SetAudioSource(const char *name)
{
	if (audio_source_obj) {
		obs_source_remove_audio_capture_callback(audio_source_obj, audio_capture_callback, this);
		obs_source_release(audio_source_obj);
		audio_source_obj = nullptr;
	}

	if (name && *name) {
		audio_source_obj = obs_get_source_by_name(name);
		if (audio_source_obj) {
			obs_source_add_audio_capture_callback(audio_source_obj, audio_capture_callback, this);
		}
	}
}

void GlassLineSource::Update(obs_data_t *settings)
{
	const char *new_source_name = obs_data_get_string(settings, S_SOURCE);
	if (audio_source_name != new_source_name) {
		audio_source_name = new_source_name;
		SetAudioSource(audio_source_name.c_str());
	}

	mode = (int)obs_data_get_int(settings, S_MODE);
	color = (uint32_t)obs_data_get_int(settings, S_COLOR);
	color_start = (uint32_t)obs_data_get_int(settings, S_COLOR_START);
	color_end = (uint32_t)obs_data_get_int(settings, S_COLOR_END);
	glow_color = (uint32_t)obs_data_get_int(settings, S_GLOW_COLOR);
	glow_strength = (float)obs_data_get_double(settings, S_GLOW_STRENGTH);
	thickness = (float)obs_data_get_double(settings, S_THICKNESS);
	line_width = (float)obs_data_get_double(settings, S_LINE_WIDTH);
	smoothing = (float)obs_data_get_double(settings, S_SMOOTHING);
	amp_scale = (float)obs_data_get_double(settings, S_AMP_SCALE);
}

void GlassLineSource::AudioCallback(const struct audio_data *data)
{
	std::lock_guard<std::mutex> lock(audio_mutex);

	size_t frames = data->frames;
	if (frames == 0)
		return;

	// Accumulate samples for FFT
	// We need a power of 2, say 1024 or 2048
	const size_t fft_size = 2048;

	// Just take the first channel (mono)
	const float *samples = (const float *)data->data[0];

	for (size_t i = 0; i < frames; i++) {
		fft_input_buffer.push_back(samples[i]);
	}

	// If we have enough data, run FFT
	if (fft_input_buffer.size() >= fft_size) {
		// Keep only the latest fft_size samples
		if (fft_input_buffer.size() > fft_size) {
			size_t excess = fft_input_buffer.size() - fft_size;
			fft_input_buffer.erase(fft_input_buffer.begin(), fft_input_buffer.begin() + excess);
		}

		// Apply window function (Hann)
		std::vector<float> windowed_input = fft_input_buffer;
		for (size_t i = 0; i < fft_size; i++) {
			float multiplier = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (fft_size - 1)));
			windowed_input[i] *= multiplier;
		}

		SimpleFFT::Compute(windowed_input, fft_output_magnitudes);

		// Smooth the magnitudes
		if (smoothed_magnitudes.size() != fft_output_magnitudes.size()) {
			smoothed_magnitudes = fft_output_magnitudes;
		} else {
			for (size_t i = 0; i < fft_output_magnitudes.size(); i++) {
				smoothed_magnitudes[i] = smoothed_magnitudes[i] * smoothing +
							 fft_output_magnitudes[i] * (1.0f - smoothing);
			}
		}

		// Clear buffer partially to allow overlap?
		// For now, just keep it sliding in the next frame.
		// Actually, we usually want to consume the buffer.
		// But for a visualizer, we want the latest data.
		// The sliding window above handles it.
	}
}

void GlassLineSource::Render(gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	std::lock_guard<std::mutex> lock(audio_mutex);

	if (smoothed_magnitudes.empty())
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);

	float width = (float)obs_source_get_width(source);
	float height = (float)obs_source_get_height(source);

	// Prepare data for visualization
	size_t start_bin = 1;
	size_t end_bin = smoothed_magnitudes.size() / 2;
	if (end_bin <= start_bin)
		return;

	size_t num_bins = end_bin - start_bin;

	// Helper to fix color format (OBS uses ABGR, we have ARGB)
	auto fix_color = [](uint32_t argb) -> uint32_t {
		uint8_t a = (argb >> 24) & 0xFF;
		uint8_t r = (argb >> 16) & 0xFF;
		uint8_t g = (argb >> 8) & 0xFF;
		uint8_t b = argb & 0xFF;
		return (a << 24) | (b << 16) | (g << 8) | r; // ABGR
	};

	while (gs_effect_loop(solid, "Solid")) {

		if (mode == 0) { // Centered Waveform (bass from center, spreads left/right)
			float center_x = width / 2.0f;
			float max_amplitude = height * 0.3f;

			// Draw glow first
			if (glow_strength > 0.01f) {
				gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(glow_color));

				// Top half glow
				gs_render_start(true);
				for (size_t i = 0; i < num_bins / 2; i++) {
					size_t bin_idx = start_bin + i;
					float mag = smoothed_magnitudes[bin_idx] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);

					// Left side (bass at center, highs outward)
					float x_left = center_x - ((float)i / (float)(num_bins / 2)) * (center_x);
					float y_top = height / 2.0f - amplitude;
					gs_vertex2f(x_left, y_top);
				}
				gs_render_stop(GS_LINESTRIP);

				// Bottom half glow
				gs_render_start(true);
				for (size_t i = 0; i < num_bins / 2; i++) {
					size_t bin_idx = start_bin + i;
					float mag = smoothed_magnitudes[bin_idx] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);

					float x_left = center_x - ((float)i / (float)(num_bins / 2)) * (center_x);
					float y_bottom = height / 2.0f + amplitude;
					gs_vertex2f(x_left, y_bottom);
				}
				gs_render_stop(GS_LINESTRIP);

				// Right side glow (mirror)
				gs_render_start(true);
				for (size_t i = 0; i < num_bins / 2; i++) {
					size_t bin_idx = start_bin + i;
					float mag = smoothed_magnitudes[bin_idx] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);

					float x_right = center_x + ((float)i / (float)(num_bins / 2)) * (center_x);
					float y_top = height / 2.0f - amplitude;
					gs_vertex2f(x_right, y_top);
				}
				gs_render_stop(GS_LINESTRIP);

				gs_render_start(true);
				for (size_t i = 0; i < num_bins / 2; i++) {
					size_t bin_idx = start_bin + i;
					float mag = smoothed_magnitudes[bin_idx] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);

					float x_right = center_x + ((float)i / (float)(num_bins / 2)) * (center_x);
					float y_bottom = height / 2.0f + amplitude;
					gs_vertex2f(x_right, y_bottom);
				}
				gs_render_stop(GS_LINESTRIP);
			}

			// Draw main waveform
			gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(color_start));

			// Left top
			gs_render_start(true);
			for (size_t i = 0; i < num_bins / 2; i++) {
				size_t bin_idx = start_bin + i;
				float mag = smoothed_magnitudes[bin_idx] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x_left = center_x - ((float)i / (float)(num_bins / 2)) * (center_x);
				float y_top = height / 2.0f - amplitude;
				gs_vertex2f(x_left, y_top);
			}
			gs_render_stop(GS_LINESTRIP);

			// Left bottom
			gs_render_start(true);
			for (size_t i = 0; i < num_bins / 2; i++) {
				size_t bin_idx = start_bin + i;
				float mag = smoothed_magnitudes[bin_idx] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x_left = center_x - ((float)i / (float)(num_bins / 2)) * (center_x);
				float y_bottom = height / 2.0f + amplitude;
				gs_vertex2f(x_left, y_bottom);
			}
			gs_render_stop(GS_LINESTRIP);

			// Right top (mirror)
			gs_render_start(true);
			for (size_t i = 0; i < num_bins / 2; i++) {
				size_t bin_idx = start_bin + i;
				float mag = smoothed_magnitudes[bin_idx] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x_right = center_x + ((float)i / (float)(num_bins / 2)) * (center_x);
				float y_top = height / 2.0f - amplitude;
				gs_vertex2f(x_right, y_top);
			}
			gs_render_stop(GS_LINESTRIP);

			// Right bottom (mirror)
			gs_render_start(true);
			for (size_t i = 0; i < num_bins / 2; i++) {
				size_t bin_idx = start_bin + i;
				float mag = smoothed_magnitudes[bin_idx] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x_right = center_x + ((float)i / (float)(num_bins / 2)) * (center_x);
				float y_bottom = height / 2.0f + amplitude;
				gs_vertex2f(x_right, y_bottom);
			}
			gs_render_stop(GS_LINESTRIP);

		} else if (mode == 1) { // Symmetric Waveform (existing - vertical mirror)
			float center_y = height / 2.0f;
			float max_amplitude = height * 0.3f;

			// Draw glow layer first
			if (glow_strength > 0.01f) {
				gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(glow_color));

				// Top half glow
				gs_render_start(true);
				for (size_t i = 0; i < num_bins; i++) {
					float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);

					float x = (float)i / (float)num_bins * width;
					float y_top = center_y - amplitude;
					gs_vertex2f(x, y_top);
				}
				gs_render_stop(GS_LINESTRIP);

				// Bottom half glow
				gs_render_start(true);
				for (size_t i = 0; i < num_bins; i++) {
					float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);

					float x = (float)i / (float)num_bins * width;
					float y_bottom = center_y + amplitude;
					gs_vertex2f(x, y_bottom);
				}
				gs_render_stop(GS_LINESTRIP);
			}

			// Draw main waveform
			gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(color_start));

			// Top half
			gs_render_start(true);
			for (size_t i = 0; i < num_bins; i++) {
				float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x = (float)i / (float)num_bins * width;
				float y_top = center_y - amplitude;
				gs_vertex2f(x, y_top);
			}
			gs_render_stop(GS_LINESTRIP);

			// Bottom half (mirrored)
			gs_render_start(true);
			for (size_t i = 0; i < num_bins; i++) {
				float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x = (float)i / (float)num_bins * width;
				float y_bottom = center_y + amplitude;
				gs_vertex2f(x, y_bottom);
			}
			gs_render_stop(GS_LINESTRIP);

		} else if (mode == 2) { // Mirrored Bars (Vertical bars from center)
			float center_y = height / 2.0f;
			float max_amplitude = height * 0.4f;
			int count = 64; // Fixed bar count for now, or could reuse bar_count if we kept it
			if (count > (int)num_bins)
				count = (int)num_bins;

			size_t bins_per_bar = num_bins / count;
			if (bins_per_bar == 0)
				bins_per_bar = 1;

			float bar_width = width / count * 0.8f;

			// Draw glow
			if (glow_strength > 0.01f) {
				gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(glow_color));
				gs_render_start(true);
				for (int i = 0; i < count; i++) {
					float sum = 0.0f;
					for (size_t j = 0; j < bins_per_bar; j++) {
						size_t bin_idx = start_bin + i * bins_per_bar + j;
						if (bin_idx < smoothed_magnitudes.size())
							sum += smoothed_magnitudes[bin_idx];
					}
					float mag = (sum / bins_per_bar) * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);

					float x = (float)i / (float)count * width + (width / count * 0.1f);

					// Top bar (using TRISTRIP: TL, BL, TR, BR)
					gs_vertex2f(x, center_y);                         // TL
					gs_vertex2f(x, center_y - amplitude);             // BL
					gs_vertex2f(x + bar_width, center_y);             // TR
					gs_vertex2f(x + bar_width, center_y - amplitude); // BR
				}
				gs_render_stop(GS_TRISTRIP);
			}

			// Draw main bars
			gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(color_start));
			gs_render_start(true);
			for (int i = 0; i < count; i++) {
				float sum = 0.0f;
				for (size_t j = 0; j < bins_per_bar; j++) {
					size_t bin_idx = start_bin + i * bins_per_bar + j;
					if (bin_idx < smoothed_magnitudes.size())
						sum += smoothed_magnitudes[bin_idx];
				}
				float mag = (sum / bins_per_bar) * amp_scale;
				float amplitude = mag * max_amplitude;

				float x = (float)i / (float)count * width + (width / count * 0.1f);

				// Top bar (TRISTRIP)
				gs_vertex2f(x, center_y);
				gs_vertex2f(x, center_y - amplitude);
				gs_vertex2f(x + bar_width, center_y);
				gs_vertex2f(x + bar_width, center_y - amplitude);

				// Bottom bar (TRISTRIP)
				gs_vertex2f(x, center_y);
				gs_vertex2f(x, center_y + amplitude);
				gs_vertex2f(x + bar_width, center_y);
				gs_vertex2f(x + bar_width, center_y + amplitude);
			}
			gs_render_stop(GS_TRISTRIP);

		} else if (mode == 3) { // Filled Mirror (Solid waveform mirrored)
			float center_y = height / 2.0f;
			float max_amplitude = height * 0.4f;

			// Draw glow
			if (glow_strength > 0.01f) {
				gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(glow_color));

				// Top half
				gs_render_start(true);
				gs_vertex2f(0.0f, center_y); // Start at center-left
				for (size_t i = 0; i < num_bins; i++) {
					float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);
					float x = (float)i / (float)num_bins * width;
					gs_vertex2f(x, center_y - amplitude);
				}
				gs_vertex2f(width, center_y); // End at center-right
				gs_render_stop(
					GS_LINESTRIP); // Using linestrip for outline, or could use triangle strip for fill

				// Bottom half
				gs_render_start(true);
				gs_vertex2f(0.0f, center_y);
				for (size_t i = 0; i < num_bins; i++) {
					float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);
					float x = (float)i / (float)num_bins * width;
					gs_vertex2f(x, center_y + amplitude);
				}
				gs_vertex2f(width, center_y);
				gs_render_stop(GS_LINESTRIP);
			}

			// Draw main filled shape
			gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(color_start));

			// We use TRIANGLE_STRIP to create a filled effect from the center line
			gs_render_start(true);
			for (size_t i = 0; i < num_bins; i++) {
				float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x = (float)i / (float)num_bins * width;

				// Top point
				gs_vertex2f(x, center_y - amplitude);
				// Bottom point
				gs_vertex2f(x, center_y + amplitude);
			}
			gs_render_stop(GS_TRISTRIP);
		} else if (mode == 4) { // Pulse Line (Vertical line pulsing with volume)
			float center_x = width / 2.0f;
			float center_y = height / 2.0f;

			// Calculate average volume
			float sum = 0.0f;
			for (size_t i = 0; i < num_bins; i++) {
				sum += smoothed_magnitudes[start_bin + i];
			}
			float avg_vol = (sum / num_bins) * amp_scale;

			float current_height = height * 0.8f * (0.2f + avg_vol);
			if (current_height > height)
				current_height = height;

			float current_width = thickness * (1.0f + avg_vol * 2.0f);

			// Draw glow
			if (glow_strength > 0.01f) {
				gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(glow_color));
				float glow_w = current_width * (1.0f + glow_strength * 4.0f);

				gs_render_start(true);
				gs_vertex2f(center_x - glow_w / 2, center_y - current_height / 2);
				gs_vertex2f(center_x - glow_w / 2, center_y + current_height / 2);
				gs_vertex2f(center_x + glow_w / 2, center_y - current_height / 2);
				gs_vertex2f(center_x + glow_w / 2, center_y + current_height / 2);
				gs_render_stop(GS_TRISTRIP);
			}

			// Draw main line
			gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(color_start));
			gs_render_start(true);
			gs_vertex2f(center_x - current_width / 2, center_y - current_height / 2);
			gs_vertex2f(center_x - current_width / 2, center_y + current_height / 2);
			gs_vertex2f(center_x + current_width / 2, center_y - current_height / 2);
			gs_vertex2f(center_x + current_width / 2, center_y + current_height / 2);
			gs_render_stop(GS_TRISTRIP);

		} else if (mode == 5) { // Multi-Wave (3 overlapping waveforms)
			float center_y = height / 2.0f;
			float max_amplitude = height * 0.3f;

			// Helper to draw one wave
			auto draw_wave = [&](uint32_t col, float scale_mod, float y_offset) {
				gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(col));

				// Top half
				gs_render_start(true);
				for (size_t i = 0; i < num_bins; i++) {
					float mag = smoothed_magnitudes[start_bin + i] * amp_scale * scale_mod;
					float amplitude = mag * max_amplitude;
					float x = (float)i / (float)num_bins * width;
					gs_vertex2f(x, center_y + y_offset - amplitude);
				}
				gs_render_stop(GS_LINESTRIP);

				// Bottom half
				gs_render_start(true);
				for (size_t i = 0; i < num_bins; i++) {
					float mag = smoothed_magnitudes[start_bin + i] * amp_scale * scale_mod;
					float amplitude = mag * max_amplitude;
					float x = (float)i / (float)num_bins * width;
					gs_vertex2f(x, center_y + y_offset + amplitude);
				}
				gs_render_stop(GS_LINESTRIP);
			};

			// Draw 3 waves with offsets
			// Wave 1: Glow color, slightly larger, offset up
			draw_wave(glow_color, 1.1f, -5.0f);

			// Wave 2: End color, slightly smaller, offset down
			draw_wave(color_end, 0.9f, 5.0f);

			// Wave 3: Main color, normal size, center
			draw_wave(color_start, 1.0f, 0.0f);

		} else if (mode == 6) { // Symetric Dots
			float center_y = height / 2.0f;
			float max_amplitude = height * 0.3f;
			float dot_size = thickness * 2.0f;

			// Draw glow dots
			if (glow_strength > 0.01f) {
				gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(glow_color));
				gs_render_start(true);
				for (size_t i = 0; i < num_bins; i += 2) { // Skip every other bin for dots
					float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
					float amplitude = mag * max_amplitude * (1.0f + glow_strength * 0.5f);
					float x = (float)i / (float)num_bins * width;

					// Top dot
					float y1 = center_y - amplitude;
					gs_vertex2f(x - dot_size, y1 - dot_size);
					gs_vertex2f(x - dot_size, y1 + dot_size);
					gs_vertex2f(x + dot_size, y1 - dot_size);
					gs_vertex2f(x + dot_size, y1 + dot_size);

					// Bottom dot
					float y2 = center_y + amplitude;
					gs_vertex2f(x - dot_size, y2 - dot_size);
					gs_vertex2f(x - dot_size, y2 + dot_size);
					gs_vertex2f(x + dot_size, y2 - dot_size);
					gs_vertex2f(x + dot_size, y2 + dot_size);
				}
				gs_render_stop(GS_TRISTRIP); // Using TRISTRIP for dots
			}

			// Draw main dots
			gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), fix_color(color_start));
			gs_render_start(true);
			for (size_t i = 0; i < num_bins; i += 2) {
				float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
				float amplitude = mag * max_amplitude;
				float x = (float)i / (float)num_bins * width;

				// Top dot
				float y1 = center_y - amplitude;
				gs_vertex2f(x - dot_size / 2, y1 - dot_size / 2);
				gs_vertex2f(x - dot_size / 2, y1 + dot_size / 2);
				gs_vertex2f(x + dot_size / 2, y1 - dot_size / 2);
				gs_vertex2f(x + dot_size / 2, y1 + dot_size / 2);

				// Bottom dot
				float y2 = center_y + amplitude;
				gs_vertex2f(x - dot_size / 2, y2 - dot_size / 2);
				gs_vertex2f(x - dot_size / 2, y2 + dot_size / 2);
				gs_vertex2f(x + dot_size / 2, y2 - dot_size / 2);
				gs_vertex2f(x + dot_size / 2, y2 + dot_size / 2);
			}
			gs_render_stop(GS_TRISTRIP);
		}
	}
}

// OBS Source Callbacks

static const char *glass_line_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "GlassLine Visualizer";
}

static void *glass_line_create(obs_data_t *settings, obs_source_t *source)
{
	GlassLineSource *context = new GlassLineSource(source);
	context->Update(settings);
	return context;
}

static void glass_line_destroy(void *data)
{
	GlassLineSource *context = (GlassLineSource *)data;
	delete context;
}

static void glass_line_update(void *data, obs_data_t *settings)
{
	GlassLineSource *context = (GlassLineSource *)data;
	context->Update(settings);
}

static void glass_line_video_render(void *data, gs_effect_t *effect)
{
	GlassLineSource *context = (GlassLineSource *)data;
	context->Render(effect);
}

static uint32_t glass_line_get_width(void *data)
{
	UNUSED_PARAMETER(data);
	return 1920; // Default width
}

static uint32_t glass_line_get_height(void *data)
{
	UNUSED_PARAMETER(data);
	return 1080; // Default height
}

static void glass_line_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_MODE, 0);
	obs_data_set_default_int(settings, S_COLOR, 0xFFFFFFFF);
	obs_data_set_default_int(settings, S_COLOR_START, 0xFFFFE7C1);
	obs_data_set_default_int(settings, S_COLOR_END, 0xFFB63814);
	obs_data_set_default_int(settings, S_GLOW_COLOR, 0xFFFF7832);
	obs_data_set_default_double(settings, S_GLOW_STRENGTH, 0.5);
	obs_data_set_default_double(settings, S_THICKNESS, 2.0);
	obs_data_set_default_double(settings, S_LINE_WIDTH, 4.0);
	obs_data_set_default_double(settings, S_SMOOTHING, 0.5);
	obs_data_set_default_double(settings, S_AMP_SCALE, 1.0);
}

static obs_properties_t *glass_line_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_list(props, S_SOURCE, T_SOURCE, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	// Populate audio sources
	struct obs_audio_source_enum_data {
		obs_property_t *prop;
	} enum_data = {obs_properties_get(props, S_SOURCE)};

	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			auto *ed = (struct obs_audio_source_enum_data *)data;
			uint32_t flags = obs_source_get_output_flags(source);
			if ((flags & OBS_SOURCE_AUDIO) != 0) {
				const char *name = obs_source_get_name(source);
				obs_property_list_add_string(ed->prop, name, name);
			}
			return true;
		},
		&enum_data);

	obs_property_t *mode_list =
		obs_properties_add_list(props, S_MODE, T_MODE, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode_list, "Centered Waveform", 0);
	obs_property_list_add_int(mode_list, "Symmetric Waveform", 1);
	obs_property_list_add_int(mode_list, "Mirrored Bars", 2);
	obs_property_list_add_int(mode_list, "Filled Mirror", 3);
	obs_property_list_add_int(mode_list, "Pulse Line", 4);
	obs_property_list_add_int(mode_list, "Multi-Wave", 5);
	obs_property_list_add_int(mode_list, "Symetric Dots", 6);

	obs_properties_add_color(props, S_COLOR, T_COLOR);
	obs_properties_add_color(props, S_COLOR_START, T_COLOR_START);
	obs_properties_add_color(props, S_COLOR_END, T_COLOR_END);
	obs_properties_add_color(props, S_GLOW_COLOR, T_GLOW_COLOR);
	obs_properties_add_float_slider(props, S_GLOW_STRENGTH, T_GLOW_STRENGTH, 0.0f, 1.0f, 0.01f);
	obs_properties_add_float(props, S_THICKNESS, T_THICKNESS, 1.0f, 20.0f, 0.5f);
	obs_properties_add_float(props, S_LINE_WIDTH, T_LINE_WIDTH, 1.0f, 20.0f, 0.5f);
	obs_properties_add_float(props, S_SMOOTHING, T_SMOOTHING, 0.0f, 1.0f, 0.01f);
	obs_properties_add_float(props, S_AMP_SCALE, T_AMP_SCALE, 0.1f, 100.0f, 0.1f);

	return props;
}

struct obs_source_info glass_line_source = {
	.id = "glass_line_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = glass_line_get_name,
	.create = glass_line_create,
	.destroy = glass_line_destroy,
	.get_width = glass_line_get_width,
	.get_height = glass_line_get_height,
	.get_defaults = glass_line_get_defaults,
	.update = glass_line_update,
	.video_render = glass_line_video_render,
	.get_properties = glass_line_get_properties,
};

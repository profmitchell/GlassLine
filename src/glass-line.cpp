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
#define S_THICKNESS "thickness"
#define S_SMOOTHING "smoothing"
#define S_AMP_SCALE "amp_scale"
#define S_BAR_COUNT "bar_count"
#define S_RADIUS "radius"

#define T_SOURCE "Audio Source"
#define T_MODE "Visual Mode"
#define T_COLOR "Color"
#define T_THICKNESS "Thickness"
#define T_SMOOTHING "Smoothing"
#define T_AMP_SCALE "Amplitude Scale"
#define T_BAR_COUNT "Bar Count"
#define T_RADIUS "Radius"

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
	thickness = 2.0f;
	smoothing = 0.5f;
	amp_scale = 1.0f;
	bar_count = 64;
	radius = 200.0f;

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
	thickness = (float)obs_data_get_double(settings, S_THICKNESS);
	smoothing = (float)obs_data_get_double(settings, S_SMOOTHING);
	amp_scale = (float)obs_data_get_double(settings, S_AMP_SCALE);
	bar_count = (int)obs_data_get_int(settings, S_BAR_COUNT);
	radius = (float)obs_data_get_double(settings, S_RADIUS);
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
	gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), color);

	float width = (float)obs_source_get_width(source);
	float height = (float)obs_source_get_height(source);

	// Prepare data for visualization
	// We usually want to ignore the DC component (index 0) and very high frequencies
	// Let's use bins 1 to N/2
	size_t start_bin = 1;
	size_t end_bin = smoothed_magnitudes.size() / 2; // Use lower half of spectrum usually
	if (end_bin <= start_bin)
		return;

	size_t num_bins = end_bin - start_bin;

	while (gs_effect_loop(solid, "Solid")) {

		if (mode == 0) { // Line
			gs_render_start(true);

			for (size_t i = 0; i < num_bins; i++) {
				float mag = smoothed_magnitudes[start_bin + i] * amp_scale;
				float x = (float)i / (float)num_bins * width;
				float y = height - (mag * height); // Draw from bottom up
				if (y < 0)
					y = 0;
				if (y > height)
					y = height;

				gs_vertex2f(x, y);
			}

			gs_render_stop(GS_LINESTRIP);

		} else if (mode == 1) { // Bars
			int count = bar_count;
			if (count > (int)num_bins)
				count = (int)num_bins;

			gs_render_start(true);

			float bar_width = width / count;
			float gap = bar_width * 0.1f; // 10% gap
			float draw_width = bar_width - gap;

			size_t bins_per_bar = num_bins / count;
			if (bins_per_bar == 0)
				bins_per_bar = 1;

			for (int i = 0; i < count; i++) {
				// Average bins for this bar
				float sum = 0.0f;
				for (size_t j = 0; j < bins_per_bar; j++) {
					size_t bin_idx = start_bin + i * bins_per_bar + j;
					if (bin_idx < smoothed_magnitudes.size())
						sum += smoothed_magnitudes[bin_idx];
				}
				float mag = (sum / bins_per_bar) * amp_scale;
				float bar_height = mag * height;

				float x = i * bar_width;
				float y = height;

				// Quad (2 triangles)
				// Triangle 1
				gs_vertex2f(x, y);
				gs_vertex2f(x, y - bar_height);
				gs_vertex2f(x + draw_width, y - bar_height);

				// Triangle 2
				gs_vertex2f(x, y);
				gs_vertex2f(x + draw_width, y - bar_height);
				gs_vertex2f(x + draw_width, y);
			}

			gs_render_stop(GS_TRIS);

		} else if (mode == 2) { // Circular Line
			gs_render_start(true);

			float center_x = width / 2.0f;
			float center_y = height / 2.0f;

			for (size_t i = 0; i <= num_bins; i++) {
				size_t idx = (i == num_bins) ? 0 : i; // Wrap around
				float mag = smoothed_magnitudes[start_bin + idx] * amp_scale;
				float r = radius + (mag * radius * 0.5f);

				float angle = (float)i / (float)num_bins * 2.0f * (float)M_PI;

				float x = center_x + r * cosf(angle);
				float y = center_y + r * sinf(angle);

				gs_vertex2f(x, y);
			}

			gs_render_stop(GS_LINESTRIP);

		} else if (mode == 3) { // Circular Bars
			int count = bar_count;
			if (count > (int)num_bins)
				count = (int)num_bins;

			gs_render_start(true);

			float center_x = width / 2.0f;
			float center_y = height / 2.0f;
			size_t bins_per_bar = num_bins / count;
			if (bins_per_bar == 0)
				bins_per_bar = 1;

			for (int i = 0; i < count; i++) {
				float sum = 0.0f;
				for (size_t j = 0; j < bins_per_bar; j++) {
					size_t bin_idx = start_bin + i * bins_per_bar + j;
					if (bin_idx < smoothed_magnitudes.size())
						sum += smoothed_magnitudes[bin_idx];
				}
				float mag = (sum / bins_per_bar) * amp_scale;
				float r_inner = radius;
				float r_outer = radius + (mag * radius * 0.5f);

				float angle_start = (float)i / (float)count * 2.0f * (float)M_PI;
				float angle_end = (float)(i + 0.8f) / (float)count * 2.0f * (float)M_PI; // 0.8 for gap

				// Quad (2 triangles)
				float x0 = center_x + r_inner * cosf(angle_start);
				float y0 = center_y + r_inner * sinf(angle_start);
				float x1 = center_x + r_outer * cosf(angle_start);
				float y1 = center_y + r_outer * sinf(angle_start);
				float x2 = center_x + r_outer * cosf(angle_end);
				float y2 = center_y + r_outer * sinf(angle_end);
				float x3 = center_x + r_inner * cosf(angle_end);
				float y3 = center_y + r_inner * sinf(angle_end);

				// Triangle 1
				gs_vertex2f(x0, y0);
				gs_vertex2f(x1, y1);
				gs_vertex2f(x2, y2);

				// Triangle 2
				gs_vertex2f(x0, y0);
				gs_vertex2f(x2, y2);
				gs_vertex2f(x3, y3);
			}

			gs_render_stop(GS_TRIS);
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
	obs_property_list_add_int(mode_list, "Line", 0);
	obs_property_list_add_int(mode_list, "Bars", 1);
	obs_property_list_add_int(mode_list, "Circular Line", 2);
	obs_property_list_add_int(mode_list, "Circular Bars", 3);

	obs_properties_add_color(props, S_COLOR, T_COLOR);
	obs_properties_add_float(props, S_THICKNESS, T_THICKNESS, 1.0f, 20.0f, 0.5f);
	obs_properties_add_float(props, S_SMOOTHING, T_SMOOTHING, 0.0f, 1.0f, 0.01f);
	obs_properties_add_float(props, S_AMP_SCALE, T_AMP_SCALE, 0.1f, 100.0f, 0.1f);
	obs_properties_add_int(props, S_BAR_COUNT, T_BAR_COUNT, 4, 512, 1);
	obs_properties_add_float(props, S_RADIUS, T_RADIUS, 10.0f, 1000.0f, 1.0f);

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
	.update = glass_line_update,
	.video_render = glass_line_video_render,
	.get_properties = glass_line_get_properties,
};

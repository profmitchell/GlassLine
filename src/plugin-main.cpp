#include <obs-module.h>
#include "glass-line.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("glass-line", "en-US")

bool obs_module_load(void)
{
	obs_register_source(&glass_line_source);
	return true;
}

void obs_module_unload(void)
{
}

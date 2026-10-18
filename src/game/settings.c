/* Settings: GLtron read every rule from Lua (scripts/config.lua) each time it
 * was needed. Here a table of the same defaults, looked up by the same names.
 * GLtron Copyright (C) Andreas Umbach. This port: GPL v2 or later. */

#include "gltron.h"

#include <string.h>

typedef struct Setting { const char *name; float value; } Setting;

/* scripts/config.lua, the game rules */
static Setting settings[] = {
	{ "speed", 8.5f },                  /* lightcycle speed */
	{ "energy", 4.8f },                 /* lightcycle energy */
	{ "energy_increase", 0.4f },        /* energy increase per second */
	{ "booster_on", 1 },
	{ "booster_min", 1.0f },            /* min. energy so the booster can start */
	{ "booster_use", 1.5f },            /* energy used per second */
	{ "booster_speed_factor", 1.1f },
	{ "booster_speed_increase", 1.2f }, /* speed added per second */
	{ "booster_speed_decrease", 0.8f }, /* speed lost per second after boosting */
	{ "wall_accel_on", 0 },
	{ "wall_accel_max", 20.0f },
	{ "wall_accel_use", 1.0f },
	{ "wall_accel_decrease", 0.8f },
	{ "wall_buster_on", 1 },
	{ "wall_buster_use", 4.8f },        /* energy used per second */
	{ "wall_buster_min", 2.4f },        /* energy needed to start it */
	{ "players", 1 },
	{ "ai_opponents", 3 },
	{ "ai_level", 2 },
	{ "erase_crashed", 1 },
	{ "fast_finish", 1 },
};

SettingsCache gSettingsCache = { 2, 1 };   /* ai_level, fast_finish */

static Setting *find(const char *name)
{
	size_t i;
	for(i = 0; i < sizeof(settings) / sizeof(settings[0]); i++)
		if(strcmp(settings[i].name, name) == 0)
			return &settings[i];
	fprintf(stderr, "gltron: unknown setting '%s'\n", name);
	nebu_assert(0);
	return NULL;
}

float getSettingf(const char *name)
{
	Setting *s = find(name);
	return s ? s->value : 0;
}

int getSettingi(const char *name)
{
	return (int) getSettingf(name);
}

void setSettingf(const char *name, float value)
{
	Setting *s = find(name);
	if(s) s->value = value;
	if(strcmp(name, "ai_level") == 0) gSettingsCache.ai_level = (int) value;
	if(strcmp(name, "fast_finish") == 0) gSettingsCache.fast_finish = (int) value;
}

#pragma once

#include "fastfetch.h"

#define FF_WALLPAPER_MODULE_NAME "Wallpaper"

void ffPrintWallpaper(FFWallpaperOptions* options);
void ffInitWallpaperOptions(FFWallpaperOptions* options);
bool ffParseWallpaperCommandOptions(FFWallpaperOptions* options, const char* key, const char* value);
void ffDestroyWallpaperOptions(FFWallpaperOptions* options);
void ffParseWallpaperJsonObject(FFWallpaperOptions* options, yyjson_val* module);

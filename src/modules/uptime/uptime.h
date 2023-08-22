#pragma once

#include "fastfetch.h"

#define FF_UPTIME_MODULE_NAME "Uptime"

void ffPrintUptime(FFUptimeOptions* options);
void ffInitUptimeOptions(FFUptimeOptions* options);
bool ffParseUptimeCommandOptions(FFUptimeOptions* options, const char* key, const char* value);
void ffDestroyUptimeOptions(FFUptimeOptions* options);
void ffParseUptimeJsonObject(FFUptimeOptions* options, yyjson_val* module);

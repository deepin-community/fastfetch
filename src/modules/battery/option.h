#pragma once

// This file will be included in "fastfetch.h", do NOT put unnecessary things here

#include "common/option.h"

typedef struct FFBatteryOptions
{
    FFModuleBaseInfo moduleInfo;
    FFModuleArgs moduleArgs;

    bool temp;

    #ifdef __linux__
        FFstrbuf dir;
    #elif defined(_WIN32)
        bool useSetupApi;
    #endif
} FFBatteryOptions;

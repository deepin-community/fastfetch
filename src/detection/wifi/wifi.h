#pragma once

#ifndef FF_INCLUDED_detection_wifi_wifi
#define FF_INCLUDED_detection_wifi_wifi

#include "fastfetch.h"

struct FFWifiInterface
{
    FFstrbuf description;
    FFstrbuf status;
};

struct FFWifiConnection
{
    FFstrbuf status;
    FFstrbuf ssid;
    FFstrbuf macAddress;
    FFstrbuf protocol;
    FFstrbuf security;
    double signalQuality; // Percentage
    double rxRate;
    double txRate;
};

typedef struct FFWifiResult
{
    struct FFWifiInterface inf;
    struct FFWifiConnection conn;
} FFWifiResult;

const char* ffDetectWifi(const FFinstance* instance, FFlist* result /*list of FFWifiItem*/);

#endif

#pragma once

#include "fastfetch.h"

#include <stdint.h>

typedef struct FFVersion
{
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} FFVersion;

#define FF_VERSION_INIT ((FFVersion) {0})

void ffParseSemver(FFstrbuf* buffer, const FFstrbuf* major, const FFstrbuf* minor, const FFstrbuf* patch);
void ffParseGTK(FFstrbuf* buffer, const FFstrbuf* gtk2, const FFstrbuf* gtk3, const FFstrbuf* gtk4);

void ffVersionToPretty(const FFVersion* version, FFstrbuf* pretty);
int8_t ffVersionCompare(const FFVersion* version1, const FFVersion* version2);

void ffParseSize(uint64_t bytes, FFstrbuf* result);
void ffParseTemperature(double celsius, FFstrbuf* buffer);

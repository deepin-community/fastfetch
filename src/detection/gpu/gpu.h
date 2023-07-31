#pragma once

#ifndef FF_INCLUDED_detection_gpu_gpu
#define FF_INCLUDED_detection_gpu_gpu

#include "fastfetch.h"

#define FF_GPU_TEMP_UNSET (0/0.0)
#define FF_GPU_CORE_COUNT_UNSET -1
#define FF_GPU_VMEM_SIZE_UNSET ((uint64_t)-1)

extern const char* FF_GPU_VENDOR_NAME_APPLE;
extern const char* FF_GPU_VENDOR_NAME_AMD;
extern const char* FF_GPU_VENDOR_NAME_INTEL;
extern const char* FF_GPU_VENDOR_NAME_NVIDIA;

typedef enum FFGpuType
{
    FF_GPU_TYPE_UNKNOWN,
    FF_GPU_TYPE_INTEGRATED,
    FF_GPU_TYPE_DISCRETE,
} FFGpuType;

typedef struct FFGPUMemory
{
    uint64_t total;
    uint64_t used;
} FFGPUMemory;

typedef struct FFGPUResult
{
    FFGpuType type;
    FFstrbuf vendor;
    FFstrbuf name;
    FFstrbuf driver;
    double temperature;
    int coreCount;
    FFGPUMemory dedicated;
    FFGPUMemory shared;
    uint32_t vulkanDeviceId; // Only used for vulkan
} FFGPUResult;

const FFlist* ffDetectGPU(const FFinstance* instance);

const char* ffGetGPUVendorString(unsigned vendorId);

#endif

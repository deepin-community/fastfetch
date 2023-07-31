#pragma once

#ifndef FF_INCLUDED_detection_disk_disk
#define FF_INCLUDED_detection_disk_disk

#include "fastfetch.h"

typedef struct FFDisk
{
    FFstrbuf mountpoint;
    FFstrbuf filesystem;
    FFstrbuf name;
    FFDiskType type;

    uint64_t bytesUsed;
    uint64_t bytesTotal;

    uint32_t filesUsed;
    uint32_t filesTotal;
} FFDisk;

typedef struct FFDiskResult
{
    FFstrbuf error;
    FFlist disks; //List of FFDisk
} FFDiskResult;

/**
 * Returns a List of FFDisk, sorted alphabetically by mountpoint.
 * If error is not set, disks contains at least one disk.
 *
 * @return const FFDiskResult*
 */
const FFDiskResult* ffDetectDisks();

#endif

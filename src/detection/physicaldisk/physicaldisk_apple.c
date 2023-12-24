#include "physicaldisk.h"
#include "util/apple/cf_helpers.h"

#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/storage/IOStorageProtocolCharacteristics.h>

static inline void wrapIoObjectRelease(io_service_t* service)
{
    assert(service);
    if (*service)
        IOObjectRelease(*service);
}
#define FF_IOOBJECT_AUTO_RELEASE __attribute__((__cleanup__(wrapIoObjectRelease)))

const char* ffDetectPhysicalDisk(FFlist* result, FFPhysicalDiskOptions* options)
{
    FF_IOOBJECT_AUTO_RELEASE io_iterator_t iterator = 0;
    if(IOServiceGetMatchingServices(MACH_PORT_NULL, IOServiceMatching(kIOMediaClass), &iterator) != KERN_SUCCESS)
        return "IOServiceGetMatchingServices() failed";

    io_registry_entry_t registryEntry;
    while((registryEntry = IOIteratorNext(iterator)) != 0)
    {
        FF_IOOBJECT_AUTO_RELEASE io_registry_entry_t entryPartition = registryEntry;

        io_name_t deviceName;
        if (IORegistryEntryGetName(registryEntry, deviceName) != KERN_SUCCESS)
            continue;

        if (options->namePrefix.length && strncmp(deviceName, options->namePrefix.chars, options->namePrefix.length) != 0)
            continue;

        FF_IOOBJECT_AUTO_RELEASE io_registry_entry_t entryDriver = 0;
        if (IORegistryEntryGetParentEntry(entryPartition, kIOServicePlane, &entryDriver) != KERN_SUCCESS)
            continue;

        if (!IOObjectConformsTo(entryDriver, kIOBlockStorageDriverClass)) // physical disk only
            continue;

        FFPhysicalDiskResult* device = (FFPhysicalDiskResult*) ffListAdd(result);
        ffStrbufInit(&device->serial);
        ffStrbufInitS(&device->name, deviceName);
        ffStrbufInit(&device->devPath);
        ffStrbufInit(&device->interconnect);
        device->type = FF_PHYSICALDISK_TYPE_NONE;
        device->size = 0;

        FF_CFTYPE_AUTO_RELEASE CFBooleanRef removable = IORegistryEntryCreateCFProperty(entryPartition, CFSTR(kIOMediaRemovableKey), kCFAllocatorDefault, kNilOptions);
        device->type |= CFBooleanGetValue(removable) ? FF_PHYSICALDISK_TYPE_REMOVABLE : FF_PHYSICALDISK_TYPE_FIXED;

        FF_CFTYPE_AUTO_RELEASE CFStringRef bsdName = IORegistryEntryCreateCFProperty(entryPartition, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, kNilOptions);
        if (bsdName)
        {
            ffCfStrGetString(bsdName, &device->devPath);
            ffStrbufPrependS(&device->devPath, "/dev/");
        }

        FF_CFTYPE_AUTO_RELEASE CFNumberRef mediaSize = IORegistryEntryCreateCFProperty(entryPartition, CFSTR(kIOMediaSizeKey), kCFAllocatorDefault, kNilOptions);
        if (mediaSize)
            ffCfNumGetInt64(mediaSize, (int64_t*) &device->size);
        else
            device->size = 0;

        FF_IOOBJECT_AUTO_RELEASE io_registry_entry_t entryPhysical = 0;
        if (IORegistryEntryGetParentEntry(entryDriver, kIOServicePlane, &entryPhysical) == KERN_SUCCESS)
        {
            FF_CFTYPE_AUTO_RELEASE CFDictionaryRef protocolCharacteristics = IORegistryEntryCreateCFProperty(entryPhysical, CFSTR(kIOPropertyProtocolCharacteristicsKey), kCFAllocatorDefault, kNilOptions);
            if (protocolCharacteristics)
                ffCfDictGetString(protocolCharacteristics, CFSTR(kIOPropertyPhysicalInterconnectTypeKey), &device->interconnect);

            FF_CFTYPE_AUTO_RELEASE CFDictionaryRef deviceCharacteristics = IORegistryEntryCreateCFProperty(entryPhysical, CFSTR(kIOPropertyDeviceCharacteristicsKey), kCFAllocatorDefault, kNilOptions);
            if (deviceCharacteristics)
            {
                ffCfDictGetString(deviceCharacteristics, CFSTR(kIOPropertyProductSerialNumberKey), &device->serial);

                CFStringRef mediumType = (CFStringRef) CFDictionaryGetValue(deviceCharacteristics, CFSTR(kIOPropertyMediumTypeKey));
                if (mediumType)
                {
                    if (CFStringCompare(mediumType, CFSTR(kIOPropertyMediumTypeSolidStateKey), 0) == 0)
                        device->type |= FF_PHYSICALDISK_TYPE_SSD;
                    else if (CFStringCompare(mediumType, CFSTR(kIOPropertyMediumTypeRotationalKey), 0) == 0)
                        device->type |= FF_PHYSICALDISK_TYPE_HDD;
                }
            }
        }
    }

    return NULL;
}

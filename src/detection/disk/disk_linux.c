#include "disk.h"

#include "common/io/io.h"
#include "util/stringUtils.h"

#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <mntent.h>
#include <sys/mount.h>

#ifdef __USE_LARGEFILE64
    #define stat stat64
    #define statvfs statvfs64
    #define dirent dirent64
    #define readdir readdir64
#endif

static bool isPhysicalDevice(const struct mntent* device)
{
    #ifndef __ANDROID__ //On Android, `/dev` is not accessable, so that the following checks always fail

    //Always show the root path
    if(ffStrEquals(device->mnt_dir, "/"))
        return true;

    //DrvFs is a filesystem plugin to WSL that was designed to support interop between WSL and the Windows filesystem.
    if(ffStrEquals(device->mnt_fsname, "drvfs"))
        return true;

    //ZFS pool
    if(ffStrEquals(device->mnt_type, "zfs"))
        return true;

    //Pseudo filesystems don't have a device in /dev
    if(!ffStrStartsWith(device->mnt_fsname, "/dev/"))
        return false;

    if(
        ffStrStartsWith(device->mnt_fsname + 5, "loop") || //Ignore loop devices
        ffStrStartsWith(device->mnt_fsname + 5, "ram")  || //Ignore ram devices
        ffStrStartsWith(device->mnt_fsname + 5, "fd")      //Ignore fd devices
    ) return false;

    struct stat deviceStat;
    if(stat(device->mnt_fsname, &deviceStat) != 0)
        return false;

    //Ignore all devices that are not block devices
    if(!S_ISBLK(deviceStat.st_mode))
        return false;

    #else

    //Pseudo filesystems don't have a device in /dev
    if(!ffStrStartsWith(device->mnt_fsname, "/dev/"))
        return false;

    if(
        ffStrStartsWith(device->mnt_fsname + 5, "loop") || //Ignore loop devices
        ffStrStartsWith(device->mnt_fsname + 5, "ram")  || //Ignore ram devices
        ffStrStartsWith(device->mnt_fsname + 5, "fd")      //Ignore fd devices
    ) return false;

    #endif // __ANDROID__

    return true;
}

static void detectNameFromPath(FFDisk* disk, const struct stat* deviceStat, FFstrbuf* basePath)
{
    FF_AUTO_CLOSE_DIR DIR* dir = opendir(basePath->chars);
    if(dir == NULL)
        return;

    uint32_t basePathLength = basePath->length;

    struct dirent* entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(entry->d_name[0] == '.')
            continue;

        ffStrbufAppendS(basePath, entry->d_name);

        struct stat entryStat;
        bool ret = stat(basePath->chars, &entryStat) == 0;

        ffStrbufSubstrBefore(basePath, basePathLength);

        if(!ret || deviceStat->st_ino != entryStat.st_ino)
            continue;

        ffStrbufAppendS(&disk->name, entry->d_name);
        break;
    }
}

static void detectName(FFDisk* disk)
{
    struct stat deviceStat;
    if(stat(disk->mountFrom.chars, &deviceStat) != 0)
        return;

    FF_STRBUF_AUTO_DESTROY basePath = ffStrbufCreate();

    //Detect USB devices. Code is put here to reuse deviceStat
    //https://stackoverflow.com/a/73302717
    ffStrbufAppendS(&basePath, "/dev/disk/by-id/");
    detectNameFromPath(disk, &deviceStat, &basePath);
    if (ffStrbufStartsWithS(&disk->name, "usb-"))
        disk->type = FF_DISK_VOLUME_TYPE_EXTERNAL_BIT;
    ffStrbufClear(&disk->name);

    //Try label first
    ffStrbufSetS(&basePath, "/dev/disk/by-label/");
    detectNameFromPath(disk, &deviceStat, &basePath);

    if(disk->name.length == 0)
    {
        //Try partlabel second
        ffStrbufSetS(&basePath, "/dev/disk/by-partlabel/");
        detectNameFromPath(disk, &deviceStat, &basePath);
    }

    if (disk->name.length == 0) return;

    // Basic\x20data\x20partition
    for (uint32_t i = ffStrbufFirstIndexS(&disk->name, "\\x");
        i != disk->name.length;
        i = ffStrbufNextIndexS(&disk->name, i + 1, "\\x"))
    {
        uint32_t len = (uint32_t) strlen("\\x20");
        if (disk->name.length >= len)
        {
            char bak = disk->name.chars[i + len];
            disk->name.chars[i + len] = '\0';
            disk->name.chars[i] = (char) strtoul(&disk->name.chars[i + 2], NULL, 16);
            ffStrbufRemoveSubstr(&disk->name, i + 1, i + len);
            disk->name.chars[i + 1] = bak;
        }
    }
}

#ifdef __ANDROID__

static void detectType(FF_MAYBE_UNUSED const FFlist* disks, FFDisk* currentDisk)
{
    if(ffStrbufEqualS(&currentDisk->mountpoint, "/") || ffStrbufEqualS(&currentDisk->mountpoint, "/storage/emulated"))
        currentDisk->type = FF_DISK_VOLUME_TYPE_REGULAR_BIT;
    else if(ffStrbufStartsWithS(&currentDisk->mountpoint, "/mnt/media_rw/"))
        currentDisk->type = FF_DISK_VOLUME_TYPE_EXTERNAL_BIT;
    else
        currentDisk->type = FF_DISK_VOLUME_TYPE_HIDDEN_BIT;
}

#else

static bool isSubvolume(const FFlist* disks, FFDisk* currentDisk)
{
    if(ffStrbufEqualS(&currentDisk->mountFrom, "drvfs")) // WSL Windows drives
        return false;

    if(ffStrbufEqualS(&currentDisk->filesystem, "zfs"))
    {
        //ZFS subvolumes
        uint32_t index = ffStrbufFirstIndexC(&currentDisk->mountFrom, '/');
        if (index == currentDisk->mountFrom.length)
            return false;

        FF_STRBUF_AUTO_DESTROY zpoolName = ffStrbufCreateNS(index, currentDisk->mountFrom.chars);
        for(uint32_t i = 0; i < disks->length - 1; i++)
        {
            const FFDisk* otherDevice = ffListGet(disks, i);
            if(ffStrbufEqualS(&otherDevice->filesystem, "zfs") && ffStrbufStartsWith(&otherDevice->mountFrom, &zpoolName))
                return true;
        }

        return false;
    }
    else
    {
        //Filter all disks which device was already found. This catches BTRFS subvolumes.
        for(uint32_t i = 0; i < disks->length - 1; i++)
        {
            const FFDisk* otherDevice = ffListGet(disks, i);

            if(ffStrbufEqual(&currentDisk->mountFrom, &otherDevice->mountFrom))
                return true;
        }
    }

    return false;
}

static bool detectPhysicalTypeAndReturnRemovable(FFDisk* currentDisk)
{
    // https://stackoverflow.com/a/73302025
    // Note my USB mobile hard disk isn't detected as removable, but my USB flash disk does.
    // USB devices should be handled in detectName
    FF_STRBUF_AUTO_DESTROY basePath = ffStrbufCreateS("/sys/block/");

    FF_AUTO_CLOSE_DIR DIR* dir = opendir(basePath.chars);
    if(dir == NULL)
        return false;

    uint32_t index = ffStrbufLastIndexC(&currentDisk->mountFrom, '/');
    const char* partitionName = index == currentDisk->mountFrom.length ? NULL : currentDisk->mountFrom.chars + index + 1;
    if (!partitionName || !*partitionName) return false;

    struct dirent* entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(entry->d_name[0] == '.')
            continue;

        if (!ffStrStartsWith(partitionName, entry->d_name)) continue;

        ffStrbufAppendS(&basePath, entry->d_name);
        index = basePath.length;
        // /sys/block/sdx/queue/rotational
        ffStrbufAppendS(&basePath, "/queue/rotational");
        FF_STRBUF_AUTO_DESTROY buffer = ffStrbufCreate();
        if (ffReadFileBuffer(basePath.chars, &buffer))
            currentDisk->physicalType = ffStrbufEqualS(&buffer, "1") ? FF_DISK_PHYSICAL_TYPE_HDD : FF_DISK_PHYSICAL_TYPE_SSD;
        else
            currentDisk->physicalType = FF_DISK_PHYSICAL_TYPE_UNKNOWN;
        ffStrbufSubstrBefore(&basePath, index);

        // /sys/block/sdx/removable
        ffStrbufAppendS(&basePath, "/removable");

        return ffReadFileBuffer(basePath.chars, &buffer) && ffStrbufEqualS(&buffer, "1");
    }

    return false;
}

static void detectType(const FFlist* disks, FFDisk* currentDisk)
{
    bool isRemovable = detectPhysicalTypeAndReturnRemovable(currentDisk);

    if(currentDisk->type != FF_DISK_VOLUME_TYPE_NONE) return;
    if(isRemovable)
        currentDisk->type = FF_DISK_VOLUME_TYPE_EXTERNAL_BIT;
    else if(isSubvolume(disks, currentDisk))
        currentDisk->type = FF_DISK_VOLUME_TYPE_SUBVOLUME_BIT;
    else if(ffStrbufStartsWithS(&currentDisk->mountpoint, "/boot") || ffStrbufStartsWithS(&currentDisk->mountpoint, "/efi"))
        currentDisk->type = FF_DISK_VOLUME_TYPE_HIDDEN_BIT;
    else
        currentDisk->type = FF_DISK_VOLUME_TYPE_REGULAR_BIT;
}

#endif

static void detectStats(FFDisk* disk)
{
    struct statvfs fs;
    if(statvfs(disk->mountpoint.chars, &fs) != 0)
        memset(&fs, 0, sizeof(struct statvfs)); //Set all values to 0, so our values get initialized to 0 too

    disk->bytesTotal = fs.f_blocks * fs.f_frsize;
    disk->bytesFree = fs.f_bfree * fs.f_frsize;
    disk->bytesAvailable = fs.f_bavail * fs.f_frsize;
    disk->bytesUsed = 0; // To be filled in ./disk.c

    disk->filesTotal = (uint32_t) fs.f_files;
    disk->filesUsed = (uint32_t) (disk->filesTotal - fs.f_ffree);

    if(fs.f_flag & ST_RDONLY)
        disk->type |= FF_DISK_VOLUME_TYPE_READONLY_BIT;
}

const char* ffDetectDisksImpl(FFlist* disks)
{
    FILE* mountsFile = setmntent("/proc/mounts", "r");
    if(mountsFile == NULL)
        return "setmntent(\"/proc/mounts\", \"r\") == NULL";

    struct mntent* device;

    while((device = getmntent(mountsFile)))
    {
        if(!isPhysicalDevice(device))
            continue;

        //We have a valid device, add it to the list
        FFDisk* disk = ffListAdd(disks);
        disk->type = FF_DISK_VOLUME_TYPE_NONE;
        disk->physicalType = FF_DISK_PHYSICAL_TYPE_UNKNOWN;

        //detect mountFrom
        ffStrbufInitS(&disk->mountFrom, device->mnt_fsname);

        //detect mountpoint
        ffStrbufInitS(&disk->mountpoint, device->mnt_dir);

        //detect filesystem
        ffStrbufInitS(&disk->filesystem, device->mnt_type);

        //detect name
        ffStrbufInit(&disk->name);
        detectName(disk); // Also detects external devices

        //detect type
        detectType(disks, disk);

        //Detects stats
        detectStats(disk);
    }

    endmntent(mountsFile);

    return NULL;
}

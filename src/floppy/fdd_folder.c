/*
 * 86Box    Folder-backed FAT12 floppy images.
 *
 *          This file is part of the 86Box distribution.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dir.h>
#include <86box/fdd_folder.h>

#include "vendor/fatfs/ff.h"
#include "vendor/fatfs/diskio.h"
#define MINIZ_NO_TIME
#include "../miniz_reader.h"

/* FatFs uses a single logical volume during import on each calling thread. */
static _Thread_local struct {
    uint8_t *data;
    LBA_t    sectors;
    WORD     sector_size;
} ram_disk;

DSTATUS
disk_status(BYTE drive)
{
    return drive == 0 && ram_disk.data ? 0 : STA_NOINIT;
}

DSTATUS
disk_initialize(BYTE drive)
{
    return disk_status(drive);
}

DRESULT
disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (disk_status(drive))
        return RES_NOTRDY;
    if (!count || sector >= ram_disk.sectors || count > ram_disk.sectors - sector)
        return RES_PARERR;
    memcpy(buffer, ram_disk.data + (size_t) sector * ram_disk.sector_size,
           (size_t) count * ram_disk.sector_size);
    return RES_OK;
}

DRESULT
disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    if (disk_status(drive))
        return RES_NOTRDY;
    if (!count || sector >= ram_disk.sectors || count > ram_disk.sectors - sector)
        return RES_PARERR;
    memcpy(ram_disk.data + (size_t) sector * ram_disk.sector_size, buffer,
           (size_t) count * ram_disk.sector_size);
    return RES_OK;
}

DRESULT
disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    if (disk_status(drive))
        return RES_NOTRDY;
    if (command == CTRL_SYNC)
        return RES_OK;
    if (command == GET_SECTOR_SIZE) {
        *(WORD *) buffer = ram_disk.sector_size;
        return RES_OK;
    }
    return RES_PARERR;
}

static void
put_le16(uint8_t *dest, unsigned value)
{
    dest[0] = value & 0xff;
    dest[1] = value >> 8;
}

static void
init_fat12(const disk_size_t *format)
{
    uint8_t *boot = ram_disk.data;
    boot[0]       = 0xeb;
    boot[1]       = 0x3c;
    boot[2]       = 0x90;
    memcpy(boot + 3, "86BOX5.0", 8);
    put_le16(boot + 0x0b, ram_disk.sector_size);
    boot[0x0d] = format->spc;
    put_le16(boot + 0x0e, 1);
    boot[0x10] = format->num_fats;
    put_le16(boot + 0x11, format->root_dir_entries);
    put_le16(boot + 0x13, ram_disk.sectors);
    boot[0x15] = format->media_desc;
    put_le16(boot + 0x16, format->spfat);
    put_le16(boot + 0x18, format->sectors);
    put_le16(boot + 0x1a, format->sides);
    boot[0x26] = 0x29;
    memcpy(boot + 0x2b, "NO NAME    FAT12   ", 19);
    /* INT 18h returns control to the IBM PC BIOS. PC-98 uses a loop. */
    if (ram_disk.sector_size == 1024)
        memcpy(boot + 0x3e, "\xeb\xfe", 2);
    else
        memcpy(boot + 0x3e, "\xcd\x18\xeb\xfe", 4);
    boot[510] = 0x55;
    boot[511] = 0xaa;

    for (int i = 0; i < format->num_fats; i++) {
        uint8_t *fat = boot + (1 + i * format->spfat) * ram_disk.sector_size;
        fat[0]       = format->media_desc;
        fat[1] = fat[2] = 0xff;
    }
}

static fdd_folder_result_t
copy_file(const char *source, const char *target, uint8_t *buffer)
{
    FILE *host = plat_fopen(source, "rb");
    if (!host)
        return FDD_FOLDER_READ_ERROR;

    FIL                 file;
    fdd_folder_result_t result = FDD_FOLDER_OK;
    if (f_open(&file, target, FA_WRITE | FA_CREATE_NEW) != FR_OK) {
        result = FDD_FOLDER_CANNOT_CREATE;
    } else {
        size_t count;
        while ((count = fread(buffer, 1, 4096, host))) {
            UINT written;
            if (f_write(&file, buffer, (UINT) count, &written) != FR_OK || written != count) {
                result = FDD_FOLDER_FULL;
                break;
            }
        }
        if (ferror(host))
            result = FDD_FOLDER_READ_ERROR;
        if (f_close(&file) != FR_OK && result == FDD_FOLDER_OK)
            result = FDD_FOLDER_CANNOT_CREATE;
    }
    fclose(host);
    return result;
}

int
fdd_folder_source(char *path)
{
    if (plat_dir_check(path))
        return 1;
    const char *extension = path_get_extension(path);
    return extension && !strcasecmp(extension, "zip");
}

static int
valid_component(const char *name, size_t length)
{
    if (!length || name[0] == ' ' || name[length - 1] == ' ' || name[length - 1] == '.')
        return 0;
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char) name[i] < ' ' || strchr("/\\:*?\"<>|", name[i]))
            return 0;
    }
    return 1;
}

static fdd_folder_result_t
copy_directory(const char *source, char *target, size_t capacity, uint8_t *buffer, unsigned depth)
{
    plat_dir_t          directory = PLAT_DIR_INIT;
    fdd_folder_result_t result    = FDD_FOLDER_OK;
    const size_t        parent    = strlen(target);

    /* Each level holds a host directory handle and its enumeration buffer. */
    if (depth >= 64)
        return FDD_FOLDER_INVALID_NAME;

    if (!plat_dir_open(&directory, source)) {
        plat_dir_close(&directory);
        return FDD_FOLDER_READ_ERROR;
    }

    while (plat_dir_read_no_links(&directory)) {
        const int is_dir = plat_dir_is_dir(&directory);
        if (!is_dir && !plat_dir_is_file(&directory))
            continue;

        const char  *name   = plat_dir_get_name(&directory);
        const size_t length = strlen(name);
        if (parent + length + 2 > capacity || !valid_component(name, length)) {
            result = FDD_FOLDER_INVALID_NAME;
            break;
        }

        target[parent] = '/';
        memcpy(target + parent + 1, name, length + 1);
        if (is_dir) {
            if (f_mkdir(target) != FR_OK)
                result = FDD_FOLDER_CANNOT_CREATE;
            else
                result = copy_directory(plat_dir_get_path(&directory), target, capacity, buffer, depth + 1);
        } else if ((uint64_t) plat_dir_get_size(&directory) > (uint64_t) ram_disk.sectors * ram_disk.sector_size)
            result = FDD_FOLDER_FULL;
        else
            result = copy_file(plat_dir_get_path(&directory), target, buffer);

        target[parent] = '\0';
        if (result != FDD_FOLDER_OK)
            break;
    }
    plat_dir_close(&directory);
    return result;
}

typedef struct {
    FIL     *file;
    uint64_t remaining;
    int      full;
} zip_output_t;

static size_t
write_zip_entry(void *opaque, UNUSED(mz_uint64 offset), const void *data, size_t size)
{
    zip_output_t *output = opaque;
    UINT          written;

    if (size > output->remaining)
        return 0;
    if (f_write(output->file, data, (UINT) size, &written) != FR_OK || written != size) {
        output->full = 1;
        return 0;
    }
    output->remaining -= size;
    return size;
}

static fdd_folder_result_t
copy_zip_entry(mz_zip_archive *archive, mz_uint index, char *target, size_t capacity)
{
    mz_zip_archive_file_stat entry;
    if (!mz_zip_reader_file_stat(archive, index, &entry))
        return FDD_FOLDER_READ_ERROR;

    /* Unix and macOS ZIPs can carry symbolic links and special files. */
    const unsigned origin = entry.m_version_made_by >> 8;
    const unsigned type   = (entry.m_external_attr >> 16) & 0170000;
    if ((origin == 3 || origin == 19) && type && type != 0100000 && type != 0040000)
        return FDD_FOLDER_OK;

    char *name           = target + 3;
    target[2]            = '/';
    const mz_uint length = mz_zip_reader_get_filename(archive, index, NULL, 0);
    if (length < 2 || length > capacity - 3)
        return FDD_FOLDER_INVALID_NAME;
    if (mz_zip_reader_get_filename(archive, index, name, length) != length || strlen(name) != length - 1)
        return FDD_FOLDER_INVALID_NAME;

    /* ZIP paths use forward slashes and may omit explicit parent entries. */
    for (char *part = name;;) {
        char *separator = strchr(part, '/');
        if (!valid_component(part, separator ? (size_t) (separator - part) : strlen(part)))
            return FDD_FOLDER_INVALID_NAME;
        if (!separator)
            break;
        part = separator + 1;
        if (!*part) {
            *separator           = '\0';
            entry.m_is_directory = 1;
            break;
        }
        *separator           = '\0';
        const FRESULT result = f_mkdir(target);
        *separator           = '/';
        if (result != FR_OK && result != FR_EXIST)
            return FDD_FOLDER_CANNOT_CREATE;
    }

    if (entry.m_is_directory) {
        const FRESULT result = f_mkdir(target);
        return (result == FR_OK || result == FR_EXIST) ? FDD_FOLDER_OK : FDD_FOLDER_CANNOT_CREATE;
    }
    if (!entry.m_is_supported || (!entry.m_comp_size && entry.m_uncomp_size))
        return FDD_FOLDER_READ_ERROR;
    if (entry.m_uncomp_size > (uint64_t) ram_disk.sectors * ram_disk.sector_size)
        return FDD_FOLDER_FULL;

    FIL file;
    if (f_open(&file, target, FA_WRITE | FA_CREATE_NEW) != FR_OK)
        return FDD_FOLDER_CANNOT_CREATE;

    zip_output_t  output    = { .file = &file, .remaining = entry.m_uncomp_size };
    const int     extracted = mz_zip_reader_extract_to_callback(archive, index, write_zip_entry, &output, 0);
    const FRESULT closed    = f_close(&file);
    if (output.full)
        return FDD_FOLDER_FULL;
    if (!extracted || output.remaining)
        return FDD_FOLDER_READ_ERROR;
    return closed == FR_OK ? FDD_FOLDER_OK : FDD_FOLDER_CANNOT_CREATE;
}

static fdd_folder_result_t
copy_zip(const char *path, char *target, size_t capacity)
{
    FILE *source = plat_fopen(path, "rb");
    if (!source)
        return FDD_FOLDER_READ_ERROR;

    mz_zip_archive      archive = { 0 };
    fdd_folder_result_t result  = FDD_FOLDER_READ_ERROR;
    if (mz_zip_reader_init_cfile(&archive, source, 0, MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        result              = FDD_FOLDER_OK;
        const mz_uint count = mz_zip_reader_get_num_files(&archive);
        for (mz_uint i = 0; i < count && result == FDD_FOLDER_OK; i++)
            result = copy_zip_entry(&archive, i, target, capacity);
        mz_zip_reader_end(&archive);
    }
    fclose(source);
    return result;
}

fdd_folder_result_t
fdd_folder_create(const char *path, const disk_size_t *format, uint8_t **image)
{
    const LBA_t sectors     = format->tracks * format->sides * format->sectors;
    const WORD  sector_size = 128U << format->sector_len;
    uint8_t    *data        = calloc(sectors, sector_size);
    if (!data)
        return FDD_FOLDER_NO_MEMORY;

    ram_disk.data        = data;
    ram_disk.sectors     = sectors;
    ram_disk.sector_size = sector_size;
    init_fat12(format);

    FATFS               volume;
    char                target[4096] = "0:";
    uint8_t             buffer[4096];
    fdd_folder_result_t result = FDD_FOLDER_CANNOT_CREATE;
    if (f_mount(&volume, "0:", 1) == FR_OK)
        result = plat_dir_check((char *) path)
            ? copy_directory(path, target, sizeof(target), buffer, 0)
            : copy_zip(path, target, sizeof(target));
    f_mount(NULL, "0:", 0);
    ram_disk.data = NULL;

    if (result == FDD_FOLDER_OK)
        *image = data;
    else
        free(data);
    return result;
}

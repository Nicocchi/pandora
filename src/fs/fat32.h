#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ata.h"

#define FAT32_SECTOR_SIZE           512
#define FAT32_FIRST_CLUSTER         2
// FAT32 end-of-chain values are 0x0FFFFFF8 .. 0x0FFFFFFF
#define FAT32_EOC_MIN               0x0FFFFFF8
#define MAX_DIR_ENTRIES_PER_SEC     16

enum FatAttribute : uint8_t
{
    FA_READ_ONLY = 0x01,
    FA_HIDDEN = 0x02,
    FA_SYSTEM = 0x04,
    FA_VOLUME_LABEL = 0x08,
    FA_DIRECTORY = 0x10,
    FA_ARCHIVE = 0x20,
    FA_LFN      = 0x0F, // Long file name entry (all lower bits set)
};

struct FAT32BootSector
{
    uint8_t  jump[3];                   // 0x00
    char     oem_name[8];               // 0x03

    // DOS 3.31 BPB
    uint16_t bytes_per_sector;          // 0x0B
    uint8_t  sectors_per_cluster;       // 0x0D
    uint16_t reserved_sector_count;     // 0x0E  <-- was uint8_t, off by 1 byte
    uint8_t  fat_count;                 // 0x10  <-- was uint16_t, off by 1 byte
    uint16_t root_entry_count;          // 0x11  (0 for FAT32)
    uint16_t total_sectors_16;          // 0x13  (0 for FAT32)
    uint8_t  media_type;                // 0x15
    uint16_t fat_size_16;               // 0x16  (0 for FAT32)
    uint16_t sectors_per_track;         // 0x18
    uint16_t head_count;                // 0x1A
    uint32_t hidden_sectors;            // 0x1C
    uint32_t total_sectors_32;          // 0x20

    // FAT32 extended BPB
    uint32_t fat_size_32;               // 0x24  sectors per FAT
    uint16_t ext_flags;                 // 0x28
    uint16_t fs_version;                // 0x2A
    uint32_t root_cluster;              // 0x2C  (usually 2)
    uint16_t fs_info_sector;            // 0x30
    uint16_t backup_boot_sector;        // 0x32
    uint8_t  reserved[12];              // 0x34
    uint8_t  drive_number;              // 0x40
    uint8_t  reserved1;                 // 0x41
    uint8_t  boot_signature;            // 0x42
    uint32_t volume_id;                 // 0x43
    char     volume_label[11];          // 0x47
    char     fs_type[8];                // 0x52
} __attribute__((packed));

static_assert(sizeof(FAT32BootSector) == 90, "FAT32BootSector layout wrong");

struct Fat32DirEntry
{
    char name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct FatVolume
{
    // Filled in by FatMount from the BPB
    uint64_t partition_lba_start;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t fat_size_sectors;
    uint8_t fat_count;
    uint16_t bytes_per_sector;

    ATADrive* drive;

    // I/O callbacks - set by Fat32Init
    // void (*read_sector)(uint8_t *buffer, uint64_t lba);
    // void (*write_sector)(const uint8_t *buffer, uint64_t lba);
};

// Open file/directory handle
struct FatFile
{
    char name[256];
    uint8_t attributes;
    uint32_t file_size;
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t cluster_offset;
    uint64_t pos;
    bool is_dir;
    FatVolume *vol;
};

// void Fat32Init(void (*read_sec)(uint8_t*, uint64_t), void (*write_sec)(const uint8_t*, uint64_t));


// FatVolume* FatMount(uint64_t partition_lba_start);
FatVolume* FatMount(ATADrive* drive, uint64_t partition_lba_start);

void FatUnmount(FatVolume* vol);

FatFile* FatOpen(FatVolume* vol, const char* path);

void FatClose(FatFile* f);

size_t FatRead(FatFile* f, void* buf, size_t len);

size_t FatWrite(FatFile* f, const void* buf, size_t len);

// Allocate one free cluster in the FAT, mark it end-of-chain.
// Returns the cluster number, or 0 on failure (disk full).
uint32_t FatAllocCluster(FatVolume* vol);

// Link `cluster` -> `next` in the FAT (overwrites whatever was there).
void FatChainLink(FatVolume* vol, uint32_t cluster, uint32_t next);

// Create a new empty file at `path`. The parent directory must exist.
// Returns a heap-allocated FatFile open for writing, nullptr on failure.
// Fails if the file already exists — call FatOpen instead.
FatFile* FatCreate(FatVolume* vol, const char* path);

// Update the file size field in the directory entry on disk.
// Call after finishing a sequence of FatWrite calls.
bool FatFlushSize(FatVolume* vol, const char* path, uint32_t new_size);

// Print directory entries (8.3 short names) under path to kprintf/serial.
// Use "/" for the volume root. Skips "." and ".." and long-filename slots.
void FatListDir(FatVolume* vol, const char* path);

// One decoded directory entry, filled by FatReadDir. Layout is shared with
// userspace (see programs/ls), so keep it plain/POD and stable.
struct FatDirInfo
{
    char name[64];      // 8.3 short name, NUL-terminated
    uint32_t size;      // file size in bytes (0 for directories)
    uint8_t is_dir;     // non-zero if this entry is a directory
};

// Enumerate directory `path` into the caller-provided `out` array (capacity
// `max` entries). Skips ".", ".." and long-filename / volume-label slots.
// Returns the number of entries written, or -1 on error (bad path / not a dir).
int FatReadDir(FatVolume* vol, const char* path, FatDirInfo* out, int max);
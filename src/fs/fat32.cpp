#include "fat32.h"
#include "memory/heap.h"
#include "lib/string.h"
#include "lib/stdio.h"
#include "drivers/serial_port.h"

// static void (*g_read_sector)(uint8_t*, uint64_t)    = nullptr;
// static void (*g_write_sector)(const uint8_t*, uint64_t) = nullptr;

// void Fat32Init(void (*read_sec)(uint8_t*, uint64_t), void (*write_sec)(const uint8_t*, uint64_t))
// {
//     g_read_sector  = read_sec;
//     g_write_sector = write_sec;
// }

static inline uint64_t ClusterToLba(const FatVolume* vol, uint32_t cluster)
{
    return vol->data_start_lba + (uint64_t)(cluster - FAT32_FIRST_CLUSTER) * vol->sectors_per_cluster;
}

static inline void ReadSector(const FatVolume* vol, uint8_t* buf, uint64_t lba)
{
    // vol->read_sector(buf, lba);
    vol->drive->Read((uint32_t)lba, 1, buf);
}

static inline void WriteSector(const FatVolume* vol, uint8_t* buf, uint64_t lba)
{
    vol->drive->Write((uint32_t)lba, 1, buf);
}

static uint32_t FatNextCluster(const FatVolume* vol, uint32_t cluster)
{
    // Each FAT32 entry is 4 bytes
    // Locate which sector of the FAT contains this entry
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = vol->fat_start_lba + (fat_offset / vol->bytes_per_sector);
    uint32_t entry_offset = fat_offset % vol->bytes_per_sector;

    uint8_t sector_buf[FAT32_SECTOR_SIZE];
    ReadSector(vol, sector_buf, fat_sector);

    uint32_t next = *(uint32_t*)(sector_buf + entry_offset);
    return next & 0x0FFFFFFF; // FAT32 entries are 28 bits
}

FatVolume* FatMount(ATADrive* drive, uint64_t partition_lba_start)
{
    if (!drive || !drive->present)
    {
        kprintf("[FAT32] FatMount: drive not present\n");
        SerialWriteString(COM1_PORT, "[FAT32] FatMount: drive not present\n");
        return nullptr;
    }

    uint8_t vbr_buf[FAT32_SECTOR_SIZE];
    // g_read_sector(vbr_buf, partition_lba_start);
    drive->Read((uint32_t)partition_lba_start, 1, vbr_buf);

    if (vbr_buf[510] != 0x55 || vbr_buf[511] != 0xAA)
    {
        kprintf("[FAT32] FatMount: invalid boot sector signature at LBA %llu\n", partition_lba_start);
        SerialWriteString(COM1_PORT, "[FAT32] FatMount: invalid boot sector signature at LBA %llu\n", partition_lba_start);
        return nullptr;
    }

    const FAT32BootSector* bpb = (FAT32BootSector*)vbr_buf;

    if (bpb->bytes_per_sector != FAT32_SECTOR_SIZE)
    {
        kprintf("[FAT32] FatMount: unsupported sector size %u\n", bpb->bytes_per_sector);
        SerialWriteString(COM1_PORT, "[FAT32] FatMount: unsupported sector size %u\n", bpb->bytes_per_sector);
        return nullptr;
    }

    if (bpb->sectors_per_cluster == 0 || (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0)
    {
        kprintf("[FAT32] FatMount: invalid sectors_per_cluster %u\n", bpb->sectors_per_cluster);
        SerialWriteString(COM1_PORT, "[FAT32] FatMount: invalid sectors_per_cluster %u\n", bpb->sectors_per_cluster);
        return nullptr;
    }

    if (bpb->root_entry_count != 0 || bpb->fat_size_16 != 0 || bpb->fat_size_32 == 0)
    {
        kprintf("[FAT32] FatMount: not FAT32 (root_entries=%u fat16=%u fat32=%u)\n",
                bpb->root_entry_count, bpb->fat_size_16, bpb->fat_size_32);
        SerialWriteString(COM1_PORT,
            "[FAT32] FatMount: not FAT32 (root_entries=%u fat16=%u fat32=%u)\n",
            bpb->root_entry_count, bpb->fat_size_16, bpb->fat_size_32);
        return nullptr;
    }

    if (memcmp(bpb->fs_type, "FAT32   ", 8) != 0)
    {
        kprintf("[FAT32] FatMount: fs_type is not FAT32 (got '%.8s')\n", bpb->fs_type);
        SerialWriteString(COM1_PORT,
            "[FAT32] FatMount: fs_type is not FAT32 (got '%.8s')\n", bpb->fs_type);
        return nullptr;
    }

    if (bpb->root_cluster < FAT32_FIRST_CLUSTER)
    {
        kprintf("[FAT32] FatMount: invalid root_cluster %u\n", bpb->root_cluster);
        SerialWriteString(COM1_PORT,
            "[FAT32] FatMount: invalid root_cluster %u\n", bpb->root_cluster);
        return nullptr;
    }

    FatVolume* vol = (FatVolume*)(kzalloc(sizeof(FatVolume)));
    if (!vol)
    {
        kprintf("[FAT32] FatMount: out of memory\n");
        SerialWriteString(COM1_PORT, "[FAT32] FatMount: out of memory\n");
        return nullptr;
    }

    vol->drive = drive;
    vol->partition_lba_start = partition_lba_start;
    vol->bytes_per_sector = bpb->bytes_per_sector;
    vol->fat_count = bpb->fat_count;
    vol->sectors_per_cluster = bpb->sectors_per_cluster;
    vol->fat_size_sectors = bpb->fat_size_32;
    vol->root_cluster = bpb->root_cluster;
    // vol->read_sector = g_read_sector;
    // vol->write_sector = g_write_sector;
    vol->fat_start_lba  = partition_lba_start + bpb->reserved_sector_count;
    vol->data_start_lba = vol->fat_start_lba  + (uint32_t)bpb->fat_count * bpb->fat_size_32;

    kprintf("[FAT32] Mounted: fat_start=%llu data_start=%llu root_cluster=%u spc=%u\n",
            (uint64_t)vol->fat_start_lba,
            (uint64_t)vol->data_start_lba,
            vol->root_cluster,
            vol->sectors_per_cluster);
    SerialWriteString(COM1_PORT, "[FAT32] Mounted: fat_start=%llu data_start=%llu root_cluster=%u spc=%u\n",
            (uint64_t)vol->fat_start_lba,
            (uint64_t)vol->data_start_lba,
            vol->root_cluster,
            vol->sectors_per_cluster);

    return vol;
}

void FatUnmount(FatVolume* vol)
{
    if (!vol) return;
    kfree(vol);
}

// Compare a FAT32 8.3 directory entry name (space-padded, no dot) against a
// plain C-string filename such as "KERNEL  ELF" or the original "kernel.elf".
// Returns true if they match (case-insensitive).
static bool MatchName83(const char dir_name[11], const char* filename)
{
    // Build a normalized 8.3 string from `filename` for comparison
    char norm[11];
    for (int i = 0; i < 11; i++) norm[i] = ' ';

    int dot = -1;
    int len = 0;
    while (filename[len] && len < 256) len++;
    for (int i = 0; i < len; i++)
    {
        if (filename[i] == '.') { dot = i; break; }
    }

    int base_len = (dot >= 0) ? dot : len;
    int ext_len = (dot >= 0) ? (len - dot - 1) : 0;

    if (base_len > 8 || ext_len > 3) return false;

    for (int i = 0; i < base_len; i++)
    {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        norm[i] = c;
    }
    for (int i = 0; i < ext_len; i++)
    {
        char c = filename[dot + 1 + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        norm[8+i] = c;
    }

    for (int i = 0; i < 11; i++)
    {
        char d = dir_name[i];
        if (d >= 'a' && d <= 'z') d -= 32;
        if (norm[i] != d) return false;
    }

    return true;
}

// Turn a raw 11-byte FAT directory name into "NAME.EXT" for display.
static void Name83ToString(const char name83[11], char* out, size_t out_cap)
{
    size_t ni = 0;
    for (int i = 0; i < 11 && ni + 2 < out_cap; i++)
    {
        if (name83[i] != ' ')
            out[ni++] = name83[i];
        if (i == 7 && name83[8] != ' ')
            out[ni++] = '.';
    }
    out[ni] = '\0';
}

// Search the directory starting at `dir_cluster` for an entry matching
// `name`. Fills `out` on success and returns true.
static bool FindInDirectory(const FatVolume* vol, uint32_t dir_cluster, const char* name, Fat32DirEntry* out)
{
    uint8_t sector_buf[FAT32_SECTOR_SIZE];
    uint32_t cluster = dir_cluster;

    while (cluster >= FAT32_FIRST_CLUSTER && cluster < FAT32_EOC_MIN)
    {
        uint64_t lba = ClusterToLba(vol, cluster);

        for (uint32_t s = 0; s < vol->sectors_per_cluster; s++)
        {
            ReadSector(vol, sector_buf, lba + s);

            const Fat32DirEntry* entries = (Fat32DirEntry*)sector_buf;
            const int per_sector = FAT32_SECTOR_SIZE / sizeof(Fat32DirEntry);

            for (int e = 0; e < per_sector; e++)
            {
                const Fat32DirEntry& entry = entries[e];

                // 0x00 - no more entries in this directory
                if ((uint8_t)entry.name[0] == 0x00) return false;
                // 0xE5 - deleted entry, skip
                if ((uint8_t)entry.name[0] == 0xE5) continue;
                // Skip LFN entries
                if (entry.attributes == FA_LFN) continue;

                if (MatchName83(entry.name, name))
                {
                    *out = entry;
                    return true;
                }
            }
        }

        cluster = FatNextCluster(vol, cluster);
    }

    return false;
}

FatFile* FatOpen(FatVolume* vol, const char* path)
{
    if (!vol || !path || path[0] != '/') return nullptr;

    uint32_t current_cluster = vol->root_cluster;
    Fat32DirEntry found_entry = {};
    bool found_any = false;

    static constexpr size_t MAX_PATH = 512;
    char path_copy[MAX_PATH];

    size_t path_len = 0;
    while (path[path_len] && path_len < MAX_PATH - 1) path_len++;
    for (size_t i = 0; i <= path_len; i++) path_copy[i] = path[i];

    if (path_copy[0] == '/' && path_copy[1] == '\0')
    {
        FatFile* f = (FatFile*)kzalloc(sizeof(FatFile));
        if (!f) return nullptr;
        f->attributes = FA_DIRECTORY;
        f->first_cluster = vol->root_cluster;
        f->current_cluster = vol->root_cluster;
        f->is_dir = true;
        f->vol = vol;
        f->name[0] = '/';
        f->name[1] = '\0';
        return f;
    }

    // Tokenise by '/'
    char* token = path_copy + 1;
    while (token && *token)
    {
        char* slash = token;
        while (*slash && *slash != '/') slash++;
        bool last_component = (*slash == '\0');
        *slash = '\0';

        Fat32DirEntry entry;
        if (!FindInDirectory(vol, current_cluster, token, &entry))
        {
            // kprintf("[FAT32] FatOpen: '%s' not found\n", token);
            // SerialWriteString(COM1_PORT, "[FAT32] FatOpen: '%s' not found\n", token);
            return nullptr;
        }

        found_entry = entry;
        found_any = true;

        current_cluster = ((uint32_t)entry.cluster_high << 16) | entry.cluster_low;

        if (!last_component)
        {
            if (!(entry.attributes & FA_DIRECTORY))
            {
                kprintf("[FAT32] FatOpen: '%s' is not a directory\n", token);
                SerialWriteString(COM1_PORT, "[FAT32] FatOpen: '%s' is not a directory\n", token);
                return nullptr;
            }
            token = slash + 1;
        }
        else
        {
            break;
        }
    }

    if (!found_any) return nullptr;

    FatFile* f = (FatFile*)kzalloc(sizeof(FatFile));
    if (!f) return nullptr;

    // Copy short name into handle
    int ni = 0;
    for (int i = 0; i < 11 && ni < 255; i++)
    {
        if (found_entry.name[i] != ' ')
        {
            f->name[ni++] = found_entry.name[i];
        }
        if (i == 7 && found_entry.name[8] != ' ')
        {
            f->name[ni++] = '.';
        }
    }

    f->name[ni] = '\0';

    f->attributes      = found_entry.attributes;
    f->file_size       = found_entry.file_size;
    f->first_cluster   = ((uint32_t)found_entry.cluster_high << 16) | found_entry.cluster_low;
    f->current_cluster = f->first_cluster;
    f->cluster_offset  = 0;
    f->pos             = 0;
    f->is_dir          = (found_entry.attributes & FA_DIRECTORY) != 0;
    f->vol             = vol;

    return f;
}

void FatClose(FatFile* f)
{
    kfree(f);
}

int FatReadDir(FatVolume* vol, const char* path, FatDirInfo* out, int max)
{
    if (!vol || !out || max <= 0) return -1;

    const char* dir_path = (path && path[0]) ? path : "/";
    FatFile* dir = FatOpen(vol, dir_path);
    if (!dir) return -1;

    if (!dir->is_dir)
    {
        FatClose(dir);
        return -1;
    }

    uint8_t sector_buf[FAT32_SECTOR_SIZE];
    uint32_t cluster = dir->first_cluster;
    int count = 0;

    while (cluster >= FAT32_FIRST_CLUSTER && cluster < FAT32_EOC_MIN && count < max)
    {
        uint64_t lba = ClusterToLba(vol, cluster);

        for (uint32_t s = 0; s < vol->sectors_per_cluster && count < max; s++)
        {
            ReadSector(vol, sector_buf, lba + s);

            const Fat32DirEntry* entries = (Fat32DirEntry*)sector_buf;
            const int per_sector = FAT32_SECTOR_SIZE / sizeof(Fat32DirEntry);

            for (int e = 0; e < per_sector && count < max; e++)
            {
                const Fat32DirEntry& entry = entries[e];
                uint8_t first = (uint8_t)entry.name[0];

                if (first == 0x00) { FatClose(dir); return count; } // end of dir
                if (first == 0xE5) continue;
                if (entry.attributes == FA_LFN) continue;
                if (entry.attributes & FA_VOLUME_LABEL) continue;
                if (first == '.') continue; // "." and ".."

                FatDirInfo& info = out[count];
                Name83ToString(entry.name, info.name, sizeof(info.name));
                info.is_dir = (entry.attributes & FA_DIRECTORY) ? 1 : 0;
                info.size = info.is_dir ? 0 : entry.file_size;
                count++;
            }
        }

        cluster = FatNextCluster(vol, cluster);
    }

    FatClose(dir);
    return count;
}

void FatListDir(FatVolume* vol, const char* path)
{
    if (!vol)
    {
        // kprintf("[FAT32] FatListDir: no volume\n");
        return;
    }

    const char* dir_path = (path && path[0]) ? path : "/";
    FatFile* dir = FatOpen(vol, dir_path);
    if (!dir)
    {
        // kprintf("[FAT32] FatListDir: cannot open '%s'\n", dir_path);
        // SerialWriteString(COM1_PORT, "[FAT32] FatListDir: cannot open '%s'\n", dir_path);
        return;
    }

    if (!dir->is_dir)
    {
        // kprintf("[FAT32] FatListDir: '%s' is not a directory\n", dir_path);
        FatClose(dir);
        return;
    }

    // kprintf("[FAT32] Directory %s:\n", dir_path);
    // SerialWriteString(COM1_PORT, "[FAT32] Directory %s:\n", dir_path);

    uint8_t sector_buf[FAT32_SECTOR_SIZE];
    uint32_t cluster = dir->first_cluster;
    uint32_t count = 0;

    while (cluster >= FAT32_FIRST_CLUSTER && cluster < FAT32_EOC_MIN)
    {
        uint64_t lba = ClusterToLba(vol, cluster);

        for (uint32_t s = 0; s < vol->sectors_per_cluster; s++)
        {
            ReadSector(vol, sector_buf, lba + s);

            const Fat32DirEntry* entries = (Fat32DirEntry*)sector_buf;
            const int per_sector = FAT32_SECTOR_SIZE / sizeof(Fat32DirEntry);

            for (int e = 0; e < per_sector; e++)
            {
                const Fat32DirEntry& entry = entries[e];
                uint8_t first = (uint8_t)entry.name[0];

                if (first == 0x00) goto done;
                if (first == 0xE5) continue;
                if (entry.attributes == FA_LFN) continue;
                if (entry.attributes & FA_VOLUME_LABEL) continue;
                if (first == '.') continue; // "." and ".."

                char display[256];
                Name83ToString(entry.name, display, sizeof(display));

                if (entry.attributes & FA_DIRECTORY)
                {
                    kprintf("  <DIR>  %s\n", display);
                    SerialWriteString(COM1_PORT, "  <DIR>  %s\n", display);
                }
                else
                {
                    kprintf("         %s  (%u bytes)\n", display, entry.file_size);
                    SerialWriteString(COM1_PORT, "         %s  (%u bytes)\n",
                                       display, entry.file_size);
                }

                count++;
            }
        }

        cluster = FatNextCluster(vol, cluster);
    }

done:
    FatClose(dir);
    kprintf("[FAT32] %u entries\n", count);
    SerialWriteString(COM1_PORT, "[FAT32] %u entries\n", count);
}

size_t FatRead(FatFile* f, void* buf, size_t len)
{
    if (!f || !buf || len == 0 || f->is_dir) return 0;
    if (f->pos >= f->file_size) return 0;

    if (f->pos + len > f->file_size) len = f->file_size - f->pos;

    const FatVolume* vol = f->vol;
    const uint32_t cluster_bytes = vol->sectors_per_cluster * FAT32_SECTOR_SIZE;

    uint8_t sector_buf[FAT32_SECTOR_SIZE];
    uint8_t* out = (uint8_t*)buf;
    size_t remaining = len;

    while (remaining > 0 && f->current_cluster >= FAT32_FIRST_CLUSTER && f->current_cluster < FAT32_EOC_MIN)
    {
        // Which sector within the current cluster are we in?
        uint32_t sector_in_cluster = f->cluster_offset / FAT32_SECTOR_SIZE;
        uint32_t offset_in_sector = f->cluster_offset % FAT32_SECTOR_SIZE;

        if (sector_in_cluster >= vol->sectors_per_cluster)
        {
            // Advance to next cluster
            uint32_t next = FatNextCluster(vol, f->current_cluster);
            if (next < FAT32_FIRST_CLUSTER || next >= FAT32_EOC_MIN) break;

            f->current_cluster = next;
            f->cluster_offset = 0;
            sector_in_cluster = 0;
            offset_in_sector = 0;
        }

        uint64_t lba = ClusterToLba(vol, f->current_cluster) + sector_in_cluster;
        ReadSector(vol, sector_buf, lba);

        size_t can_copy = FAT32_SECTOR_SIZE - offset_in_sector;
        if (can_copy > remaining) can_copy = remaining;

        for (size_t i = 0; i < can_copy; i++)
        {
            out[i] = sector_buf[offset_in_sector + i];
        }

        out += can_copy;
        remaining -= can_copy;
        f->cluster_offset += (uint32_t)can_copy;
        f->pos += can_copy;

        // If consumed this whole sector, bump to the next one.
        // The cluster boundary check at the top of the loop handles
        // the cluster transition automatically.
    }

    return len - remaining;
}

// Write a 32-bit value into FAT copy 0 at the entry for `cluster`.
// We only update FAT copy 0 for now — mirroring to copy 1 is a future
// improvement once the driver is stable.
static void FatWriteEntry(FatVolume* vol, uint32_t cluster, uint32_t value)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = vol->fat_start_lba + (fat_offset / 512);
    uint32_t byte_off = fat_offset % 512;

    uint8_t buf[512];
    // vol->read_sector(buf, fat_sector);
    ReadSector(vol, buf, fat_sector);

    // FAT32 entries are 28 bits — preserve the top 4 bits
    uint32_t existing = *(uint32_t*)(buf + byte_off);
    uint32_t updated = (existing & 0xF0000000) | (value & 0x0FFFFFFF);
    *(uint32_t*)(buf + byte_off) = updated;

    // vol->write_sector(buf, fat_sector);
    WriteSector(vol, buf, fat_sector);

    // Mirror to additional FAT copies when present
    for (uint8_t copy = 1; copy < vol->fat_count; copy++)
    {
        // vol->write_sector(buf, fat_sector + (uint32_t)copy * vol->fat_size_sectors);
        WriteSector(vol, buf, fat_sector + (uint32_t)copy * vol->fat_size_sectors);
    }
}

uint32_t FatAllocCluster(FatVolume* vol)
{
    // Scan FAT copy 0 for a free entry (value == 0).
    // Start from cluster 2 — clusters 0 and 1 are reserved.
    uint8_t buf[512];
    uint32_t last_fat_sector = 0xFFFFFFFF;

    uint32_t total_clusters = (vol->fat_size_sectors * 512) / 4;

    for (uint32_t cluster = 2; cluster < total_clusters; cluster++)
    {
        uint32_t fat_offset  = cluster * 4;
        uint32_t fat_sector  = vol->fat_start_lba + (fat_offset / 512);
        uint32_t byte_off    = fat_offset % 512;

        // only re-read when we cross into a new FAT sector
        if (fat_sector != last_fat_sector)
        {
            // vol->read_sector(buf, fat_sector);
            ReadSector(vol, buf, fat_sector);
            last_fat_sector = fat_sector;
        }

        uint32_t entry = *(uint32_t*)(buf + byte_off);
        entry &= 0x0FFFFFFF;

        if (entry == 0x00000000)
        {
            // Mark as end-of-chain
            FatWriteEntry(vol, cluster, 0x0FFFFFFF);
            return cluster;
        }
    }

    // kprintf("[FAT32] FatAllocCluster: disk full\n");
    // SerialWriteString(COM1_PORT, "[FAT32] FatAllocCluster: disk full\n");
    return 0;
}

void FatChainLink(FatVolume* vol, uint32_t cluster, uint32_t next)
{
    FatWriteEntry(vol, cluster, next);
}

// Find the directory cluster of the parent path.
// e.g. for "/foo/bar.txt" returns the cluster of "/foo".
// For a top-level file like "/bar.txt" returns vol->root_cluster.
static uint32_t FindParentCluster(FatVolume* vol, const char* path)
{
    // Find last slash
    int last_slash = 0;
    for (int i = 0; path[i]; i++)
    {
        if (path[i] == '/') last_slash = i;
    }

    // File is in root
    if (last_slash == 0) return vol->root_cluster;

    // Otherwise open the parent directory and return its cluster
    char parent[512];
    for (int i = 0; i < last_slash; i++) parent[i] = path[i];
    parent[last_slash] = '\0';

    FatFile* dir = FatOpen(vol, parent);
    if (!dir || !dir->is_dir)
    {
        if (dir) FatClose(dir);
        return 0;
    }

    uint32_t cluster = dir->first_cluster;
    FatClose(dir);
    return cluster;
}

// Write an 8.3 directory entry into a free slot in the directory
// at `dir_cluster`, allocating a new cluster for the directory if needed.
static bool WriteNewDirEntry(FatVolume* vol, uint32_t dir_cluster,
                              const char name83[11], uint32_t file_cluster)
{
    uint8_t sector_buf[512];
    uint32_t cluster = dir_cluster;

    while (cluster < 0x0FFFFFF8)
    {
        uint64_t lba = ClusterToLba(vol, cluster);

        for (uint32_t s = 0; s < vol->sectors_per_cluster; s++)
        {
            // vol->read_sector(sector_buf, lba + s);
            ReadSector(vol, sector_buf, lba + s);

            int per_sector = 512 / 32;
            for (int e = 0; e < per_sector; e++)
            {
                uint8_t first = sector_buf[e * 32];

                // 0x00 = never used, 0xE5 = deleted — both are free slots
                if (first == 0x00 || first == 0xE5)
                {
                    uint8_t* entry = sector_buf + e * 32;

                    // Zero the whole entry first
                    for (int i = 0; i < 32; i++) entry[i] = 0;

                    // Name (11 bytes)
                    for (int i = 0; i < 11; i++)
                    {
                        entry[i] = (uint8_t)name83[i];
                    }

                    // Attribute: ARCHIVE
                    entry[11] = 0x20;

                    // First cluster high/low
                    entry[20] = (uint8_t)((file_cluster >> 16) & 0xFF);
                    entry[21] = (uint8_t)((file_cluster >> 24) & 0xFF);
                    entry[26] = (uint8_t)( file_cluster        & 0xFF);
                    entry[27] = (uint8_t)((file_cluster >>  8) & 0xFF);

                    // File size = 0 initially; updated by FatWrite via FatFlushSize
                    entry[28] = 0; entry[29] = 0; entry[30] = 0; entry[31] = 0;

                    // If this was a 0x00 slot, write a new terminator after it
                    if (first == 0x00 && e + 1 < per_sector)
                    {
                        sector_buf[(e + 1) * 32] = 0x00;
                    }

                    // vol->write_sector(sector_buf, lba + s);
                    WriteSector(vol, sector_buf, lba + s);
                    return true;
                }
            }
        }

        uint32_t next = FatNextCluster(vol, cluster);
        if (next >= 0x0FFFFFF8)
        {
            // Directory is full - allocate a new cluster for it
            uint32_t new_cluster = FatAllocCluster(vol);
            if (!new_cluster) return false;

            // Zero the new cluster
            uint8_t zero[512];
            for (int i = 0; i < 512; i++) zero[i] = 0;
            uint64_t new_lba = ClusterToLba(vol, new_cluster);
            for (uint32_t s = 0; s < vol->sectors_per_cluster; s++)
            {
                // vol->write_sector(zero, new_lba + s);
                WriteSector(vol, zero, new_lba + s);
            }

            FatChainLink(vol, cluster, new_cluster);
            cluster = new_cluster;
        }
        else
        {
            cluster = next;
        }
    }

    return false;
}

// Convert a filename string to a space-padded 8.3 name.
// Returns false if the name can't be represented in 8.3 format.
static bool FilenameToName83(const char* filename, char out[11])
{
    for (int i = 0; i < 11; i++) out[i] = ' ';

    int dot = -1;
    int len = 0;
    while (filename[len]) len++;
    for (int i = 0; i < len; i++)
        if (filename[i] == '.') { dot = i; break; }

    int base_len = (dot >= 0) ? dot : len;
    int ext_len  = (dot >= 0) ? (len - dot - 1) : 0;

    if (base_len == 0 || base_len > 8) return false;
    if (ext_len > 3)                   return false;

    for (int i = 0; i < base_len; i++)
    {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[i] = c;
    }
    for (int i = 0; i < ext_len; i++)
    {
        char c = filename[dot + 1 + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[8 + i] = c;
    }

    return true;
}

FatFile* FatCreate(FatVolume* vol, const char* path)
{
    if (!vol || !path || path[0] != '/') return nullptr;

    // Check the file doesn't already exist
    FatFile* existing = FatOpen(vol, path);
    if (existing) { FatClose(existing); return nullptr; }

    // Extract the filename component (everything after the last '/')
    int last_slash = 0;
    for (int i = 0; path[i]; i++)
    {
        if (path[i] == '/') last_slash = i;
    }

    const char* filename = path + last_slash + 1;
    if (!filename[0]) return nullptr;

    // Convert to 8.3
    char name83[11];
    if (!FilenameToName83(filename, name83))
    {
        kprintf("[FAT32] FatCreate: '%s' can't be represented in 8.3\n", filename);
        SerialWriteString(COM1_PORT, "[FAT32] FatCreate: '%s' can't be represented in 8.3\n", filename);
        return nullptr;
    }

    // Find the parent directory cluster
    uint32_t parent_cluster = FindParentCluster(vol, path);
    if (!parent_cluster) return nullptr;

    // Allocate the file's first cluster
    uint32_t file_cluster = FatAllocCluster(vol);
    if (!file_cluster) return nullptr;

    // Zero the cluster so there's no stale data
    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    uint64_t lba = ClusterToLba(vol, file_cluster);
    for (uint32_t s = 0; s < vol->sectors_per_cluster; s++)
    {
        // vol->write_sector(zero, lba + s);
        WriteSector(vol, zero, lba + s);
    }

    // Write the directory entry
    if (!WriteNewDirEntry(vol, parent_cluster, name83, file_cluster))
    {
        kprintf("[FAT32] FatCreate: failed to write directory entry\n");
        SerialWriteString(COM1_PORT, "[FAT32] FatCreate: failed to write directory entry\n");
        // Free the cluster we allocated
        FatWriteEntry(vol, file_cluster, 0x00000000);
        return nullptr;
    }

    // Build and return an open file handle
    FatFile* f = static_cast<FatFile*>(kzalloc(sizeof(FatFile)));
    if (!f) return nullptr;

    int ni = 0;
    for (int i = 0; filename[i] && ni < 255; i++)
    {
        f->name[ni++] = filename[i];
    }
    f->name[ni] = '\0';

    f->attributes      = 0x20; // ARCHIVE
    f->file_size       = 0;
    f->first_cluster   = file_cluster;
    f->current_cluster = file_cluster;
    f->cluster_offset  = 0;
    f->pos             = 0;
    f->is_dir          = false;
    f->vol             = vol;

    return f;
}

bool FatFlushSize(FatVolume* vol, const char* path, uint32_t new_size)
{
    // Find the last path component
    int last_slash = 0;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;
    const char* filename = path + last_slash + 1;

    uint32_t dir_cluster = FindParentCluster(vol, path);
    if (!dir_cluster) return false;

    uint8_t sector_buf[512];
    uint32_t cluster = dir_cluster;

    while (cluster < 0x0FFFFFF8)
    {
        uint64_t lba = ClusterToLba(vol, cluster);
        for (uint32_t s = 0; s < vol->sectors_per_cluster; s++)
        {
            // vol->read_sector(sector_buf, lba + s);
            ReadSector(vol, sector_buf, lba + s);

            int per_sector = 512 / 32;
            for (int e = 0; e < per_sector; e++)
            {
                uint8_t first = sector_buf[e * 32];
                if (first == 0x00) return false; // end of entries
                if (first == 0xE5) continue;     // deleted

                Fat32DirEntry* entry = (Fat32DirEntry*)(sector_buf + e * 32);
                if (entry->attributes == 0x0F) continue; // LFN

                if (MatchName83(entry->name, filename))
                {
                    // Update size field at offset 28 within the entry
                    uint8_t* raw = sector_buf + e * 32;
                    raw[28] = (uint8_t)( new_size        & 0xFF);
                    raw[29] = (uint8_t)((new_size >>  8) & 0xFF);
                    raw[30] = (uint8_t)((new_size >> 16) & 0xFF);
                    raw[31] = (uint8_t)((new_size >> 24) & 0xFF);
                    // vol->write_sector(sector_buf, lba + s);
                    WriteSector(vol, sector_buf, lba + s);
                    return true;
                }
            }
        }

        cluster = FatNextCluster(vol, cluster);
    }

    return false;
}

// Write (basic — does not allocate new clusters or update directory entries)
size_t FatWrite(FatFile* f, const void* buf, size_t len)
{
    if (!f || !buf || len == 0 || f->is_dir) return 0;
    // if (!f->vol->write_sector)
    // {
    //     kprintf("[FAT32] FatWrite: no write_sector callback\n");
    //     SerialWriteString(COM1_PORT, "[FAT32] FatWrite: no write_sector callback\n");
    //     return 0;
    // }

    const FatVolume* vol = f->vol;
    const uint32_t cluster_bytes = vol->sectors_per_cluster * FAT32_SECTOR_SIZE;
    const uint8_t* in = (const uint8_t*)buf;
    size_t remaining = len;
    uint8_t sector_buf[FAT32_SECTOR_SIZE];

    while (remaining > 0 && f->current_cluster >= FAT32_FIRST_CLUSTER && f->current_cluster < FAT32_EOC_MIN)
    {
        uint32_t sector_in_cluster = f->cluster_offset / FAT32_SECTOR_SIZE;
        uint32_t offset_in_sector = f->cluster_offset % FAT32_SECTOR_SIZE;

        if (sector_in_cluster >= vol->sectors_per_cluster)
        {
            uint32_t next = FatNextCluster(vol, f->current_cluster);
            if (next < FAT32_FIRST_CLUSTER || next >= FAT32_EOC_MIN) break; // would need cluster allocation - not implemented yet

            f->current_cluster = next;
            f->cluster_offset = 0;
            sector_in_cluster = 0;
            offset_in_sector = 0;
        }

        uint64_t lba = ClusterToLba(vol, f->current_cluster) + sector_in_cluster;

        // Read-modify-write so it doesn't clobber unrelated bytes
        ReadSector(vol, sector_buf, lba);

        size_t can_write = FAT32_SECTOR_SIZE - offset_in_sector;
        if (can_write > remaining) can_write = remaining;

        for (size_t i = 0; i < can_write; i++)
        {
            sector_buf[offset_in_sector + i] = in[i];
        }

        // vol->write_sector(sector_buf, lba);
        WriteSector(vol, sector_buf, lba);

        in += can_write;
        remaining -= can_write;
        f->cluster_offset += (uint32_t)can_write;
        f->pos += can_write;

        if (f->pos > f->file_size) f->file_size = (uint32_t)f->pos;
    }

    return len - remaining;
}
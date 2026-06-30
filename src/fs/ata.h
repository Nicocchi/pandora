#pragma once

#include <stdint.h>
#include <stddef.h>

// Standard IDE primary channel port base addresses
static constexpr uint16_t ATA_PRIMARY_BASE        = 0x1F0;
static constexpr uint16_t ATA_PRIMARY_CTRL         = 0x3F6;

// Register offsets from base port
static constexpr uint16_t ATA_REG_DATA             = 0x00;
static constexpr uint16_t ATA_REG_ERROR            = 0x01;
static constexpr uint16_t ATA_REG_FEATURES         = 0x01;
static constexpr uint16_t ATA_REG_SECTOR_COUNT     = 0x02;
static constexpr uint16_t ATA_REG_LBA_LOW          = 0x03;
static constexpr uint16_t ATA_REG_LBA_MID          = 0x04;
static constexpr uint16_t ATA_REG_LBA_HIGH         = 0x05;
static constexpr uint16_t ATA_REG_DRIVE_SELECT     = 0x06;
static constexpr uint16_t ATA_REG_STATUS           = 0x07;
static constexpr uint16_t ATA_REG_COMMAND          = 0x07;

// Status register bits
static constexpr uint8_t ATA_STATUS_ERR            = (1 << 0); // Error
static constexpr uint8_t ATA_STATUS_DRQ            = (1 << 3); // Data request ready
static constexpr uint8_t ATA_STATUS_SRV            = (1 << 4); // Overlapped mode service request
static constexpr uint8_t ATA_STATUS_DF             = (1 << 5); // Drive fault
static constexpr uint8_t ATA_STATUS_RDY            = (1 << 6); // Drive ready
static constexpr uint8_t ATA_STATUS_BSY            = (1 << 7); // Drive busy

// Commands
static constexpr uint8_t ATA_CMD_READ_SECTORS      = 0x20; // LBA28 read
static constexpr uint8_t ATA_CMD_WRITE_SECTORS     = 0x30; // LBA28 write
static constexpr uint8_t ATA_CMD_CACHE_FLUSH       = 0xE7; // Flush write cache
static constexpr uint8_t ATA_CMD_IDENTIFY          = 0xEC; // Identify drive

// Drive select: master on primary = 0xE0, slave = 0xF0
// Bits 6 and 7 must always be set (legacy requirement)
// Bits 24-27 of LBA go into bits 0-3 for LBA28
static constexpr uint8_t ATA_DRIVE_MASTER          = 0xE0;
static constexpr uint8_t ATA_DRIVE_SLAVE           = 0xF0;

// Maximum LBA28 addressable sector (2^28 - 1 = 128 GiB)
static constexpr uint32_t ATA_LBA28_MAX            = 0x0FFFFFFF;

// Result codes
enum class ATAResult : uint8_t
{
    OK = 0,
    Timeout,        // BSY never cleared
    DriveError,     // ERR or DF set in status
    NoDRQ,          // DRQ never asserted after command
    InvalidLBA,     // LBA exceeds LBA28 range
    NotPresent,     // IDENTIFY returned no device
};

struct ATADrive
{
    int index;
    uint16_t base;
    uint16_t ctrl;
    uint8_t drive_select;
    bool present;
    uint32_t sector_count;
    char model[41];

    ATAResult Init();
    ATAResult Read(uint32_t lba, uint8_t count, uint8_t* buf);
    ATAResult Write(uint32_t lba, uint8_t count, const uint8_t* buf);
};

extern ATADrive g_ata_primary;
static constexpr int ATA_MAX_DRIVES = 4;
extern ATADrive g_ata_drives[ATA_MAX_DRIVES];
extern int g_ata_drive_count;

ATAResult ATAInit();

void ATAReadSector (uint8_t* buf, uint64_t lba);
void ATAWriteSector(const uint8_t* buf, uint64_t lba);
void ATAReadSectorDrive(int drive_idx, uint8_t* buf, uint64_t lba);

struct MBRPartitionEntry
{
    uint8_t status;         // 0x80 = bootable
    uint8_t chs_first[3];
    uint8_t type;           // partition type code
    uint8_t chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

// 0x0B / 0x0C = FAT32
// 0x83 = Linux ext2/3/4
// 0x07 = NTFS/exFAT
//0x00 = Empty slot

struct MBR
{
    uint8_t bootstrap[446];
    MBRPartitionEntry partitions[4];
    uint16_t signature; // should be 0xAA55
} __attribute__((packed));

enum class FsType { Unknown, FAT32, Ext2, NTFS };

FsType DetectFilesystem(ATADrive* drive, uint32_t partition_lba);
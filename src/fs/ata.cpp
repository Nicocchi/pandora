#include "ata.h"
#include "common.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "drivers/serial_port.h"

ATADrive g_ata_primary;
ATADrive g_ata_drives[ATA_MAX_DRIVES];
int g_ata_drive_count = 0;

struct ATADriveConfig
{
    uint16_t base;
    uint16_t ctrl;
    uint8_t drive_select;
    const char* label;
};

static constexpr ATADriveConfig k_ata_configs[ATA_MAX_DRIVES] =
{
    { 0x1F0, 0x3F6, ATA_DRIVE_MASTER, "Primary Master"   },
    { 0x1F0, 0x3F6, ATA_DRIVE_SLAVE,  "Primary Slave"    },
    { 0x170, 0x376, ATA_DRIVE_MASTER, "Secondary Master" },
    { 0x170, 0x376, ATA_DRIVE_SLAVE,  "Secondary Slave"  },
};

static inline uint8_t ATAInB(uint16_t port)
{
    return InPortB(port);
}

static inline void ATAOutB(uint16_t port, uint8_t val)
{
    OutPortB(port, val);
}

static inline uint16_t ATAInW(uint16_t port)
{
    uint16_t val;
    asm volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void ATAOutW(uint16_t port, uint16_t val)
{
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static void ATADelay400ns(const ATADrive& drive)
{
    ATAInB(drive.ctrl);
    ATAInB(drive.ctrl);
    ATAInB(drive.ctrl);
    ATAInB(drive.ctrl);
}

static ATAResult ATAWaitReady(const ATADrive& drive, uint8_t wait_for_bits = 0)
{
    uint32_t timeout = 0x1000000;

    while (timeout--)
    {
        uint8_t status = ATAInB(drive.base + ATA_REG_STATUS);

        if (status & ATA_STATUS_ERR) return ATAResult::DriveError;
        if (status & ATA_STATUS_DF) return ATAResult::DriveError;
        if (status & ATA_STATUS_BSY) continue;

        if (wait_for_bits && !(status & wait_for_bits)) continue;

        return ATAResult::OK;
    }

    return ATAResult::Timeout;
}

static ATAResult ATASelectDrive(const ATADrive& drive)
{
    ATAOutB(drive.base + ATA_REG_DRIVE_SELECT, drive.drive_select);
    ATADelay400ns(drive);
    return ATAWaitReady(drive);
}

ATAResult ATADrive::Init()
{
    present = false;
    sector_count = 0;
    model[0] = '\0';

    ATAOutB(ctrl, 0x04); // SRST bit
    ATADelay400ns(*this);
    ATAOutB(ctrl, 0x00); // Clear SRST
    ATADelay400ns(*this);

    // Select the drive
    ATAResult res = ATASelectDrive(*this);
    if (res != ATAResult::OK) return res;

    ATAOutB(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ATADelay400ns(*this);

    uint8_t status = ATAInB(base + ATA_REG_STATUS);
    if (status == 0x00) return ATAResult::NotPresent;

    res = ATAWaitReady(*this);
    if (res != ATAResult::OK) return res;

    uint8_t lba_mid = ATAInB(base + ATA_REG_LBA_MID);
    uint8_t lba_high = ATAInB(base + ATA_REG_LBA_HIGH);
    if (lba_mid != 0 || lba_high != 0)
    {
        // ATAPI or SATA device responding with a signature - not plain ATA
        SerialWriteString(COM1_PORT,
            "[ATA] IDENTIFY: non-ATA signature (mid=%02x high=%02x), skipping\n",
            lba_mid, lba_high);
        return ATAResult::NotPresent;
    }

    res = ATAWaitReady(*this, ATA_STATUS_DRQ);
    if (res != ATAResult::OK) return res;

    uint16_t identify[256];
    for (int i = 0; i < 256; i++)
    {
        identify[i] = ATAInW(base + ATA_REG_DATA);

    }

    sector_count = ((uint32_t)identify[61] << 16 | identify[60]);

    for (int i = 0; i < 20; i++)
    {
        uint16_t word = identify[27 + i];
        model[i * 2]     = (char)(word >> 8);
model[i * 2 + 1] = (char)(word & 0xFF);
    }
    model[40] = '\0';

    for (int i = 39; i >= 0 && model[i] == ' '; i--)
    {
        model[i] = '\0';
    }

    present = true;
    return ATAResult::OK;
}

ATAResult ATADrive::Read(uint32_t lba, uint8_t count, uint8_t* buf)
{
    if (!present)                    return ATAResult::NotPresent;
    if (lba > ATA_LBA28_MAX)         return ATAResult::InvalidLBA;
    if (count == 0)                  return ATAResult::OK;

    ATAResult res = ATASelectDrive(*this);
    if (res != ATAResult::OK) return res;

    ATAOutB(base + ATA_REG_DRIVE_SELECT, drive_select | (uint8_t)((lba >> 24) & 0x0F));

    ATAOutB(base + ATA_REG_FEATURES,      0x00);
    ATAOutB(base + ATA_REG_SECTOR_COUNT,  count);
    ATAOutB(base + ATA_REG_LBA_LOW,  (uint8_t)( lba        & 0xFF));
    ATAOutB(base + ATA_REG_LBA_MID,  (uint8_t)((lba >>  8) & 0xFF));
    ATAOutB(base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    ATAOutB(base + ATA_REG_COMMAND,        ATA_CMD_READ_SECTORS);

    for (uint8_t s = 0; s < count; s++)
    {
        ATADelay400ns(*this);

        res = ATAWaitReady(*this, ATA_STATUS_DRQ);
        if (res != ATAResult::OK) return res;

        uint16_t* dst = (uint16_t*)(buf + (uint32_t)s * 512);
        for (int i = 0; i < 256; i++)
        {
            dst[i] = ATAInW(base + ATA_REG_DATA);
        }
    }

    return ATAResult::OK;
}

ATAResult ATADrive::Write(uint32_t lba, uint8_t count, const uint8_t* buf)
{
    if (!present)            return ATAResult::NotPresent;
    if (lba > ATA_LBA28_MAX) return ATAResult::InvalidLBA;
    if (count == 0)          return ATAResult::OK;

    ATAResult res = ATASelectDrive(*this);
    if (res != ATAResult::OK) return res;

    ATAOutB(base + ATA_REG_DRIVE_SELECT, drive_select | (uint8_t)((lba >> 24) & 0x0F));

    ATAOutB(base + ATA_REG_FEATURES,      0x00);
    ATAOutB(base + ATA_REG_SECTOR_COUNT,  count);
    ATAOutB(base + ATA_REG_LBA_LOW,  (uint8_t)( lba        & 0xFF));
    ATAOutB(base + ATA_REG_LBA_MID,  (uint8_t)((lba >>  8) & 0xFF));
    ATAOutB(base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    ATAOutB(base + ATA_REG_COMMAND,        ATA_CMD_WRITE_SECTORS);

    for (uint8_t s = 0; s < count; s++)
    {
        ATADelay400ns(*this);

        res = ATAWaitReady(*this, ATA_STATUS_DRQ);
        if (res != ATAResult::OK) return res;

        const uint16_t* src = (const uint16_t*)(buf + (uint32_t)s * 512);
        for (int i = 0; i < 256; i++)
        {
            ATAOutW(base + ATA_REG_DATA, src[i]);
        }

        ATAOutB(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ATAWaitReady(*this);
    }

    return ATAResult::OK;
}

ATAResult ATAInit()
{
    g_ata_drive_count = 0;

    for (int i = 0; i < ATA_MAX_DRIVES; i++)
    {
        ATADrive& d = g_ata_drives[i];
        d.base = k_ata_configs[i].base;
        d.ctrl = k_ata_configs[i].ctrl;
        d.drive_select = k_ata_configs[i].drive_select;

        ATAResult res = d.Init();
        if (res == ATAResult::OK)
        {
            d.index = i;
            g_ata_drive_count++;
            kprintf("[ATA] %s: %s (%lu sectors)\n", k_ata_configs[i].label, d.model,
                    (unsigned long)d.sector_count);
        }
        else
        {
            kprintf("[ATA] %s: not present (result=%d)\n", k_ata_configs[i].label, (int)res);
        }
    }

    return (g_ata_drive_count > 0) ? ATAResult::OK : ATAResult::NotPresent;

    // g_ata_primary.base = ATA_PRIMARY_BASE;
    // g_ata_primary.ctrl = ATA_PRIMARY_CTRL;
    // g_ata_primary.drive_select = ATA_DRIVE_MASTER;

    // ATAResult res = g_ata_primary.Init();

    // if (res == ATAResult::OK)
    // {
    //     kprintf("[ATA] Drive found: %s (%lu sectors, %lu MiB)\n",
    //             g_ata_primary.model,
    //             (unsigned long)g_ata_primary.sector_count,
    //             (unsigned long)(g_ata_primary.sector_count / 2048));
    //     SerialWriteString(COM1_PORT,
    //             "[ATA] Drive found: %s (%lu sectors)\n",
    //             g_ata_primary.model,
    //             (unsigned long)g_ata_primary.sector_count);
    // }
    // else
    // {
    //     kprintf("[ATA] No drive found (result=%d)\n", (int)res);
    //     SerialWriteString(COM1_PORT,
    //             "[ATA] No drive found (result=%d)\n", (int)res);
    // }

    // return res;
}

void ATAReadSector(uint8_t* buf, uint64_t lba)
{
    if (lba > ATA_LBA28_MAX)
    {
        SerialWriteString(COM1_PORT,
            "[ATA] ATAReadSector: LBA %llu exceeds LBA28 range\n", lba);
        for (int i = 0; i < 512; i++) buf[i] = 0;
        return;
    }

    ATAResult res = g_ata_primary.Read((uint32_t)lba, 1, buf);
    if (res != ATAResult::OK)
    {
        SerialWriteString(COM1_PORT,
            "[ATA] Read error at LBA %llu (result=%d)\n", lba, (int)res);
    }
}

void ATAReadSectorDrive(int drive_idx, uint8_t* buf, uint64_t lba)
{
    g_ata_drives[drive_idx].Read((uint32_t)lba, 1, buf);
}

void ATAWriteSector(const uint8_t* buf, uint64_t lba)
{
    if (lba > ATA_LBA28_MAX)
    {
        SerialWriteString(COM1_PORT,
            "[ATA] ATAWriteSector: LBA %llu exceeds LBA28 range\n", lba);
        return;
    }

    ATAResult res = g_ata_primary.Write((uint32_t)lba, 1, buf);
    if (res != ATAResult::OK)
    {
        SerialWriteString(COM1_PORT,
            "[ATA] Write error at LBA %llu (result=%d)\n", lba, (int)res);
    }
}

FsType DetectFilesystem(ATADrive* drive, uint32_t partition_lba)
{
    uint8_t buf[512];
    drive->Read(partition_lba, 1, buf);

    // FAT32: boot sector signature + fs_type string at offset 82
    if (buf[510] == 0x55 && buf[511] == 0xAA)
    {
        if (memcmp(buf + 82, "FAT32   ", 8) == 0) return FsType::FAT32;
    }

    // ext2: superblock is at byte offset 1024 from partition start
    drive->Read(partition_lba + 2, 1, buf); // sector 2 = bytes 1024-1535
    uint16_t ext_magic = *(uint16_t*)(buf + 56); // superblock magic at +56
    if (ext_magic == 0xEF53) return FsType::Ext2;

    return FsType::Unknown;
}
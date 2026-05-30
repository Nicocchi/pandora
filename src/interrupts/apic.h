/**
 * @file        apic.h
 * @brief       Advanced Programmable Interrupt Controller (APIC) structures and definitions.
 *
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 *
 * @author      Nicocchi
 * @date        May 29, 2026
 *
 * @details
 * Defines the data structures and function declarations required to initialize
 * and operate the x86_64 Local APIC and I/O APIC subsystems.
 *
 * This header covers:
 *
 * - ACPI Root System Description Pointer (RSDP) parsing structures
 * - ACPI System Description Table (SDT) header layout
 * - Multiple APIC Description Table (MADT) header and entry structures
 * - Interrupt Source Override (ISO) storage for GSI remapping
 * - Local APIC and I/O APIC initialization and EOI signaling
 *
 * The APIC subsystem replaces the legacy 8259 Programmable Interrupt Controller
 * (PIC) and is required for symmetric multiprocessing (SMP) support.
 *
 * Interrupt routing through the I/O APIC is configured using the MADT entries
 * provided by ACPI firmware, including any Interrupt Source Overrides that
 * remap legacy ISA IRQs to different Global System Interrupt (GSI) lines.
 *
 * References:
 * - ACPI Specification: https://uefi.org/specifications
 * - Local APIC: https://wiki.osdev.org/APIC
 * - I/O APIC: https://wiki.osdev.org/IOAPIC
 * - MADT: https://wiki.osdev.org/MADT
 * - SMP: https://wiki.osdev.org/SMP
 */

#pragma once

#include "boot/limine.h"

#define MADT_LOCAL_APIC     0 /**< @brief MADT entry type: Processor Local APIC */
#define MADT_IO_APIC        1 /**< @brief MADT entry type: I/O APIC */
#define MADT_ISO            2 /**< @brief MADT entry type: Interrupt Source Override */

/**
 * @defgroup    ACPI_structs ACPI Structures
 * @brief       ACPI Structures
 * @{
 */
/**
 * @brief ACPI Root System Description Pointer (RSDP) version 2.0+.
 *
 * Located by the firmware and provided by Limine via the RSDP request.
 * Contains pointers to either the RSDT (ACPI 1.0) or the XSDT (ACPI 2.0+),
 * which in turn point to all other ACPI system description tables.
 *
 * The revision field distinguishes ACPI 1.0 (revision 0) from ACPI 2.0+
 * (revision 2). When revision >= 2 and xsdt_address is non-zero, the XSDT
 * should be used in preference to the RSDT, as XSDT entries are 64-bit.
 *
 * @note Must be parsed as a packed struct to avoid compiler padding.
 *
 * @see https://wiki.osdev.org/RSDP
 */
struct RSDP2
{
    char signature[8];                  /**< "RSD PTR " signature (not null-terminated) */
    uint8_t checksum;                   /**< Checksum of the first 20 bytes; sum must equal zero */
    char oem_id[6];                     /**< OEM-supplied string identifying the OEM */
    uint8_t revision;                   /**< ACPI revision: 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;              /**< Physical address of the RSDT (ACPI 1.0) */

    // ACPI 2.0+ extended fields
    uint32_t length;                    /**< Total length of this structure in bytes (ACPI 2.0+) */
    uint64_t xsdt_address;              /**< Physical address of the XSDT (ACPI 2.0+) */
    uint8_t extended_checksum;          /**< Checksum of the entire structure (ACPI 2.0+) */
    uint8_t reserved[3];                /**< Reserved, must be zero */
} __attribute__((packed));

/**
 * @brief Common header shared by all ACPI System Description Tables.
 *
 * Every ACPI table begins with this 36-byte header. The signature field
 * identifies the table type (e.g., "APIC" for MADT, "FACP" for FADT).
 *
 * @note Must be parsed as a packed struct to avoid compiler padding.
 *
 * @see https://wiki.osdev.org/ACPI
 */
struct ACPISDTHeader
{
    char signature[4];                  /**< 4-byte ASCII table identifier (e.g., "APIC", "XSDT") */
    uint32_t length;                    /**< Total length of the table in bytes, including this header */
    uint8_t revision;                   /**< Table revision number */
    uint8_t checksum;                   /**< Checksum byte; entire table must sum to zero */
    char oem_id[6];                     /**< OEM-supplied string identifying the OEM */
    char oem_table_id[8];               /**< OEM-supplied string identifying the table */
    uint32_t oem_revision;              /**< OEM-supplied revision number for this table */
    uint32_t creator_id;                /**< Vendor ID of the utility that created the table */
    uint32_t creator_revision;          /**< Revision of the utility that created the table */
} __attribute__((packed));
/** @} */

/**
 * @defgroup    MADT_structs MADT Structures
 * @brief       MADT Structures
 * @{
 */

/**
 * @brief Multiple APIC Description Table (MADT) header.
 *
 * Follows the standard ACPISDTHeader and provides the default Local APIC
 * base address as reported by firmware. Variable-length MADT entries
 * immediately follow this header and describe each APIC component
 * present in the system.
 *
 * The entries are walked sequentially using each entry's length field
 * to advance to the next entry.
 *
 * @note Must be parsed as a packed struct to avoid compiler padding.
 *
 * @see https://wiki.osdev.org/MADT
 */
struct MADTHeader
{
    uint32_t signature;                 /**< Table signature, corresponds to "APIC" (0x43495041) */
    uint32_t length;                    /**< Total length of the MADT in bytes, including all entries */
    uint8_t revision;                   /**< MADT revision number */
    uint8_t checksum;                   /**< Checksum byte; entire table must sum to zero */
    uint8_t oem_id[6];                  /**< OEM identifier string */
    uint8_t oem_table_id[8];            /**< OEM table identifier string */
    uint32_t oem_revision;              /**< OEM revision number */
    uint32_t creator_id;                /**< Creator tool vendor ID */
    uint32_t creator_revision;          /**< Creator tool revision */
    uint32_t local_apic_addr;           /**< Physical address of the Local APIC (may be overridden by entry type 5) */
    uint32_t flags;                     /**< Bit 0: dual 8259 PICs installed; other bits reserved */
} __attribute__((packed));

/**
 * @brief Common header for all variable-length MADT entries.
 *
 * Every MADT entry begins with this two-byte header. The type field
 * identifies the entry kind and the length field gives the total byte
 * size of the entry, allowing the MADT walker to skip unknown types.
 *
 * @note Must be parsed as a packed struct to avoid compiler padding.
 */
struct MADTEntry
{
    uint8_t type;                       /**< Entry type (see MADT_LOCAL_APIC, MADT_IO_APIC, MADT_ISO, etc.) */
    uint8_t length;                     /**< Total length of this entry in bytes, including this header */
} __attribute__((packed));

/**
 * @brief MADT entry describing a Processor Local APIC (type 0).
 *
 * One entry of this type exists per logical processor in the system.
 * The flags field indicates whether the processor is enabled and
 * available for use. Only processors with bit 0 of flags set should
 * be considered active.
 *
 * @note Must be parsed as a packed struct to avoid compiler padding.
 *
 * @see https://wiki.osdev.org/MADT
 */
struct MADTLocalAPIC
{
    MADTEntry header;                   /**< Common MADT entry header (type = 0, length = 8) */
    uint8_t processor_id;               /**< ACPI processor UID, used to match with the ACPI Processor object */
    uint8_t apic_id;                    /**< Local APIC ID for this processor */
    uint32_t flags;                     /**< Bit 0: processor enabled; bit 1: online capable */
} __attribute__((packed));

/**
 * @brief MADT entry describing an I/O APIC (type 1).
 *
 * Provides the physical base address of an I/O APIC and its Global System
 * Interrupt (GSI) base, which is the first GSI number that this I/O APIC
 * handles. Systems with multiple I/O APICs assign non-overlapping GSI ranges
 * to each unit.
 *
 * @note Must be parsed as a packed struct to avoid compiler padding.
 *
 * @see https://wiki.osdev.org/IOAPIC
 */
struct MADTIOApic
{
    MADTEntry header;                   /**< Common MADT entry header (type = 1, length = 12) */
    uint8_t io_apic_id;                 /**< I/O APIC hardware ID */
    uint8_t reserved;                   /**< Reserved, must be zero */
    uint32_t io_apic_addr;              /**< Physical base address of the I/O APIC MMIO registers */
    uint32_t gsi_base;                  /**< Global System Interrupt number where this I/O APIC's inputs begin */
} __attribute__((packed));

/**
 * @brief MADT entry describing an Interrupt Source Override (type 2).
 *
 * Interrupt Source Overrides describe differences between the IA-PC standard
 * interrupt bus (ISA) and the actual hardware routing. For example, ISA IRQ 0
 * (PIT timer) is often physically wired to GSI 2 on the I/O APIC rather than
 * GSI 0, and this entry records that remapping.
 *
 * The flags field encodes polarity and trigger mode:
 * - Bits 0-1: Polarity (00 = bus default, 01 = active high, 11 = active low)
 * - Bits 2-3: Trigger mode (00 = bus default, 01 = edge, 11 = level)
 *
 * @note Must be parsed as a packed struct to avoid compiler padding.
 *
 * @see https://wiki.osdev.org/MADT
 */
struct MADTIso
{
    MADTEntry header;                   /**< Common MADT entry header (type = 2, length = 10) */
    uint8_t bus;                        /**< Source bus; always 0 (ISA) */
    uint8_t irq;                        /**< Source ISA IRQ number being overridden */
    uint32_t gsi;                       /**< Target Global System Interrupt number on the I/O APIC */
    uint16_t flags;                     /**< MPS INTI flags: polarity (bits 0-1) and trigger mode (bits 2-3) */
} __attribute__((packed));
 /** @} */

/**
 * @defgroup    Kernel_structs Internal Kernel Structures
 * @brief       Internal Kernel Structures
 * @{
 */

/**
 * @brief Kernel-internal representation of a parsed Interrupt Source Override.
 *
 * Populated during MADT parsing and used by InitIOAPIC() to correctly
 * route legacy ISA IRQs to their actual GSI lines on the I/O APIC.
 * Up to 16 ISOs are stored, matching the maximum number of ISA IRQ lines.
 */
struct ISOEntry
{
    uint8_t irq;                        /**< Legacy ISA IRQ number (0-15) */
    uint32_t gsi;                       /**< Corresponding Global System Interrupt on the I/O APIC */
    uint16_t flags;                     /**< MPS INTI flags copied from the MADT ISO entry */
};
 /** @} */


/**
 * @brief Initializes the Local APIC for the current processor.
 *
 * Enables the Local APIC by setting the Software Enable bit (bit 8) in the
 * Spurious Interrupt Vector Register and configures vector 0xFF as the
 * spurious interrupt vector. Must be called after InitAPIC() has mapped
 * the Local APIC MMIO region into virtual memory.
 */
void InitLAPIC();

/**
 * @brief Signals End Of Interrupt to the Local APIC.
 *
 * Must be called at the end of every hardware interrupt handler to allow
 * the Local APIC to accept further interrupts. Writing any value to the
 * EOI register acknowledges the current interrupt.
 *
 * @note Do NOT call this for NMIs, SMIs, INIT, or spurious interrupts.
 */
void LAPICEoi();

/**
 * @brief Initializes the I/O APIC and configures interrupt routing.
 *
 * Routes the PIT timer IRQ (IRQ 0) to IDT vector 32 on CPU 0, taking into
 * account any Interrupt Source Override present in the MADT that remaps
 * IRQ 0 to a different GSI. Must be called after InitAPIC() has mapped
 * the I/O APIC MMIO region and populated the ISO table.
 */
void InitIOAPIC();

/**
 * @brief Configures a single I/O APIC redirection table entry.
 *
 * Each redirection entry maps a GSI input pin on the I/O APIC to a specific
 * IDT vector, destination CPU (by APIC ID), and delivery/trigger settings.
 *
 * @param irq       I/O APIC GSI input pin number to configure
 * @param vector    IDT vector number to deliver the interrupt to (32-255)
 * @param apic_id   Local APIC ID of the destination CPU
 * @param mask      If true, the interrupt is masked (disabled); false to enable
 */
void IOAPICSetRedirect(uint8_t irq, uint8_t vector, uint8_t apic_id, bool mask);

/**
 * @brief Parses ACPI tables and initializes the APIC subsystem.
 *
 * Performs the following steps:
 *
 * 1. Retrieves the RSDP address from the Limine bootloader response.
 * 2. Walks the XSDT (ACPI 2.0+) or RSDT (ACPI 1.0) to locate the MADT.
 * 3. Parses all MADT entries to discover Local APIC and I/O APIC addresses
 *    and records any Interrupt Source Overrides.
 * 4. Maps the Local APIC and I/O APIC MMIO regions into virtual memory by
 *    splitting existing huge page mappings in the kernel page tables.
 *
 * Must be called before InitLAPIC(), InitIOAPIC(), or DisablePIC().
 * Panics if the RSDP, HHDM, MADT, or I/O APIC cannot be found.
 */
void InitAPIC();
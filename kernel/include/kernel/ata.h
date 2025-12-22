#ifndef _KERNEL_ATA_H
#define _KERNEL_ATA_H

#include <stdint.h>
#include <stdbool.h>

// ATA registers (primary bus)
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CONTROL 0x376

// ATA register offsets
#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_FEATURES   1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LO     3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HI     5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

// ATA commands
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_READ_DMA      0xC8
#define ATA_CMD_WRITE_DMA     0xCA
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_CACHE_FLUSH   0xE7

// Bus Master IDE registers
#define BM_COMMAND_REG    0
#define BM_STATUS_REG     2
#define BM_PRDT_REG       4

// Bus Master commands
#define BM_CMD_START      0x01
#define BM_CMD_READ       0x08  // 0 = write to memory, 1 = read from memory

// Bus Master status
#define BM_STATUS_ERROR   0x02
#define BM_STATUS_IRQ     0x04
#define BM_STATUS_DMA0    0x20
#define BM_STATUS_DMA1    0x40

// ATA status bits
#define ATA_SR_BSY  0x80  // Busy
#define ATA_SR_DRDY 0x40  // Drive ready
#define ATA_SR_DF   0x20  // Drive write fault
#define ATA_SR_DSC  0x10  // Drive seek complete
#define ATA_SR_DRQ  0x08  // Data request ready
#define ATA_SR_CORR 0x04  // Corrected data
#define ATA_SR_IDX  0x02  // Index
#define ATA_SR_ERR  0x01  // Error

// Sector size
#define ATA_SECTOR_SIZE 512

// ATA device structure
typedef struct {
    uint16_t base;              // I/O base address
    uint16_t control;           // Control base address
    uint16_t bmide;             // Bus Master IDE base address
    uint8_t drive;              // 0 = master, 1 = slave
    bool exists;                // Does this device exist?
    uint32_t size_sectors;      // Size in sectors
    char model[41];             // Model string
} ata_device_t;

// Physical Region Descriptor Table entry
typedef struct {
    uint32_t buffer_phys;       // Physical address of buffer
    uint16_t byte_count;        // Number of bytes (0 = 64KB)
    uint16_t reserved;          // Bit 15 = end of table marker
} __attribute__((packed)) prdt_entry_t;

// Initialize ATA driver
void ata_init(void);

// Read sectors from disk
bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, uint8_t *buffer);

// Write sectors to disk
bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, const uint8_t *buffer);

// Get device information
ata_device_t* ata_get_device(uint8_t drive);

// Wait for drive to be ready
bool ata_wait_ready(uint16_t base);

// Wait for DRQ (data request)
bool ata_wait_drq(uint16_t base);

#endif // _KERNEL_ATA_H

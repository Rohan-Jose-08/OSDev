#include <kernel/ata.h>
#include <kernel/io.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>

static ata_device_t ata_devices[4]; // Primary master/slave, Secondary master/slave

// Read ATA register
static inline uint8_t ata_read_reg(uint16_t base, uint8_t reg) {
    return inb(base + reg);
}

// Write ATA register
static inline void ata_write_reg(uint16_t base, uint8_t reg, uint8_t value) {
    outb(base + reg, value);
}

// Wait for BSY to clear
bool ata_wait_ready(uint16_t base) {
    uint8_t status;
    // Wait up to ~1 second
    for (int i = 0; i < 10000; i++) {
        status = ata_read_reg(base, ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
        // Small delay
        for (volatile int j = 0; j < 1000; j++);
    }
    return false;
}

// Wait for DRQ to be set
bool ata_wait_drq(uint16_t base) {
    uint8_t status;
    // Wait up to ~1 second
    for (int i = 0; i < 10000; i++) {
        status = ata_read_reg(base, ATA_REG_STATUS);
        if (status & ATA_SR_DRQ) {
            return true;
        }
        // Small delay
        for (volatile int j = 0; j < 1000; j++);
    }
    return false;
}

// Identify ATA device
static bool ata_identify(uint16_t base, uint8_t drive_sel, ata_device_t *device) {
    // Select drive
    ata_write_reg(base, ATA_REG_DRIVE, 0xA0 | (drive_sel << 4));
    
    // Small delay after drive select
    for (volatile int i = 0; i < 10000; i++);
    
    // Send IDENTIFY command
    ata_write_reg(base, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    // Small delay
    for (volatile int i = 0; i < 10000; i++);
    
    // Check if drive exists
    uint8_t status = ata_read_reg(base, ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return false; // No drive
    }
    
    // Wait for BSY to clear
    if (!ata_wait_ready(base)) {
        return false;
    }
    
    // Check for errors
    status = ata_read_reg(base, ATA_REG_STATUS);
    if (status & ATA_SR_ERR) {
        return false;
    }
    
    // Wait for DRQ
    if (!ata_wait_drq(base)) {
        return false;
    }
    
    // Read identification data
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(base + ATA_REG_DATA);
    }
    
    // Extract model string (words 27-46)
    char *model = device->model;
    for (int i = 0; i < 20; i++) {
        model[i * 2] = (char)(identify_data[27 + i] >> 8);
        model[i * 2 + 1] = (char)(identify_data[27 + i] & 0xFF);
    }
    model[40] = '\0';
    
    // Trim trailing spaces
    for (int i = 39; i >= 0; i--) {
        if (model[i] == ' ') {
            model[i] = '\0';
        } else {
            break;
        }
    }
    
    // Get size in sectors (words 60-61 for 28-bit LBA)
    device->size_sectors = (uint32_t)identify_data[60] | ((uint32_t)identify_data[61] << 16);
    
    return true;
}

// Initialize ATA driver
void ata_init(void) {
    printf("ATA: Initializing IDE/ATA driver...\n");
    
    memset(ata_devices, 0, sizeof(ata_devices));
    
    // Check primary master
    ata_devices[0].base = ATA_PRIMARY_IO;
    ata_devices[0].control = ATA_PRIMARY_CONTROL;
    ata_devices[0].drive = 0;
    if (ata_identify(ATA_PRIMARY_IO, 0, &ata_devices[0])) {
        ata_devices[0].exists = true;
        printf("ATA: Primary master detected: %s (%u sectors, %u MB)\n",
               ata_devices[0].model,
               ata_devices[0].size_sectors,
               (ata_devices[0].size_sectors / 2048));
    }
    
    // Check primary slave
    ata_devices[1].base = ATA_PRIMARY_IO;
    ata_devices[1].control = ATA_PRIMARY_CONTROL;
    ata_devices[1].drive = 1;
    if (ata_identify(ATA_PRIMARY_IO, 1, &ata_devices[1])) {
        ata_devices[1].exists = true;
        printf("ATA: Primary slave detected: %s (%u sectors, %u MB)\n",
               ata_devices[1].model,
               ata_devices[1].size_sectors,
               (ata_devices[1].size_sectors / 2048));
    }
    
    // Check secondary master
    ata_devices[2].base = ATA_SECONDARY_IO;
    ata_devices[2].control = ATA_SECONDARY_CONTROL;
    ata_devices[2].drive = 0;
    if (ata_identify(ATA_SECONDARY_IO, 0, &ata_devices[2])) {
        ata_devices[2].exists = true;
        printf("ATA: Secondary master detected: %s (%u sectors, %u MB)\n",
               ata_devices[2].model,
               ata_devices[2].size_sectors,
               (ata_devices[2].size_sectors / 2048));
    }
    
    // Check secondary slave
    ata_devices[3].base = ATA_SECONDARY_IO;
    ata_devices[3].control = ATA_SECONDARY_CONTROL;
    ata_devices[3].drive = 1;
    if (ata_identify(ATA_SECONDARY_IO, 1, &ata_devices[3])) {
        ata_devices[3].exists = true;
        printf("ATA: Secondary slave detected: %s (%u sectors, %u MB)\n",
               ata_devices[3].model,
               ata_devices[3].size_sectors,
               (ata_devices[3].size_sectors / 2048));
    }
}

// Get device by drive number
ata_device_t* ata_get_device(uint8_t drive) {
    if (drive >= 4) {
        return NULL;
    }
    return ata_devices[drive].exists ? &ata_devices[drive] : NULL;
}

// Read sectors from disk
bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (drive >= 4 || !ata_devices[drive].exists) {
        return false;
    }
    
    ata_device_t *device = &ata_devices[drive];
    uint16_t base = device->base;
    
    // Wait for drive to be ready
    if (!ata_wait_ready(base)) {
        return false;
    }
    
    // Select drive and set LBA mode with top 4 bits of LBA
    ata_write_reg(base, ATA_REG_DRIVE, 0xE0 | (device->drive << 4) | ((lba >> 24) & 0x0F));
    
    // Set sector count
    ata_write_reg(base, ATA_REG_SECCOUNT, sector_count);
    
    // Set LBA low, mid, high
    ata_write_reg(base, ATA_REG_LBA_LO, lba & 0xFF);
    ata_write_reg(base, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    ata_write_reg(base, ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    
    // Send READ command
    ata_write_reg(base, ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
    
    // Read sectors
    for (int i = 0; i < sector_count; i++) {
        // Wait for DRQ
        if (!ata_wait_drq(base)) {
            return false;
        }
        
        // Read sector data
        uint16_t *buf16 = (uint16_t *)(buffer + i * ATA_SECTOR_SIZE);
        for (int j = 0; j < 256; j++) {
            buf16[j] = inw(base + ATA_REG_DATA);
        }
    }
    
    return true;
}

// Write sectors to disk
bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, const uint8_t *buffer) {
    if (drive >= 4 || !ata_devices[drive].exists) {
        return false;
    }
    
    ata_device_t *device = &ata_devices[drive];
    uint16_t base = device->base;
    
    // Wait for drive to be ready
    if (!ata_wait_ready(base)) {
        return false;
    }
    
    // Select drive and set LBA mode with top 4 bits of LBA
    ata_write_reg(base, ATA_REG_DRIVE, 0xE0 | (device->drive << 4) | ((lba >> 24) & 0x0F));
    
    // Set sector count
    ata_write_reg(base, ATA_REG_SECCOUNT, sector_count);
    
    // Set LBA low, mid, high
    ata_write_reg(base, ATA_REG_LBA_LO, lba & 0xFF);
    ata_write_reg(base, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    ata_write_reg(base, ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    
    // Send WRITE command
    ata_write_reg(base, ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
    
    // Write sectors
    for (int i = 0; i < sector_count; i++) {
        // Wait for DRQ
        if (!ata_wait_drq(base)) {
            return false;
        }
        
        // Write sector data
        const uint16_t *buf16 = (const uint16_t *)(buffer + i * ATA_SECTOR_SIZE);
        for (int j = 0; j < 256; j++) {
            outw(base + ATA_REG_DATA, buf16[j]);
        }
    }
    
    // Flush cache
    ata_write_reg(base, ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (!ata_wait_ready(base)) {
        return false;
    }
    
    return true;
}

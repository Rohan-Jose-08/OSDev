#include <kernel/ata.h>
#include <kernel/io.h>
#include <kernel/memory.h>
#include <kernel/pci.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>

static ata_device_t ata_devices[4]; // Primary master/slave, Secondary master/slave

// DMA buffers and PRDT (must be physically contiguous and aligned)
static uint8_t dma_buffer[65536] __attribute__((aligned(65536)));
static prdt_entry_t prdt[16] __attribute__((aligned(4)));

// Bus Master IDE base addresses (will be detected or use defaults)
static uint16_t primary_bmide = 0;
static uint16_t secondary_bmide = 0;
#ifndef ATA_ENABLE_DMA
#define ATA_ENABLE_DMA 0
#endif
#ifndef ATA_DMA_VERIFY
#define ATA_DMA_VERIFY 1
#endif
static bool ata_dma_enabled = (ATA_ENABLE_DMA != 0);
static bool ata_dma_verified = false;
static uint8_t dma_verify_buffer[ATA_SECTOR_SIZE];

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
    for (int i = 0; i < 100000; i++) {
        status = ata_read_reg(base, ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
    }
    return false;
}

// Wait for DRQ to be set
bool ata_wait_drq(uint16_t base) {
    uint8_t status;
    for (int i = 0; i < 100000; i++) {
        status = ata_read_reg(base, ATA_REG_STATUS);
        if (status & ATA_SR_DRQ) {
            return true;
        }
        if (status & ATA_SR_ERR) {
            return false;
        }
    }
    return false;
}

// Setup DMA transfer
static bool ata_setup_dma(uint16_t bmide, const uint8_t *buffer, uint32_t byte_count, bool is_write) {
    if (bmide == 0 || byte_count == 0 || byte_count > 65536) {
        return false;
    }
    
    // Build PRDT - split into chunks if needed
    uint32_t remaining = byte_count;
    uint32_t offset = 0;
    int prdt_entries = 0;
    
    while (remaining > 0 && prdt_entries < 16) {
        uint32_t chunk_size = (remaining > 65536) ? 65536 : remaining;
        
        prdt[prdt_entries].buffer_phys = virt_to_phys(buffer + offset);
        prdt[prdt_entries].byte_count = (chunk_size == 65536) ? 0 : chunk_size;  // 0 means 64KB
        prdt[prdt_entries].reserved = (remaining <= chunk_size) ? 0x8000 : 0;  // Set EOT on last entry
        
        remaining -= chunk_size;
        offset += chunk_size;
        prdt_entries++;
    }
    
    // Stop any current DMA transfer
    outb(bmide + BM_COMMAND_REG, 0);
    
    // Set PRDT address
    outl(bmide + BM_PRDT_REG, virt_to_phys(prdt));
    
    // Clear error and interrupt flags
    uint8_t status = inb(bmide + BM_STATUS_REG);
    outb(bmide + BM_STATUS_REG, status | BM_STATUS_ERROR | BM_STATUS_IRQ);
    
    // Set direction (0 = write to memory/read from drive, 1 = read from memory/write to drive)
    uint8_t cmd = is_write ? BM_CMD_READ : 0;  // Yes, this is backwards!
    outb(bmide + BM_COMMAND_REG, cmd);
    
    return true;
}

// Start DMA transfer
static void ata_start_dma(uint16_t bmide) {
    uint8_t cmd = inb(bmide + BM_COMMAND_REG);
    outb(bmide + BM_COMMAND_REG, cmd | BM_CMD_START);
}

// Wait for DMA completion
static bool ata_wait_dma(uint16_t bmide, uint16_t ata_base) {
    // Wait for DMA completion IRQ or drive becoming idle.
    for (int i = 0; i < 100000; i++) {
        uint8_t bm_status = inb(bmide + BM_STATUS_REG);
        uint8_t ata_status = ata_read_reg(ata_base, ATA_REG_STATUS);

        if (bm_status & BM_STATUS_IRQ) {
            outb(bmide + BM_COMMAND_REG, 0);
            outb(bmide + BM_STATUS_REG, bm_status | BM_STATUS_ERROR | BM_STATUS_IRQ);

            if ((bm_status & BM_STATUS_ERROR) || (ata_status & ATA_SR_ERR)) {
                return false;
            }
            if (!(ata_status & ATA_SR_BSY)) {
                return true;
            }
        } else if (!(ata_status & ATA_SR_BSY) && (ata_status & ATA_SR_DRDY)) {
            // Some controllers don't assert IRQ in polling mode.
            outb(bmide + BM_COMMAND_REG, 0);
            outb(bmide + BM_STATUS_REG, bm_status | BM_STATUS_ERROR | BM_STATUS_IRQ);
            if (ata_status & ATA_SR_ERR) {
                return false;
            }
            return true;
        }
    }

    // Timeout - stop DMA
    outb(bmide + BM_COMMAND_REG, 0);
    return false;
}

void ata_set_dma_enabled(bool enabled) {
    ata_dma_enabled = enabled;
    ata_dma_verified = false;
}

bool ata_dma_is_enabled(void) {
    return ata_dma_enabled;
}

static bool ata_verify_dma_write(uint8_t drive, uint32_t lba, const uint8_t *buffer) {
    if (!ata_read_sectors(drive, lba, 1, dma_verify_buffer)) {
        return false;
    }
    return memcmp(dma_verify_buffer, buffer, ATA_SECTOR_SIZE) == 0;
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
    
    // Capabilities (word 49)
    device->dma_supported = (identify_data[49] & (1 << 8)) != 0;

    // Get size in sectors (words 60-61 for 28-bit LBA)
    device->size_sectors = (uint32_t)identify_data[60] | ((uint32_t)identify_data[61] << 16);

    return true;
}

// Initialize ATA driver
void ata_init(void) {
    printf("ATA: Initializing IDE/ATA driver...\n");
    
    memset(ata_devices, 0, sizeof(ata_devices));

    bool bmide_found = false;
    pci_device_t ide_dev;
    if (pci_find_class(0x01, 0x01, 0xFF, &ide_dev)) {
        pci_enable_bus_master(&ide_dev);
        uint32_t bmide_bar = ide_dev.bar[4];
        uint16_t bmide_base = 0;
        if (bmide_bar & 0x1) {
            bmide_base = (uint16_t)(bmide_bar & ~0x3);
        }
        if (bmide_base == 0) {
            uint32_t alt_bar = ide_dev.bar[5];
            if (alt_bar & 0x1) {
                bmide_base = (uint16_t)(alt_bar & ~0x3);
            }
        }
        if (bmide_base != 0) {
            primary_bmide = bmide_base;
            secondary_bmide = (uint16_t)(bmide_base + 8);
            bmide_found = true;
            printf("ATA: PCI IDE BMIDE at 0x%x (bus %u slot %u func %u)\n",
                   bmide_base, ide_dev.bus, ide_dev.slot, ide_dev.func);
        }
    }

    if (!bmide_found) {
        // Try to detect Bus Master IDE address from common locations
        // Most systems use 0xC000-0xC00F for primary/secondary BMIDE
        // Try reading the status register - if it returns sensible values, we found it
        for (uint16_t test_addr = 0xC000; test_addr < 0xD000; test_addr += 0x10) {
            uint8_t status = inb(test_addr + BM_STATUS_REG);
            // Valid status should have some bits but not all FFs
            if (status != 0xFF && status != 0x00) {
                primary_bmide = test_addr;
                secondary_bmide = test_addr + 8;
                printf("ATA: Detected Bus Master IDE at 0x%x\n", test_addr);
                bmide_found = true;
                break;
            }
        }
    }
    
    if (primary_bmide == 0) {
        printf("ATA: Bus Master IDE not detected, DMA disabled\n");
        ata_dma_enabled = false;
    } else if (!ata_dma_enabled) {
        printf("ATA: DMA disabled, using PIO\n");
    } else {
        printf("ATA: DMA enabled\n");
    }
    
    // Check primary master
    ata_devices[0].base = ATA_PRIMARY_IO;
    ata_devices[0].control = ATA_PRIMARY_CONTROL;
    ata_devices[0].bmide = primary_bmide;
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
    ata_devices[1].bmide = primary_bmide;
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
    ata_devices[2].bmide = secondary_bmide;
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
    ata_devices[3].bmide = secondary_bmide;
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

// Write sectors to disk using DMA (with PIO fallback)
bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t sector_count, const uint8_t *buffer) {
    if (drive >= 4 || !ata_devices[drive].exists || sector_count == 0) {
        return false;
    }
    
    ata_device_t *device = &ata_devices[drive];
    uint16_t base = device->base;
    uint16_t bmide = device->bmide;
    
    // Try DMA if available
    if (ata_dma_enabled && device->dma_supported && bmide != 0 && sector_count <= 128) {
        uint32_t byte_count = sector_count * ATA_SECTOR_SIZE;
        
        // Copy to aligned DMA buffer
        if (byte_count <= sizeof(dma_buffer)) {
            memcpy(dma_buffer, buffer, byte_count);
            
            // Wait for drive ready
            if (!ata_wait_ready(base)) {
                goto fallback_pio;
            }
            
            // Setup DMA
            if (!ata_setup_dma(bmide, dma_buffer, byte_count, true)) {
                goto fallback_pio;
            }
            
            // Select drive and set LBA mode
            ata_write_reg(base, ATA_REG_DRIVE, 0xE0 | (device->drive << 4) | ((lba >> 24) & 0x0F));
            
            // Set sector count and LBA
            ata_write_reg(base, ATA_REG_SECCOUNT, sector_count);
            ata_write_reg(base, ATA_REG_LBA_LO, lba & 0xFF);
            ata_write_reg(base, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
            ata_write_reg(base, ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
            
            // Send DMA WRITE command
            ata_write_reg(base, ATA_REG_COMMAND, ATA_CMD_WRITE_DMA);
            
            // Start DMA
            ata_start_dma(bmide);
            
            // Wait for completion
            if (ata_wait_dma(bmide, base)) {
#if ATA_DMA_VERIFY
                if (!ata_dma_verified) {
                    if (!ata_verify_dma_write(drive, lba, buffer)) {
                        printf("ATA: DMA verify failed, disabling DMA\n");
                        ata_dma_enabled = false;
                        goto fallback_pio;
                    }
                    ata_dma_verified = true;
                }
#endif
                return true;
            }

            // DMA failed, fall back to PIO and disable DMA
            printf("ATA: DMA failed, falling back to PIO\n");
            ata_dma_enabled = false;
        }
    }
    
fallback_pio:
    // PIO fallback - write in chunks
    uint8_t remaining = sector_count;
    uint32_t offset = 0;
    
    while (remaining > 0) {
        uint8_t chunk_size = (remaining > 64) ? 64 : remaining;
        
        // Wait for drive to be ready
        if (!ata_wait_ready(base)) {
            return false;
        }
        
        // Select drive and set LBA mode
        uint32_t current_lba = lba + (offset / ATA_SECTOR_SIZE);
        ata_write_reg(base, ATA_REG_DRIVE, 0xE0 | (device->drive << 4) | ((current_lba >> 24) & 0x0F));
        
        // Set sector count and LBA
        ata_write_reg(base, ATA_REG_SECCOUNT, chunk_size);
        ata_write_reg(base, ATA_REG_LBA_LO, current_lba & 0xFF);
        ata_write_reg(base, ATA_REG_LBA_MID, (current_lba >> 8) & 0xFF);
        ata_write_reg(base, ATA_REG_LBA_HI, (current_lba >> 16) & 0xFF);
        
        // Send WRITE command
        ata_write_reg(base, ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
        
        // Write sectors in this chunk
        for (int i = 0; i < chunk_size; i++) {
            // Wait for DRQ
            if (!ata_wait_drq(base)) {
                return false;
            }
            
            // Write sector data
            const uint16_t *buf16 = (const uint16_t *)(buffer + offset + i * ATA_SECTOR_SIZE);
            for (int j = 0; j < 256; j++) {
                outw(base + ATA_REG_DATA, buf16[j]);
            }
        }
        
        remaining -= chunk_size;
        offset += chunk_size * ATA_SECTOR_SIZE;
    }
    
    // Wait for write to complete
    if (!ata_wait_ready(base)) {
        return false;
    }
    
    return true;
}

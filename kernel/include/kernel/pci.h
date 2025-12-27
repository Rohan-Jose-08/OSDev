#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_id;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t irq_line;
    uint32_t bar[6];
} pci_device_t;

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *out_dev);
bool pci_find_class(uint8_t class_id, uint8_t subclass, uint8_t prog_if, pci_device_t *out_dev);
void pci_enable_bus_master(const pci_device_t *dev);

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);

#endif

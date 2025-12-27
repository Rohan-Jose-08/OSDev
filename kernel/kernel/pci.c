#include <kernel/pci.h>
#include <kernel/io.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_BUS_MASTER 0x4

static uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                      ((uint32_t)func << 8) | (offset & 0xFC));
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read_config32(bus, slot, func, offset);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read_config32(bus, slot, func, offset);
    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t cur = pci_read_config32(bus, slot, func, offset);
    uint32_t shift = (offset & 2) * 8;
    cur &= ~(0xFFFFu << shift);
    cur |= ((uint32_t)value << shift);
    pci_write_config32(bus, slot, func, offset, cur);
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t cur = pci_read_config32(bus, slot, func, offset);
    uint32_t shift = (offset & 3) * 8;
    cur &= ~(0xFFu << shift);
    cur |= ((uint32_t)value << shift);
    pci_write_config32(bus, slot, func, offset, cur);
}

static bool pci_device_present(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_read_config16(bus, slot, func, 0x00) != 0xFFFF;
}

static void pci_fill_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *dev) {
    if (!dev) {
        return;
    }
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = pci_read_config16(bus, slot, func, 0x00);
    dev->device_id = pci_read_config16(bus, slot, func, 0x02);
    dev->revision = pci_read_config8(bus, slot, func, 0x08);
    dev->prog_if = pci_read_config8(bus, slot, func, 0x09);
    dev->subclass = pci_read_config8(bus, slot, func, 0x0A);
    dev->class_id = pci_read_config8(bus, slot, func, 0x0B);
    dev->header_type = pci_read_config8(bus, slot, func, 0x0E);
    dev->irq_line = pci_read_config8(bus, slot, func, 0x3C);

    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_config32(bus, slot, func, (uint8_t)(0x10 + i * 4));
    }
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *out_dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            if (!pci_device_present((uint8_t)bus, slot, 0)) {
                continue;
            }
            uint8_t header = pci_read_config8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t funcs = (header & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < funcs; func++) {
                if (!pci_device_present((uint8_t)bus, slot, func)) {
                    continue;
                }
                uint16_t ven = pci_read_config16((uint8_t)bus, slot, func, 0x00);
                uint16_t dev = pci_read_config16((uint8_t)bus, slot, func, 0x02);
                if (ven == vendor_id && dev == device_id) {
                    pci_fill_device((uint8_t)bus, slot, func, out_dev);
                    return true;
                }
            }
        }
    }
    return false;
}

bool pci_find_class(uint8_t class_id, uint8_t subclass, uint8_t prog_if, pci_device_t *out_dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            if (!pci_device_present((uint8_t)bus, slot, 0)) {
                continue;
            }
            uint8_t header = pci_read_config8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t funcs = (header & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < funcs; func++) {
                if (!pci_device_present((uint8_t)bus, slot, func)) {
                    continue;
                }
                uint8_t dev_class = pci_read_config8((uint8_t)bus, slot, func, 0x0B);
                uint8_t dev_subclass = pci_read_config8((uint8_t)bus, slot, func, 0x0A);
                uint8_t dev_prog_if = pci_read_config8((uint8_t)bus, slot, func, 0x09);
                if (dev_class == class_id && dev_subclass == subclass &&
                    (prog_if == 0xFF || dev_prog_if == prog_if)) {
                    pci_fill_device((uint8_t)bus, slot, func, out_dev);
                    return true;
                }
            }
        }
    }
    return false;
}

void pci_enable_bus_master(const pci_device_t *dev) {
    if (!dev) {
        return;
    }
    uint16_t cmd = pci_read_config16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);
    pci_write_config16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

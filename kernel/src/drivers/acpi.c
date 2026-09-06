/*
 * SzpontOS - ACPI (Advanced Configuration and Power Interface) Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <drivers/acpi.h>
#include <drivers/pci.h>
#include <arch/x86_64/io.h>
#include <limine.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <kernel/kprint.h>
#include <kernel/string.h>

__attribute__((used, section(".requests"))) static volatile struct limine_rsdp_request g_rsdp_request = {
    .id = LIMINE_RSDP_REQUEST, .revision = 0};

static acpi_rsdp_t *g_rsdp = NULL;
static acpi_sdt_header_t *g_rsdt_or_xsdt = NULL;
static bool g_use_xsdt = false;
static acpi_fadt_t *g_fadt = NULL;
static acpi_madt_t *g_madt = NULL;
static uint16_t g_slp_typa = 0;
static uint16_t g_slp_typb = 0;
static bool g_s5_found = false;

static void acpi_map_region(uintptr_t phys, size_t len) {
    uintptr_t start = phys & ~0xFFFULL;
    uintptr_t end = (phys + len + PAGE_SIZE - 1) & ~0xFFFULL;
    for (uintptr_t p = start; p < end; p += PAGE_SIZE) {
        vmm_map_page(&g_kernel_pagemap, (uintptr_t)PHYS_TO_VIRT(p), p, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    }
}

acpi_sdt_header_t *acpi_find_table(const char *signature) {
    if (!g_rsdt_or_xsdt || !signature)
        return NULL;

    size_t sig_len = strlen(signature);
    if (sig_len != 4)
        return NULL;

    if (g_use_xsdt) {
        acpi_xsdt_t *xsdt = (acpi_xsdt_t *)g_rsdt_or_xsdt;
        size_t count = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / sizeof(uint64_t);
        for (size_t i = 0; i < count; i++) {
            uintptr_t phys = (uintptr_t)xsdt->tables[i];
            if (!phys)
                continue;

            acpi_map_region(phys, sizeof(acpi_sdt_header_t));
            acpi_sdt_header_t *tbl = (acpi_sdt_header_t *)PHYS_TO_VIRT(phys);
            if (memcmp(tbl->signature, signature, 4) == 0) {
                acpi_map_region(phys, tbl->length);
                return tbl;
            }
        }
    } else {
        acpi_rsdt_t *rsdt = (acpi_rsdt_t *)g_rsdt_or_xsdt;
        size_t count = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / sizeof(uint32_t);
        for (size_t i = 0; i < count; i++) {
            uintptr_t phys = (uintptr_t)rsdt->tables[i];
            if (!phys)
                continue;

            acpi_map_region(phys, sizeof(acpi_sdt_header_t));
            acpi_sdt_header_t *tbl = (acpi_sdt_header_t *)PHYS_TO_VIRT(phys);
            if (memcmp(tbl->signature, signature, 4) == 0) {
                acpi_map_region(phys, tbl->length);
                return tbl;
            }
        }
    }

    return NULL;
}

bool acpi_has_8042_controller(void) {
    if (!g_fadt)
        return true; /* Default to true if FADT absent */

    /*
     * In ACPI 1.0 (rev 1) and ACPI 2.0 (rev 2), the 8042 bit in IA-PC Boot Architecture Flags
     * does not exist (reserved/zero). All legacy PCs and VMs (e.g. QEMU) are assumed to have 8042.
     * Only in ACPI 3.0+ (Revision >= 3 and table length >= 244) is Bit 1 defined as 8042 presence.
     */
    if (g_fadt->header.revision >= 3 && g_fadt->header.length >= 244) {
        return (g_fadt->ia_pc_boot_arch & (1 << 1)) != 0;
    }

    return true;
}

uint32_t acpi_get_lapic_address(void) {
    if (g_madt) {
        return g_madt->lapic_addr;
    }
    return 0xFEE00000;
}

uint32_t acpi_get_smi_cmd_port(void) {
    return g_fadt ? g_fadt->smi_cmd : 0;
}

uint8_t acpi_get_enable_cmd(void) {
    return g_fadt ? g_fadt->acpi_enable : 0;
}

uint32_t acpi_get_pm1a_cnt(void) {
    return g_fadt ? g_fadt->pm1a_cnt_blk : 0;
}

static uint16_t parse_aml_int(const uint8_t **p, const uint8_t *end) {
    if (*p >= end)
        return 0;
    uint8_t op = **p;
    (*p)++;
    if (op == 0x00)
        return 0;
    if (op == 0x01)
        return 1;
    if (op == 0xFF)
        return 0xFF;
    if (op == 0x0A) { /* BytePrefix */
        if (*p >= end)
            return 0;
        uint8_t val = **p;
        (*p)++;
        return val;
    }
    if (op == 0x0B) { /* WordPrefix */
        if (*p + 1 >= end)
            return 0;
        uint16_t val = (uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8);
        (*p) += 2;
        return val;
    }
    if (op == 0x0C) { /* DWordPrefix */
        if (*p + 3 >= end)
            return 0;
        uint32_t val = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) |
                       ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
        (*p) += 4;
        return (uint16_t)val;
    }
    return 0;
}

static void acpi_parse_s5(acpi_sdt_header_t *dsdt) {
    if (!dsdt || dsdt->length <= sizeof(acpi_sdt_header_t))
        return;

    const uint8_t *data = (const uint8_t *)dsdt + sizeof(acpi_sdt_header_t);
    size_t len = dsdt->length - sizeof(acpi_sdt_header_t);
    const uint8_t *end = data + len;

    for (size_t i = 0; i + 4 < len; i++) {
        if (data[i] == '_' && data[i + 1] == 'S' && data[i + 2] == '5' && data[i + 3] == '_') {
            bool valid = false;
            if (i >= 1 && data[i - 1] == 0x08) {
                valid = true;
            } else if (i >= 2 && data[i - 2] == 0x08 && data[i - 1] == '\\') {
                valid = true;
            }

            if (valid) {
                const uint8_t *ptr = &data[i + 4];
                if (ptr < end && *ptr == 0x12) { /* PackageOp */
                    ptr++;
                    if (ptr >= end)
                        break;
                    uint8_t lead = *ptr;
                    int pkg_bytes = (lead >> 6) & 0x03;
                    ptr += (1 + pkg_bytes);
                    if (ptr >= end)
                        break;

                    uint8_t num_elements = *ptr++;
                    if (num_elements >= 1) {
                        g_slp_typa = parse_aml_int(&ptr, end);
                    }
                    if (num_elements >= 2) {
                        g_slp_typb = parse_aml_int(&ptr, end);
                    }
                    g_s5_found = true;
                    klog_info("ACPI: Found \\_S5_ package in DSDT (SLP_TYPa: 0x%02x, SLP_TYPb: 0x%02x)",
                              g_slp_typa, g_slp_typb);
                    return;
                }
            }
        }
    }
    klog_warn("ACPI: \\_S5_ package not found in DSDT, using standard fallbacks");
}

void acpi_init(void) {
    if (!g_rsdp_request.response || !g_rsdp_request.response->address) {
        klog_warn("ACPI: Bootloader did not provide RSDP address");
        return;
    }

    uintptr_t rsdp_phys = (uintptr_t)g_rsdp_request.response->address;
    if (rsdp_phys >= g_hhdm_base) {
        rsdp_phys -= g_hhdm_base;
    }

    /* Map RSDP page */
    acpi_map_region(rsdp_phys, sizeof(acpi_xsdp_t));
    g_rsdp = (acpi_rsdp_t *)PHYS_TO_VIRT(rsdp_phys);

    /* Verify RSDP signature ("RSD PTR ") */
    if (memcmp(g_rsdp->signature, "RSD PTR ", 8) != 0) {
        klog_error("ACPI: Invalid RSDP signature!");
        return;
    }

    /* Check Revision: 0 = ACPI 1.0 (RSDT), 2 = ACPI 2.0+ (XSDT) */
    if (g_rsdp->revision >= 2) {
        acpi_xsdp_t *xsdp = (acpi_xsdp_t *)g_rsdp;
        if (xsdp->xsdt_addr) {
            uintptr_t xsdt_phys = (uintptr_t)xsdp->xsdt_addr;
            acpi_map_region(xsdt_phys, sizeof(acpi_sdt_header_t));
            acpi_sdt_header_t *hdr = (acpi_sdt_header_t *)PHYS_TO_VIRT(xsdt_phys);
            acpi_map_region(xsdt_phys, hdr->length);
            g_rsdt_or_xsdt = hdr;
            g_use_xsdt = true;
            klog_info("ACPI: ACPI 2.0+ XSDT found at phys 0x%016lx", (unsigned long)xsdt_phys);
        }
    }

    if (!g_rsdt_or_xsdt && g_rsdp->rsdt_addr) {
        uintptr_t rsdt_phys = (uintptr_t)g_rsdp->rsdt_addr;
        acpi_map_region(rsdt_phys, sizeof(acpi_sdt_header_t));
        acpi_sdt_header_t *hdr = (acpi_sdt_header_t *)PHYS_TO_VIRT(rsdt_phys);
        acpi_map_region(rsdt_phys, hdr->length);
        g_rsdt_or_xsdt = hdr;
        g_use_xsdt = false;
        klog_info("ACPI: ACPI 1.0 RSDT found at phys 0x%08x", g_rsdp->rsdt_addr);
    }

    if (!g_rsdt_or_xsdt) {
        klog_error("ACPI: Neither XSDT nor RSDT could be resolved!");
        return;
    }

    /* Find and cache FADT */
    g_fadt = (acpi_fadt_t *)acpi_find_table("FACP");
    if (g_fadt) {
        char oem[7] = {0};
        memcpy(oem, g_fadt->header.oem_id, 6);
        bool has_8042 = acpi_has_8042_controller();
        bool has_reset = (g_fadt->header.length >= 129) && ((g_fadt->flags & (1 << 10)) != 0);
        klog_info("ACPI: FADT (FACP) found (OEM: %s, 8042 PS/2: %s, Reset Reg: %s)", oem,
                  has_8042 ? "YES" : "NO", has_reset ? "YES" : "NO");

        /* Map and parse DSDT */
        uintptr_t dsdt_phys = g_fadt->dsdt;
        if (g_fadt->header.length >= 148 && g_fadt->header.revision >= 2 && g_fadt->x_dsdt) {
            dsdt_phys = (uintptr_t)g_fadt->x_dsdt;
        }

        if (dsdt_phys) {
            acpi_map_region(dsdt_phys, sizeof(acpi_sdt_header_t));
            acpi_sdt_header_t *dsdt = (acpi_sdt_header_t *)PHYS_TO_VIRT(dsdt_phys);
            if (memcmp(dsdt->signature, "DSDT", 4) == 0) {
                acpi_map_region(dsdt_phys, dsdt->length);
                klog_info("ACPI: DSDT found at phys 0x%016lx (Length: %u)",
                          (unsigned long)dsdt_phys, dsdt->length);
                acpi_parse_s5(dsdt);
            } else {
                klog_warn("ACPI: Invalid DSDT signature at phys 0x%016lx", (unsigned long)dsdt_phys);
            }
        }
    }

    /* Find and cache MADT (APIC) */
    g_madt = (acpi_madt_t *)acpi_find_table("APIC");
    if (g_madt) {
        klog_info("ACPI: MADT (APIC) found (LAPIC phys: 0x%08x)", g_madt->lapic_addr);
    }

    /* Find HPET */
    acpi_hpet_t *hpet = (acpi_hpet_t *)acpi_find_table("HPET");
    if (hpet) {
        klog_info("ACPI: HPET table found");
    }

    /* Find MCFG (PCIe) */
    acpi_mcfg_t *mcfg = (acpi_mcfg_t *)acpi_find_table("MCFG");
    if (mcfg) {
        klog_info("ACPI: MCFG PCIe MMCONFIG table found");
    }

    klog_info("ACPI: Hardware configuration parsed successfully!");
}

bool acpi_reboot(void) {
    if (!g_fadt)
        return false;

    /* Check length and RESET_REG_SUP flag (bit 10) */
    if (g_fadt->header.length < 129 || !(g_fadt->flags & (1 << 10))) {
        return false;
    }

    acpi_gas_t *reg = &g_fadt->reset_reg;
    uint8_t val = g_fadt->reset_value;

    klog_info("ACPI: Triggering ACPI hardware reset (Space: %u, Addr: 0x%016lx, Val: 0x%02x)",
              reg->address_space, (unsigned long)reg->address, val);

    if (reg->address_space == 1) {
        /* System I/O space */
        outb((uint16_t)reg->address, val);
        return true;
    } else if (reg->address_space == 0) {
        /* System Memory (MMIO) */
        uintptr_t phys = (uintptr_t)reg->address;
        acpi_map_region(phys, 4);
        volatile uint8_t *ptr = (volatile uint8_t *)PHYS_TO_VIRT(phys);
        *ptr = val;
        return true;
    } else if (reg->address_space == 2) {
        /* PCI Configuration Space */
        uint8_t bus = (reg->address >> 48) & 0xFF;
        uint8_t dev = (reg->address >> 32) & 0x1F;
        uint8_t func = (reg->address >> 16) & 0x07;
        uint8_t offset = reg->address & 0xFF;
        pci_write8(bus, dev, func, offset, val);
        return true;
    }

    return false;
}

bool acpi_poweroff(void) {
    if (!g_fadt)
        return false;

    uint32_t pm1a_cnt = g_fadt->pm1a_cnt_blk;
    uint32_t pm1b_cnt = g_fadt->pm1b_cnt_blk;

    if (!pm1a_cnt)
        return false;

    /* 1. Ensure ACPI mode is enabled via SMI command port if SCI_EN is not set */
    if ((inw((uint16_t)pm1a_cnt) & 1) == 0) {
        if (g_fadt->smi_cmd && g_fadt->acpi_enable) {
            klog_info("ACPI: Enabling ACPI mode (SMI port 0x%x <- 0x%x)...",
                      g_fadt->smi_cmd, g_fadt->acpi_enable);
            outb((uint16_t)g_fadt->smi_cmd, g_fadt->acpi_enable);
            for (int t = 0; t < 1000; t++) {
                if (inw((uint16_t)pm1a_cnt) & 1)
                    break;
                for (volatile int d = 0; d < 10000; d++);
            }
        }
    }

    /* 2. Determine sleep type values */
    uint16_t slp_a = g_s5_found ? g_slp_typa : 5;
    uint16_t slp_b = g_s5_found ? g_slp_typb : 5;

    klog_info("ACPI: S5 poweroff write to PM1a (port 0x%x, val 0x%x)...",
              pm1a_cnt, (uint16_t)((slp_a << 10) | (1 << 13)));

    /* 3. Send S5 sleep command (SLP_TYP in bits 10..12, SLP_EN in bit 13) */
    outw((uint16_t)pm1a_cnt, (uint16_t)((slp_a << 10) | (1 << 13)));
    if (pm1b_cnt) {
        outw((uint16_t)pm1b_cnt, (uint16_t)((slp_b << 10) | (1 << 13)));
    }

    for (volatile int d = 0; d < 5000000; d++);

    /* 4. Fallback alternative SLP_TYP values (7, 0) */
    outw((uint16_t)pm1a_cnt, (uint16_t)((7 << 10) | (1 << 13)));
    if (pm1b_cnt) {
        outw((uint16_t)pm1b_cnt, (uint16_t)((7 << 10) | (1 << 13)));
    }
    for (volatile int d = 0; d < 5000000; d++);

    outw((uint16_t)pm1a_cnt, (uint16_t)(1 << 13));
    if (pm1b_cnt) {
        outw((uint16_t)pm1b_cnt, (uint16_t)(1 << 13));
    }
    for (volatile int d = 0; d < 5000000; d++);

    return true;
}


/*
 * SzpontOS - ACPI (Advanced Configuration and Power Interface) Subsystem
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef SZPONTOS_DRIVERS_ACPI_H
#define SZPONTOS_DRIVERS_ACPI_H

#include <kernel/types.h>
#include <stdbool.h>

/* ACPI 1.0 RSDP structure */
typedef struct __attribute__((packed)) {
    char signature[8]; /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision; /* 0 for ACPI 1.0, 2 for ACPI 2.0+ */
    uint32_t rsdt_addr;
} acpi_rsdp_t;

/* ACPI 2.0+ Extended RSDP structure */
typedef struct __attribute__((packed)) {
    acpi_rsdp_t first_part;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_xsdp_t;

/* Standard ACPI SDT Header */
typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

/* ACPI Generic Address Structure (GAS) - 12 bytes */
typedef struct __attribute__((packed)) {
    uint8_t address_space; /* 0: System Memory (MMIO), 1: System I/O, 2: PCI Config Space */
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;   /* 0: Undefined, 1: Byte, 2: Word, 3: Dword, 4: Qword */
    uint64_t address;
} acpi_gas_t;

/* RSDT (Root System Description Table - 32-bit pointers) */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t tables[];
} acpi_rsdt_t;

/* XSDT (Extended System Description Table - 64-bit pointers) */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint64_t tables[];
} acpi_xsdt_t;

/* FADT (Fixed ACPI Description Table) */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_len;
    uint8_t gpe1_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;
    uint16_t ia_pc_boot_arch; /* Bit 1 = 8042 controller present */
    uint8_t reserved2;
    uint32_t flags;
    /* Reset register (ACPI 2.0+) */
    acpi_gas_t reset_reg;
    uint8_t reset_value;
    uint8_t reserved3[3];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
} acpi_fadt_t;

/* MADT (Multiple APIC Description Table) */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t lapic_addr;
    uint32_t flags;
    uint8_t entries[];
} acpi_madt_t;

/* HPET (High Precision Event Timer Table) */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t event_timer_block_id;
    uint8_t base_address[12];
    uint8_t hpet_number;
    uint16_t main_counter_minimum_clock_tick;
    uint8_t page_protection_and_oem_attribute;
} acpi_hpet_t;

/* MCFG (PCI Express Memory Mapped Configuration Table) */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint64_t reserved;
    struct {
        uint64_t base_addr;
        uint16_t pci_segment_group;
        uint8_t start_bus;
        uint8_t end_bus;
        uint32_t reserved;
    } entries[];
} acpi_mcfg_t;

/* ACPI Subsystem API */
void acpi_init(void);
acpi_sdt_header_t *acpi_find_table(const char *signature);
bool acpi_has_8042_controller(void);
uint32_t acpi_get_lapic_address(void);
uint32_t acpi_get_smi_cmd_port(void);
uint8_t acpi_get_enable_cmd(void);
uint32_t acpi_get_pm1a_cnt(void);
bool acpi_reboot(void);
bool acpi_poweroff(void);

#endif /* SZPONTOS_DRIVERS_ACPI_H */

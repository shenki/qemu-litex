/*
 *  QEMU model for the Litex board.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "hw/devices.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/block-backend.h"
#include "litex-hw.h"
#include "litex.h"
#include "exec/address-spaces.h"
#include "qemu/cutils.h"
#include "hw/char/serial.h"
#include "generated/csr.h"
#include "generated/mem.h"


#define BIOS_FILENAME    "bios.bin"

typedef struct {
    LM32CPU *cpu;
    hwaddr bootstrap_pc;
    hwaddr flash_base;
} ResetInfo;

static void cpu_irq_handler(void *opaque, int irq, int level)
{
    LM32CPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    if (level) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void main_cpu_reset(void *opaque)
{
    ResetInfo *reset_info = opaque;
    CPULM32State *env = &reset_info->cpu->env;

    cpu_reset(CPU(reset_info->cpu));

    /* init defaults */
    env->pc = reset_info->bootstrap_pc;
    env->eba = ROM_BASE;
    env->deba = ROM_BASE;
}

static void
litex_init(MachineState *machine)
{
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    
    LM32CPU *cpu;
    CPULM32State *env;

    int kernel_size;
    MemoryRegion *address_space_mem = get_system_memory();

    MemoryRegion *phys_rom = g_new(MemoryRegion, 1);
    MemoryRegion *phys_sram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_main_ram = g_new(MemoryRegion, 1);



    hwaddr rom_base   = ROM_BASE;
    hwaddr sram_base   = SRAM_BASE;
    hwaddr main_ram_base   = MAIN_RAM_BASE;
    
    size_t rom_size   = ROM_SIZE;
    size_t sram_size   = SRAM_SIZE;
    size_t main_ram_size   = MAIN_RAM_SIZE;

    
    qemu_irq irq[32];
    int i;
    char *bios_filename;
    ResetInfo *reset_info;

    reset_info = g_malloc0(sizeof(ResetInfo));

    if (cpu_model == NULL) {
        cpu_model = "lm32-full";
    }
    cpu = cpu_lm32_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "qemu: unable to find CPU '%s'\n", cpu_model);
        exit(1);
    }

    env = &cpu->env;
    reset_info->cpu = cpu;

    /** addresses from 0x80000000 to 0xFFFFFFFF are not shadowed */
    cpu_lm32_set_phys_msb_ignore(env, 1);

    memory_region_allocate_system_memory(phys_rom, NULL, "litex.rom", rom_size);
    memory_region_add_subregion(address_space_mem, rom_base, phys_rom);

    memory_region_allocate_system_memory(phys_sram, NULL, "litex.sram",    sram_size);
    memory_region_add_subregion(address_space_mem, sram_base, phys_sram);

    memory_region_allocate_system_memory(phys_main_ram, NULL, "litex.main_ram", main_ram_size);
    memory_region_add_subregion(address_space_mem, main_ram_base, phys_main_ram);

    
    /* create irq lines */
    env->pic_state = litex_pic_init(qemu_allocate_irq(cpu_irq_handler, cpu, 0));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(env->pic_state, i);
    }

    /* load bios rom */
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    bios_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    if (bios_filename) {
        load_image_targphys(bios_filename, ROM_BASE, ROM_SIZE);
    }
    reset_info->bootstrap_pc = ROM_BASE;


    /* if no kernel is given no valid bios rom is a fatal error */
    if (!kernel_filename  && !bios_filename && !qtest_enabled()) {
        fprintf(stderr, "qemu: could not load Milkymist One bios '%s'\n",
                bios_name);
        exit(1);
    }
    g_free(bios_filename);

   

    /* litex uart */
#ifdef CSR_UART_BASE
    litex_uart_create(CSR_UART_BASE & 0x7FFFFFFF, irq[0], serial_hds[0]);
#endif

    /* litex timer*/
#ifdef CSR_TIMER0_BASE
    litex_timer_create(CSR_TIMER0_BASE & 0x7FFFFFFF, irq[1], 80000000);
#endif
    
    /* INIT UART 16550 */
#ifndef CSR_UART_BASE
 #ifdef CSR_UART16550_BASE
    serial_mm_init(address_space_mem, CSR_UART16550_BASE & 0x7FFFFFFF, 2, irq[0],   115200, serial_hds[0], DEVICE_NATIVE_ENDIAN); */
 #endif
#endif
        
    /* make sure juart isn't the first chardev */
    env->juart_state = lm32_juart_init(serial_hds[1]);

    if (kernel_filename) {
        uint64_t entry;

        /* Boots a kernel elf binary.  */
        kernel_size = load_elf(kernel_filename, NULL, NULL, &entry, NULL, NULL, 1, EM_LATTICEMICO32, 0, 0);
        reset_info->bootstrap_pc = entry;

        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename, main_ram_base,   main_ram_size);
            reset_info->bootstrap_pc = main_ram_base;
        }

        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",    kernel_filename);
            exit(1);
        }
    }

    qemu_register_reset(main_cpu_reset, reset_info);
}

static void litex_machine_init(MachineClass *mc)
{
    mc->desc = "Litex One";
    mc->init = litex_init;
    mc->is_default = 0;
}

DEFINE_MACHINE("litex", litex_machine_init)

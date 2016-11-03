#ifndef QEMU_HW_LITEX_HW_H
#define QEMU_HW_LITEX_HW_H

#include "hw/qdev.h"
#include "net/net.h"

static inline DeviceState *litex_uart_create(hwaddr base,
                                             qemu_irq irq,
                                             CharDriverState *chr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "litex-uart");
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}



static inline DeviceState *litex_timer_create(hwaddr base, qemu_irq timer0_irq, uint32_t freq_hz)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "litex-timer");
    qdev_prop_set_uint32(dev, "frequency", freq_hz);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, timer0_irq);
    
    return dev;
}


#endif /* QEMU_HW_LITEX_HW_H */

#ifndef HW_LITEX_H
#define HW_LITEX_H

#include "hw/char/lm32_juart.h"

static inline DeviceState *litex_pic_init(qemu_irq cpu_irq)
{
    DeviceState *dev;
    SysBusDevice *d;

    dev = qdev_create(NULL, "lm32-pic");
    qdev_init_nofail(dev);
    d = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(d, 0, cpu_irq);

    return dev;
}

static inline DeviceState *lm32_juart_init(CharDriverState *chr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_LM32_JUART);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);

    return dev;
}


#endif

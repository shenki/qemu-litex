#ifndef QEMU_HW_LITEX_PIC_H
#define QEMU_HW_LITEX_PIC_H

#include "qemu-common.h"

uint32_t litex_pic_get_ip(DeviceState *d);
uint32_t litex_pic_get_im(DeviceState *d);
void litex_pic_set_ip(DeviceState *d, uint32_t ip);
void litex_pic_set_im(DeviceState *d, uint32_t im);

#endif /* QEMU_HW_LM32_PIC_H */

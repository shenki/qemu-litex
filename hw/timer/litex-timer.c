/*
 *  QEMU model of the Litex System Controller.
 *
 *  Copyright (c) 2016 Ramtin Amin <keytwo@gmail.com>
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
 *
 *
 * Specification available at:
 *   http://litex.walle.cc/socdoc/timer.pdf
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "qemu/error-report.h"

enum {
    R_TIMER_LOAD0 = 0,
    R_TIMER_LOAD1,
    R_TIMER_LOAD2,
    R_TIMER_LOAD3,
    
    R_TIMER_RELOAD0, 
    R_TIMER_RELOAD1, 
    R_TIMER_RELOAD2, 
    R_TIMER_RELOAD3, 

    R_TIMER_EN,
    R_TIMER_UPDATE_VALUE,

    R_TIMER_VALUE0,
    R_TIMER_VALUE1,
    R_TIMER_VALUE2,
    R_TIMER_VALUE3,
    
    R_TIMER_EV_STATUS,
    R_TIMER_EV_PENDING,
    R_TIMER_EV_ENABLE,
    R_MAX
};

#define TYPE_LITEX_TIMER "litex-timer"
#define LITEX_TIMER(obj) \
    OBJECT_CHECK(LitexTimerState, (obj), TYPE_LITEX_TIMER)

struct LitexTimerState {
    SysBusDevice parent_obj;

    MemoryRegion regs_region;

    QEMUBH *bh0;
    
    ptimer_state *ptimer0;

    char old_en;
    
    uint32_t freq_hz;

    uint32_t regs[R_MAX];

    qemu_irq gpio_irq;
    qemu_irq timer0_irq;
};

typedef struct LitexTimerState LitexTimerState;


static uint64_t timer_read(void *opaque, hwaddr addr,  unsigned size)
{
    LitexTimerState *s = opaque;
    uint32_t r = 0;
    
    addr >>= 2;
    switch (addr) {
    case R_TIMER_LOAD0:
    case R_TIMER_LOAD1:
    case R_TIMER_LOAD2:
    case R_TIMER_LOAD3:
    case R_TIMER_RELOAD0:
    case R_TIMER_RELOAD1:
    case R_TIMER_RELOAD2:
    case R_TIMER_RELOAD3:
    case R_TIMER_EN:
    case R_TIMER_UPDATE_VALUE:
    case R_TIMER_VALUE0:
    case R_TIMER_VALUE1:
    case R_TIMER_VALUE2:
    case R_TIMER_VALUE3:
    case R_TIMER_EV_STATUS:
    case R_TIMER_EV_PENDING:
    case R_TIMER_EV_ENABLE:
        r = s->regs[addr];
        break;

    default:
        error_report("litex_timer: read access to unknown register 0x"   TARGET_FMT_plx, addr << 2);
        break;
    }

    //printf("Read reg %lx val %016x\n", addr * 4, r);
    return r;
}

static void timer_write(void *opaque, hwaddr addr, uint64_t value,  unsigned size)
{
    LitexTimerState *s = opaque;
    unsigned int timval;
    
    value = value & 0xff;
    
    //printf("Write value %08x reg %08x\n", (unsigned int)value, (unsigned int)addr);
    addr >>= 2;

    switch (addr) {
    case R_TIMER_LOAD0:
    case R_TIMER_LOAD1:
    case R_TIMER_LOAD2:
    case R_TIMER_LOAD3:
    case R_TIMER_RELOAD0:
    case R_TIMER_RELOAD1:
    case R_TIMER_RELOAD2:
    case R_TIMER_RELOAD3:
        s->regs[addr] = value;
        break;
    case R_TIMER_EN:
        s->old_en = s->regs[addr];
        s->regs[addr] = value;

        if(!value)
        {
            ptimer_stop(s->ptimer0);
        } else {
            /* Make sure we were not at 1 before already */
            if(!s->old_en)
            {
                timval = (s->regs[R_TIMER_LOAD0] << 24) | \
                    (s->regs[R_TIMER_LOAD1] << 16) |      \
                    (s->regs[R_TIMER_LOAD2] << 8) |       \
                    (s->regs[R_TIMER_LOAD3]);
                ptimer_set_count(s->ptimer0,  timval);
                ptimer_run(s->ptimer0, 0);
            }
        }
        break;
    case R_TIMER_UPDATE_VALUE:
        timval = (uint32_t)ptimer_get_count(s->ptimer0) ;
        s->regs[R_TIMER_VALUE0] = timval >> 24;
        s->regs[R_TIMER_VALUE1] = (timval >> 16) & 0xff;
        s->regs[R_TIMER_VALUE2] = (timval >> 8 ) & 0xff;
        s->regs[R_TIMER_VALUE3] = timval & 0xff;
        break;
    case R_TIMER_EV_PENDING:
        if(value)
        {
            qemu_irq_lower(s->timer0_irq);
        }
    case R_TIMER_EV_ENABLE:
        s->regs[addr] = value;
        break;
        
    default:
        error_report("litex_timer: write access to unknown register 0x"  TARGET_FMT_plx, addr << 2);
        break;
    }
}

static const MemoryRegionOps timer_mmio_ops = {
    .read = timer_read,
    .write = timer_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void timer0_hit(void *opaque)
{
    LitexTimerState *s = opaque;
    uint32_t timval;
    
    //printf("Timer Hit!\n");
    ptimer_stop(s->ptimer0);
    
    if(s->regs[R_TIMER_EV_ENABLE])
    {
        qemu_irq_raise(s->timer0_irq);
    }
    
    if(s->regs[R_TIMER_EN])
    {        
        timval = (s->regs[R_TIMER_RELOAD0] << 24) | \
            (s->regs[R_TIMER_RELOAD1] << 16) |      \
            (s->regs[R_TIMER_RELOAD2] << 8) |       \
            (s->regs[R_TIMER_RELOAD3]);
        
        ptimer_set_count(s->ptimer0,  timval);
        if(timval)
            ptimer_run(s->ptimer0, 0);
        else
            ptimer_stop(s->ptimer0);
    } else {
        
        ptimer_stop(s->ptimer0);
    }
}


static void litex_timer_reset(DeviceState *d)
{
    LitexTimerState *s = LITEX_TIMER(d);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
    s->old_en = 0;
    ptimer_stop(s->ptimer0);

}

static void litex_timer_init(Object *obj)
{
    LitexTimerState *s = LITEX_TIMER(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(dev, &s->timer0_irq);
    s->bh0 = qemu_bh_new(timer0_hit, s);
    s->ptimer0 = ptimer_init(s->bh0, PTIMER_POLICY_DEFAULT);
    memory_region_init_io(&s->regs_region, obj, &timer_mmio_ops, s,  "litex-timer", R_MAX * 4);
    sysbus_init_mmio(dev, &s->regs_region);
}

static void litex_timer_realize(DeviceState *dev, Error **errp)
{
    LitexTimerState *s = LITEX_TIMER(dev);

    ptimer_set_freq(s->ptimer0, s->freq_hz);
}

static const VMStateDescription vmstate_litex_timer = {
    .name = "litex-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LitexTimerState, R_MAX),
        VMSTATE_PTIMER(ptimer0, LitexTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property litex_timer_properties[] = {
    DEFINE_PROP_UINT32("frequency", LitexTimerState,
    freq_hz, 80000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void litex_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = litex_timer_realize;
    dc->reset = litex_timer_reset;
    dc->vmsd = &vmstate_litex_timer;
    dc->props = litex_timer_properties;
}

static const TypeInfo litex_timer_info = {
    .name          = TYPE_LITEX_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LitexTimerState),
    .instance_init = litex_timer_init,
    .class_init    = litex_timer_class_init,
};

static void litex_timer_register_types(void)
{
    type_register_static(&litex_timer_info);
}

type_init(litex_timer_register_types)

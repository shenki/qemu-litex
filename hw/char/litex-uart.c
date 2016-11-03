/*
 *  QEMU model of the Litex UART block.
 *
 *  Copyright (c) 2016 Ramtin Amin
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
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "sysemu/char.h"
#include "qemu/error-report.h"


enum {
    CSR_UART_RXTX_ADDR,
    CSR_UART_TXFULL_ADDR, //4
    CSR_UART_RXEMPTY_ADDR, //8
    CSR_UART_EV_STATUS_ADDR, //c
    CSR_UART_EV_PENDING_ADDR, //10
    CSR_UART_EV_ENABLE_ADDR, //14
    CSR_UART_R_MAX
};

#define UART_EV_TX 1
#define UART_EV_RX 2
#define FIFO_DEPTH  64

#define TYPE_LITEX_UART "litex-uart"
#define LITEX_UART(obj) \
    OBJECT_CHECK(LitexUartState, (obj), TYPE_LITEX_UART)


struct char_fifo {
    uint8_t fifo[FIFO_DEPTH];
    uint32_t fifo_cnt;
    uint32_t fifo_wr_idx;
    uint32_t fifo_rd_idx;
    uint8_t fifo_empty;
    uint8_t fifo_full;
};

static void write_fifo(struct char_fifo *f, char c)
{
   
    if(!f->fifo_full)
    {
        f->fifo[f->fifo_wr_idx] = c;
        f->fifo_wr_idx = (f->fifo_wr_idx + 1) % FIFO_DEPTH;
        // printf("fifo_wr_idx:%d\n", f->fifo_wr_idx);
        f->fifo_empty = 0;
        if(f->fifo_wr_idx == f->fifo_rd_idx)
        {
            f->fifo_full = 1;
        }
        f->fifo_cnt++;
    } else {
        printf("can't write, fifo full %d %d!\n", f->fifo_wr_idx, f->fifo_rd_idx);
    }
}

static unsigned char read_fifo(struct char_fifo *f)
{
    return f->fifo[f->fifo_rd_idx];;
}

static void pop_fifo(struct char_fifo *f)
{
    if(f->fifo_cnt){
        f->fifo_rd_idx = (f->fifo_rd_idx + 1) % FIFO_DEPTH;
        //printf("fifo_rd_idx:%d\n", f->fifo_rd_idx);
        f->fifo_full = 0;
        if(f->fifo_rd_idx == f->fifo_wr_idx)
        {
            f->fifo_empty = 1;
        }
        f->fifo_cnt--;
    }
}

struct LitexUartState {
    SysBusDevice parent_obj;

    struct char_fifo rx_fifo;
    int irqstate;
    MemoryRegion regs_region;
    CharBackend chr;
    qemu_irq irq;

    uint32_t regs[CSR_UART_R_MAX];
};
typedef struct LitexUartState LitexUartState;


static uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{

    LitexUartState *s = opaque;
    uint32_t r = 0;

    addr = addr / 4;
    switch(addr)
    {
        
    case CSR_UART_RXTX_ADDR:
        r = read_fifo(&s->rx_fifo);
        break;
        
    case CSR_UART_TXFULL_ADDR:
    case CSR_UART_RXEMPTY_ADDR:
    case CSR_UART_EV_PENDING_ADDR:
    case CSR_UART_EV_STATUS_ADDR:
    case CSR_UART_EV_ENABLE_ADDR:
        r = s->regs[addr];
        break;
    default:
        printf("UNKONW ADDR\n");
    }
    
    
    //printf("Got uart read %08x val: %08x\n", (unsigned int)addr*4, r);
    trace_litex_uart_memory_read(addr*4 , r);
    return r;
}

static void uart_write(void *opaque, hwaddr addr, uint64_t value,    unsigned size)
{
    LitexUartState *s = opaque;
    unsigned char ch = value;

    addr = addr / 4;

    //printf("Got uart write %08x %02x %d\n", (unsigned int)addr*4,ch, size);

    switch(addr)
    {
    case CSR_UART_RXTX_ADDR:
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        break;

    case CSR_UART_EV_PENDING_ADDR:
        if(value & UART_EV_RX)
        {
            //printf("pop fifo !\n");
            pop_fifo(&s->rx_fifo);
            if(s->rx_fifo.fifo_empty)
            {
                s->regs[CSR_UART_RXEMPTY_ADDR] = 1;
            }
            s->regs[CSR_UART_EV_PENDING_ADDR] &= ~UART_EV_RX;    
        }
        if(value & UART_EV_TX)
        {
            s->regs[CSR_UART_EV_PENDING_ADDR] &= ~UART_EV_TX;
        }
        if(!s->regs[CSR_UART_EV_PENDING_ADDR])
        {
            if(s->irqstate)
            {
                qemu_irq_lower(s->irq);
                //  printf("irq lower\n");
                s->irqstate=0;
            }
        }
        break;


    case CSR_UART_EV_ENABLE_ADDR:
        
        s->regs[addr] = ch;
        break;


    default:
        printf("UNKONW ADDR\n");
    }
    trace_litex_uart_memory_write(addr, value);

    
}

static const MemoryRegionOps uart_mmio_ops = {
    .read = uart_read,
    .write = uart_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{

    LitexUartState *s = opaque;
    int i;
    for(i = 0; i < size; i++)
    {
        write_fifo(&s->rx_fifo, buf[i]);
    }
    
    s->regs[CSR_UART_RXEMPTY_ADDR] = 0;
    

    //printf("Got uart_rx %d\n", size);
    if(s->regs[CSR_UART_EV_ENABLE_ADDR] & UART_EV_RX)
    {

        s->regs[CSR_UART_EV_PENDING_ADDR] |= UART_EV_RX;
        if(!s->irqstate)
        {
            s->irqstate=1;
            //printf("raise irq\n");
            qemu_irq_raise(s->irq);
        }
        
    }
    
}

static int uart_can_rx(void *opaque)
{
    
    LitexUartState *s = opaque;
    
    //printf("got uart_can_rx %d %d\n",FIFO_DEPTH - s->rx_fifo.fifo_cnt, !s->rx_fifo.fifo_full);
    //return !s->rx_fifo.fifo_full;
    return FIFO_DEPTH - s->rx_fifo.fifo_cnt;
}

static void uart_event(void *opaque, int event)
{
    //printf("got ev\n");
}

static void litex_uart_reset(DeviceState *d)
{

    LitexUartState *s = LITEX_UART(d);
    int i;

    for (i = 0; i < CSR_UART_R_MAX; i++) {
        s->regs[i] = 0;
    }
    s->irqstate=0;
    memset((void*)&s->rx_fifo, 0, sizeof(s->rx_fifo));

    s->rx_fifo.fifo_empty = 1;
    
    //printf("litex uart reset\n");
       
}

static void litex_uart_realize(DeviceState *dev, Error **errp)
{

      LitexUartState *s = LITEX_UART(dev);
      qemu_chr_fe_set_handlers(&s->chr, uart_can_rx, uart_rx,  uart_event, s, NULL, true);

      //printf("litex uart realize\n");
}

static void litex_uart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    LitexUartState *s = LITEX_UART(obj);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->regs_region, OBJECT(s), &uart_mmio_ops, s, "litex-uart", CSR_UART_R_MAX * 4);
    sysbus_init_mmio(sbd, &s->regs_region);
}

static const VMStateDescription vmstate_litex_uart = {
    .name = "litex-uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LitexUartState, CSR_UART_R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property litex_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", LitexUartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void litex_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = litex_uart_realize;
    dc->reset = litex_uart_reset;
    dc->vmsd = &vmstate_litex_uart;
    dc->props = litex_uart_properties;
}

static const TypeInfo litex_uart_info = {
    .name          = TYPE_LITEX_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LitexUartState),
    .instance_init = litex_uart_init,
    .class_init    = litex_uart_class_init,
};

static void litex_uart_register_types(void)
{
    type_register_static(&litex_uart_info);
}

type_init(litex_uart_register_types)

/*
 * Raspbiec - Commodore 64 & 1541 serial bus handler for Raspberry Pi
 * Copyright (C) 2013 Antti Paarlahti <antti.paarlahti@outlook.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Kernel module framework adapted from:
 * Linux 2.6 and 3.0 'parrot' sample device driver
 * Copyright (c) 2011, Pete Batard <pete@akeo.ie>
 */

/*
 * The module can be installed on a running kernel like this:
 * -- remove previous version if necessary
 * $ sudo rmmod raspbiecdrv
 * -- install (with or without debug prints)
 *    <debuglevel> is an integer
 *    (0==no debug messges, 1=commands, 2==data, 3==statemachine)
 * $ sudo insmod raspbiecdrv.ko [debug=<debuglevel>]
 * -- set suitable device permissions
 * $ sudo chmod go+rw /dev/raspbiec
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include "raspbiecdrv.h"

/* Module information */
MODULE_AUTHOR("Antti Paarlahti <antti.paarlahti@outlook.com>");
MODULE_DESCRIPTION("Commodore IEC serial bus handler for Raspberry Pi");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

/* Device variables */
static struct class* raspbiec_class = NULL;
static struct device* raspbiec_device = NULL;
static int raspbiec_major;
/* A mutex will ensure that only one process accesses our device */
static DEFINE_MUTEX(raspbiec_device_mutex);

/* Use a Kernel FIFO */
static DECLARE_KFIFO(raspbiec_read_fifo, int16_t, RASPBIEC_READ_FIFO_SIZE);
static DECLARE_KFIFO(raspbiec_write_fifo, int16_t, RASPBIEC_WRITE_FIFO_SIZE);

/* Wait queues for blocking I/O */
DECLARE_WAIT_QUEUE_HEAD(readq);
DECLARE_WAIT_QUEUE_HEAD(writeq);

/* Module parameters that can be provided on insmod */
static int debug = 0;  /* ra debug info */
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug info level (default: 0)");

static const struct gpio gpios[] =
{
    /* GPIO,        flags,    label */
    { IEC_ATN_IN,   GPIOF_IN, "RASPBIEC ATN in" },
    { IEC_CLK_IN,   GPIOF_IN, "RASPBIEC CLOCK in" },
    { IEC_DATA_IN,  GPIOF_IN, "RASPBIEC DATA in" },
#ifdef INVERTED_OUTPUT
    /* Initial IEC bus configuration is lines at high voltage, */
    /* but the output is inverted by the IEC bus adapter       */
    { IEC_ATN_OUT,  GPIOF_OUT_INIT_LOW, "RASPBIEC ATN out" },
    { IEC_CLK_OUT,  GPIOF_OUT_INIT_LOW, "RASPBIEC CLOCK out" },
    { IEC_DATA_OUT, GPIOF_OUT_INIT_LOW, "RASPBIEC DATA out" },
#else
    /* Using noninverting bus adapter */
    { IEC_ATN_OUT,  GPIOF_OUT_INIT_HIGH, "RASPBIEC ATN out" },
    { IEC_CLK_OUT,  GPIOF_OUT_INIT_HIGH, "RASPBIEC CLOCK out" },
    { IEC_DATA_OUT, GPIOF_OUT_INIT_HIGH, "RASPBIEC DATA out" },
#endif
    /* Debug pins for logical analyzer (e.g. Panalyzer on another Pi) */
    /* DEBUG0 is high when servicing a GPIO interrupt */
    /* DEBUG1 is high when servicing a device write */
    /* DEBUG2 is high when servicing a tasklet */
    /* DEBUG3 is high when servicing a timer interrupt */
    { IEC_DEBUG0,   GPIOF_OUT_INIT_LOW, "RASPBIEC DEBUG0 out" },
    { IEC_DEBUG1,   GPIOF_OUT_INIT_LOW, "RASPBIEC DEBUG1 out" },
    { IEC_DEBUG2,   GPIOF_OUT_INIT_LOW, "RASPBIEC DEBUG2 out" },
    { IEC_DEBUG3,   GPIOF_OUT_INIT_LOW, "RASPBIEC DEBUG3 out" }
};

typedef struct
{
    int GPIO;
    int count;
} debugpin_t;

static debugpin_t debugpins[] =
{
    { IEC_DEBUG0, 0 },
    { IEC_DEBUG1, 0 },
    { IEC_DEBUG2, 0 },
    { IEC_DEBUG3, 0 }
};

static void set_debugpin(int pin, int value);

/* GPIO IRQ handler params */
static bool service_ints = false;
typedef struct irq_info
{
    unsigned int irq;
    unsigned long flags;
    int gpio;
    /* IEC_NONE = not waiting
     * IEC_LO   = wait value->0
     * IEC_HI   = wait value->1 */
    int waiting;
    bool checkmissed; /* true => check for a missed transition */
    int index;
} irq_info;

static int irqs_active;
static irq_info irqi[3]; /* ATN, CLK, DATA */

typedef struct bit_timing
{
    int data_hi;      /* A */
    int data_settle;  /* B */
    int data_valid;   /* C */
} bit_timing;
/*              ______        ___        ___        ___
 * clk  _______|      |______|   |______|   |______|   |_____...
 *                _________  :    ____  :    ____  :    ____
 * data _________|         |_:_C_| A  |_:_C_| A  |_:_C_| A  |...
 *                         :B:        :B:        :B:
 */
static bit_timing bit_timings[] =
{
    /* data_hi, data_settle, data_valid */
    { 50,  25,  25}, /* C64  */
    { 90,  25,  75}  /* 1541 */
};

enum device_type_index
{
    DEV_COMPUTER = 0,
    DEV_DRIVE    = 1,
};
static int device_type = DEV_COMPUTER;

/* For drive identity */
static int dev_num = 8;
static int dev_state = DEV_IDLE;

/*
 * State machine events / IRQ indexes
 */
#define FOREACH_EVENT(EVENT) \
    EVENT(iec_atn) \
    EVENT(iec_clk) \
    EVENT(iec_data) \
    EVENT(iec_timeout) \
    EVENT(iec_user) \
    EVENT(iec_tasklet) \
    EVENT(iec_no_event) \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

enum event_index
{
    FOREACH_EVENT(GENERATE_ENUM)
};

static const char *event_string[] =
{
    FOREACH_EVENT(GENERATE_STRING)
};

enum event_index_mask
{
    iec_atn_mask  = 1<<iec_atn,
    iec_clk_mask  = 1<<iec_clk,
    iec_data_mask = 1<<iec_data
};
static unsigned int event_wait_mask;

static bool under_atn;
static atomic_t irq_queue;
static int queued_event;
static int queued_value;
static int current_state;
static int16_t iec_status;

/* Error code state machine for conveying IEC errors to user.
 * - Return -EIO at the next possible user I/O operation
 * - Return error code at the next user read
 * - Wait until the previous error has been cleared
 *   before sending a new one
 */
enum error_notification_state
{
    iec_no_error,
    iec_return_eio,
    iec_send_error_code,
    iec_error_clearing_pending
};
static enum error_notification_state notify_error;

/*
 * State machine states (enum and string table)
 */
#define FOREACH_STATE(STATE) \
    STATE(IEC_IDLE) \
    STATE(IEC_WAIT_ATN_ASSERT) \
    STATE(IEC_WAIT_ATN_DEASSERT) \
    STATE(IEC_CHECK_ATN) \
    STATE(IEC_NEXT_CMD_BYTE) \
    STATE(IEC_INVALID) \
    STATE(IEC_RECEIVE_BYTE) \
    STATE(IEC_REMOTE_TALKER_READY_TO_SEND) \
    STATE(IEC_LISTENER_READY_FOR_DATA) \
    STATE(IEC_PROCESS_USER_DATA) \
    STATE(IEC_SEND_NEXT_BYTE) \
    STATE(IEC_SEND_BYTE) \
    STATE(IEC_REMOTE_LISTENER_READY_FOR_DATA) \
    STATE(IEC_REMOTE_LISTENER_DATA_ACCEPTED) \
    STATE(IEC_EOI_HANDSHAKE) \
    STATE(IEC_EOI_HANDSHAKE_END) \
    STATE(IEC_EOI_ATN_ASSERTED) \
    STATE(IEC_SEND_COMMAND) \
    STATE(IEC_RESET) \
    STATE(IEC_ERROR) \

enum machine_state
{
    FOREACH_STATE(GENERATE_ENUM)
};

static const char *machine_state_string[] =
{
    FOREACH_STATE(GENERATE_STRING)
};

static inline void dbg_state(int state)
{
    if (state >= 0 && state < ARRAY_SIZE(machine_state_string))
        msg(3,"raspbiec: %s\n",machine_state_string[state]);
    else
        msg(3,"raspbiec: Illegal state %d\n",state);
}

enum EOI_State
{
    iec_no_EOI,
    iec_send_EOI,
    iec_EOI_sent,
    iec_EOI_received
};
static enum EOI_State EOI_state;
static int iec_bit;
static int16_t iec_byte;

/* Not all output data was sent to bus, listener asserted ATN mid-transmission.
 * Needs to be a separate flag as the number of written bytes must be conveyed
 * to the user side and the bus state can change to anything in the meantime.
 */
static bool talk_interrupted;

/* For printing hex numbers with a sign in front of absolute value */
/* Use format "%c0x%02X" */
#define ABSHEX(val) ((val)<0)?'-':' ',((val)<0)?-(val):(val)

/* High resolution timer */
static struct hrtimer raspbiec_timer_timeout;
static enum hrtimer_restart raspbiec_timeout_callback(struct hrtimer *timer);

/* Arbitrary user data given to iec_set_timeout() and passed to
 * the state machine when the timeout event triggers */
static int timeout_event_value;

/* Tasklet to reschedule transmission to IEC bus */
static struct tasklet_struct raspbiec_tasklet;
static void raspbiec_tasklet_callback(unsigned long);

static inline int iec_get_data(void)
{
    return gpio_get_value(IEC_DATA_IN);
}

static inline int iec_get_clk(void)
{
    return gpio_get_value(IEC_CLK_IN);
}

static inline int iec_get_atn(void)
{
    return gpio_get_value(IEC_ATN_IN);
}

/*-------------------------------------------------------------------*/
static int raspbiec_device_open(struct inode* inode, struct file* filp)
/*-------------------------------------------------------------------*/
{
    info("device opened\n");

    if (!mutex_trylock(&raspbiec_device_mutex))
    {
        warn("another process is accessing the device\n");
        return -EBUSY;
    }

    /* Clear any errors upon opening */
    kfifo_reset(&raspbiec_read_fifo);
    kfifo_reset(&raspbiec_write_fifo);
    device_type = DEV_COMPUTER;
    current_state = IEC_RESET;
    raspbiec_state_machine(iec_user,-1);

    if (!iec_bus_is_idle())
    {
        warn("IEC bus is not in idle state\n");
    }

    return 0;
}

/*-------------------------------------------------------------------*/
static int raspbiec_device_release(struct inode* inode, struct file* filp)
/*-------------------------------------------------------------------*/
{
    device_type = DEV_COMPUTER;
    current_state = IEC_RESET;
    raspbiec_state_machine(iec_user,-1);
    mutex_unlock(&raspbiec_device_mutex);
    info("device closed\n");
    return 0;
}

/*-------------------------------------------------------------------*/
static ssize_t raspbiec_device_read(struct file* filp,
                                    char __user *buffer,
                                    size_t length,
                                    loff_t* offset)
/*-------------------------------------------------------------------*/
{
    int fifoerr;
    unsigned int copied;

    while (kfifo_is_empty(&raspbiec_read_fifo))
    {
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        msg(3,"raspbiec: read blocked   ------\n");
        if (wait_event_interruptible(readq, (!kfifo_is_empty(&raspbiec_read_fifo))))
        {
            msg(3,"raspbiec: read signaled  ++++++\n");
            return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        }
        msg(3,"raspbiec: read unblocked ++++++\n");
    }

    if (notify_error == iec_return_eio)
    {
        msg(3,"raspbiec: read return EIO\n");
        notify_error = iec_send_error_code;
        return -EIO;
    }

    fifoerr = kfifo_to_user(&raspbiec_read_fifo,
                            buffer,
                            length,
                            &copied);
    /* Ignore short reads (but warn about them) */
    if (length > copied)
    {
        msg(1,"short read detected\n");
    }

    if (notify_error == iec_send_error_code && copied >= 1)
    {
        msg(3,"raspbiec: read return error code\n");
        if (iec_status != IEC_OK)
            notify_error = iec_error_clearing_pending;
        else
            notify_error = iec_no_error;
    }

    talk_interrupted = false;

    return fifoerr ? fifoerr : copied;
}

/*-------------------------------------------------------------------*/
static ssize_t raspbiec_device_write(struct file* filp,
                                     const char __user *buffer,
                                     size_t length,
                                     loff_t* offset)
/*-------------------------------------------------------------------*/
{
    int err;
    unsigned int copied;
    unsigned int datalen;
    unsigned int sent;

    if (notify_error == iec_return_eio)
    {
        msg(3,"raspbiec: write return EIO\n");
        notify_error = iec_send_error_code;
        return -EIO;
    }

    if (talk_interrupted)
    {
        msg(3,"raspbiec: write talk_interrupted\n");
        kfifo_reset(&raspbiec_write_fifo);
        return 0;
        /* When talk_interrupted is true it means ATN was asserted
         * during sending. Do a read to acknowledge.
         */
    }

    while (kfifo_is_full(&raspbiec_write_fifo))
    {
        if (filp->f_flags & O_NONBLOCK)
        {
            return -EAGAIN;
        }
        msg(3,"raspbiec: write blocked   ------\n");
        if (wait_event_interruptible(writeq,
                                     !kfifo_is_full(&raspbiec_write_fifo)))
        {
            msg(3,"raspbiec: write signaled  ++++++\n");
            return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        }
        msg(3,"raspbiec: write unblocked ++++++\n");
    }

    err = kfifo_from_user(&raspbiec_write_fifo,
                          buffer,
                          length,
                          &copied);
    /* Ignore short writes (but warn about them) */
    if (length > copied)
    {
        msg(1,"short write detected\n");
    }
    datalen = kfifo_len(&raspbiec_write_fifo);

    /* Check if the state machine should be triggered manually.
     * It runs automatically as long as it
     * - has data to process in the output fifo
     * - is waiting for an interrupt or a timer event
     */
    if (!kfifo_is_empty(&raspbiec_write_fifo) &&
            (current_state == IEC_PROCESS_USER_DATA ||
             current_state == IEC_CHECK_ATN))
    {
        set_debugpin(1, 1);
        raspbiec_state_machine(iec_user,-1);
        set_debugpin(1, 0);
    }

    /* Block until either the whole buffer has been sent or
     * TALK has been interrupted. In the latter case the
     * rest of the write data is discarded and the amount
     * of the data actually sent is returned.
     */
    wait_event_interruptible(writeq, kfifo_is_empty(&raspbiec_write_fifo) ||
                             talk_interrupted);

    sent = datalen - kfifo_len(&raspbiec_write_fifo);

    if (talk_interrupted)
    {
        msg(3,"raspbiec: write talk_interrupted\n");
        kfifo_reset(&raspbiec_write_fifo);
        talk_interrupted = false;
    }

    return (err != 0) ? err : sent;
}

static struct file_operations fops =
{
    .read    = raspbiec_device_read,
    .write   = raspbiec_device_write,
    .open    = raspbiec_device_open,
    .release = raspbiec_device_release
};

/*-------------------------------------------------------------------*/
static ssize_t sys_state(struct device *dev,
                         struct device_attribute *attr,
                         char *buf)
/*-------------------------------------------------------------------*/
{
    int count = 1;
    count += scnprintf(buf+count-1, PAGE_SIZE-count, "%d\n", current_state);
    return count;
}

/* Declare the sysfs entries */
static DEVICE_ATTR(state, S_IRUSR|S_IRGRP|S_IROTH, sys_state, NULL);

/* Read the current state machine state number from
 * /sys/devices/virtual/raspbiec/raspbiec/state
 */

/*-------------------------------------------------------------------*/
/* Module initialization and release */
static int __init raspbiec_module_init(void)
/*-------------------------------------------------------------------*/
{
    int retval;
    int irqs = 0;

    info("module loaded\n");
    raspbiec_major = register_chrdev(0, DEVICE_NAME, &fops);
    if (raspbiec_major < 0)
    {
        err("failed to register device: error %d\n", raspbiec_major);
        retval = raspbiec_major;
        goto failed_chrdevreg;
    }

    raspbiec_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(raspbiec_class))
    {
        err("failed to register device class '%s'\n", CLASS_NAME);
        retval = PTR_ERR(raspbiec_class);
        goto failed_classreg;
    }

    raspbiec_device = device_create(raspbiec_class,
                                    NULL,
                                    MKDEV(raspbiec_major, 0),
                                    NULL,
                                    CLASS_NAME);
    if (IS_ERR(raspbiec_device))
    {
        err("failed to create device '%s'\n", CLASS_NAME);
        retval = PTR_ERR(raspbiec_device);
        goto failed_devreg;
    }

    retval = device_create_file(raspbiec_device, &dev_attr_state);
    if (retval < 0)
    {
        warn("failed to create state /sys endpoint - continuing without\n");
    }

    mutex_init(&raspbiec_device_mutex);

    INIT_KFIFO(raspbiec_read_fifo);
    INIT_KFIFO(raspbiec_write_fifo);

    /* Init GPIOs */
    if (gpio_request_array(gpios, ARRAY_SIZE(gpios)))
    {
        err("trouble requesting GPIOs");
        goto failed_gpioreq;
    }

    irqi[iec_atn].gpio  = IEC_ATN_IN;
    ++irqs;
    irqi[iec_clk].gpio  = IEC_CLK_IN;
    ++irqs;
    irqi[iec_data].gpio = IEC_DATA_IN;
    ++irqs;

    for (irqs_active = 0; irqs_active < irqs; ++irqs_active)
    {
        irqi[irqs_active].irq = gpio_to_irq(irqi[irqs_active].gpio);
        irqi[irqs_active].flags  = IRQF_TRIGGER_RISING
                                   | IRQF_TRIGGER_FALLING
                                   | IRQF_ONESHOT;
        if ( request_irq(irqi[irqs_active].irq,
                         gpio_interrupt,
                         irqi[irqs_active].flags,
                         "raspbiec_gpio",
                         &irqi[irqs_active]) )
        {
            printk(KERN_ERR "raspbiec: trouble requesting IRQ %d",
                   irqi[irqs_active].irq);
            goto failed_irqreq;
        }

        irqi[irqs_active].waiting = IEC_NONE;
        irqi[irqs_active].checkmissed = false;
        irqi[irqs_active].index = irqs_active;
    }

    hrtimer_init(&raspbiec_timer_timeout, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    raspbiec_timer_timeout.function = &raspbiec_timeout_callback;
    tasklet_init(&raspbiec_tasklet, raspbiec_tasklet_callback, 0);

    current_state = IEC_RESET;
    raspbiec_state_machine(iec_user,-1);
    atomic_set(&irq_queue, 0);
    queued_event = 0;
    queued_value = 0;
    under_atn = false;
    notify_error = iec_no_error;
    talk_interrupted = false;

    event_wait_mask = 0;
    service_ints = true;

    return 0;

failed_irqreq:
    for ( --irqs_active; irqs_active >= 0; --irqs_active)
    {
        free_irq(irqi[irqs_active].irq, &irqi[irqs_active]);
    }
    gpio_free_array(gpios, ARRAY_SIZE(gpios));

failed_gpioreq:
    device_destroy(raspbiec_class, MKDEV(raspbiec_major, 0));
failed_devreg:
    class_unregister(raspbiec_class);
    class_destroy(raspbiec_class);
failed_classreg:
    unregister_chrdev(raspbiec_major, DEVICE_NAME);
failed_chrdevreg:
    return -1;
}

/*-------------------------------------------------------------------*/
static void __exit raspbiec_module_exit(void)
/*-------------------------------------------------------------------*/
{
    info("module unloaded\n");
    service_ints = false;
    tasklet_kill(&raspbiec_tasklet);
    hrtimer_cancel(&raspbiec_timer_timeout);
    for ( --irqs_active; irqs_active >= 0; --irqs_active)
    {
        free_irq(irqi[irqs_active].irq, &irqi[irqs_active]);
    }
    gpio_free_array(gpios, ARRAY_SIZE(gpios));
    device_remove_file(raspbiec_device, &dev_attr_state);
    device_destroy(raspbiec_class, MKDEV(raspbiec_major, 0));
    class_unregister(raspbiec_class);
    class_destroy(raspbiec_class);
    unregister_chrdev(raspbiec_major, DEVICE_NAME);
}

module_init(raspbiec_module_init);
module_exit(raspbiec_module_exit);

/*-------------------------------------------------------------------*/
static int gpio_checkwait(irq_info *pirqi, int checkmissed)
/*-------------------------------------------------------------------*/
{
    int i;
    int current_value;
    int state_machine_call_needed = 0;

    current_value = gpio_get_value(pirqi->gpio);

    if (pirqi->waiting == current_value)
    {
        /* This pin and pin value was waited on. */
        /* Reset values so that the wait can be
         * reused inside the state machine. */
        pirqi->waiting = IEC_NONE;
        pirqi->checkmissed = false;
        event_wait_mask = 0;
        if (checkmissed)
        {
            raspbiec_state_machine(pirqi->index, current_value);
        }
        else
        {
            state_machine_call_needed = 1;
        }
    }
    else if (checkmissed && pirqi->checkmissed)
    {
        /* This pin was waited on, but the value is opposite of
        * what was expected. Return the expected value */
        msg(1,"raspbiec: opposite event %d,%d", pirqi->index, pirqi->waiting);
        pirqi->waiting = IEC_NONE;
        pirqi->checkmissed = false;
        event_wait_mask = 0;
        raspbiec_state_machine(pirqi->index, !current_value);
    }
    else if (checkmissed && event_wait_mask != 0)
    {
        /* Check if an earlier event was missed.
         * In most cases it is possible to catch up
         * one missed event without errors because
         * Pi is so much faster than a C64 or 1541.
         */
        for (i=0; i < ARRAY_SIZE(irqi); ++i)
        {
            current_value = gpio_get_value(irqi[i].gpio);

            if ((event_wait_mask & (1<<i)) &&
                    (irqi[i].waiting == current_value))
            {
                msg(1,"raspbiec: late event %d,%d", i, current_value);
                irqi[i].waiting = IEC_NONE;
                irqi[i].checkmissed = false;
                event_wait_mask = 0;
                raspbiec_state_machine(i, current_value);
            }
        }
    }

    return state_machine_call_needed;
}

/*-------------------------------------------------------------------*/
static irqreturn_t gpio_interrupt(int irq, void* dev_id)
/*-------------------------------------------------------------------*/
{
    irq_info *pirqi = dev_id;

    if (service_ints)
    {
        set_debugpin(0, 1);
        gpio_checkwait(pirqi, 1);
        set_debugpin(0, 0);
    }

    return IRQ_HANDLED;
}

/*-------------------------------------------------------------------*/
static void iec_set_timeout(int usecs, int value)
/*-------------------------------------------------------------------*/
{
    ktime_t ktime;
    timeout_event_value = value;
    ktime = ktime_set( 0, usecs*1000 );
    hrtimer_start( &raspbiec_timer_timeout, ktime, HRTIMER_MODE_REL );
}

/*-------------------------------------------------------------------*/
static void iec_cancel_timeout(void)
/*-------------------------------------------------------------------*/
{
    hrtimer_cancel(&raspbiec_timer_timeout);
}

/*-------------------------------------------------------------------*/
static enum hrtimer_restart raspbiec_timeout_callback(struct hrtimer *timer)
/*-------------------------------------------------------------------*/
{
    set_debugpin(3, 1);
    raspbiec_state_machine(iec_timeout, timeout_event_value);
    set_debugpin(3, 0);
    return HRTIMER_NORESTART;
}

/*-------------------------------------------------------------------*/
static void raspbiec_tasklet_callback(unsigned long data)
/*-------------------------------------------------------------------*/
{
    set_debugpin(2, 1);
    raspbiec_state_machine(iec_tasklet, data);
    set_debugpin(2, 0);
}

/*-------------------------------------------------------------------*/
static void raspbiec_state_machine(int event, int value)
/*-------------------------------------------------------------------*/
{
    int state;
    int queue_len;
    bool wait_for_event;
    unsigned long flags;

    msg(3,"raspbiec: event %d (%s) = %d",event, event_string[event], value);

    queue_len = atomic_inc_return(&irq_queue);
    if (queue_len > 2)
    {
        warn("state machine IRQ queue full! (event %d, value %d)", event, value);
        atomic_dec(&irq_queue);
        return;
    }

    if (queue_len > 1)
    {
        queued_event = event;
        queued_value = value;
        msg(3,"raspbiec: event queued");
        return;
    }

    local_irq_save(flags);
    state = current_state;
eventloop:
    /*****************************
     * Main state loop
     *****************************/
    do
    {
        dbg_state(state);
        wait_for_event = raspbiec_state_selector(&state, event, value);
        event = iec_no_event; /* The outside event is given only once */
    }
    while (!wait_for_event);

    queue_len = atomic_dec_return(&irq_queue);
    if (queue_len > 0)
    {
        event = queued_event;
        value = queued_value;
        msg(3,"raspbiec: queued event %d (%s) = %d", event, event_string[event], value);
        goto eventloop;
    }
    current_state = state;
    local_irq_restore(flags);
}

/*-------------------------------------------------------------------*/
static bool raspbiec_state_selector(int* state, int event, int value)
/*-------------------------------------------------------------------*/
{
    int next_state = IEC_ERROR; /* To catch any missing state setting */
    int16_t tmpbyte;
    bool iec_biterror = false;
    bool wait = false;

    /*
     * See ROM listings at e.g.
     * http://www.pagetable.com/c64rom/c64rom_en.html
     * http://www.ffd2.com/fridge/docs/1541dis.html
     */
    switch(*state)
    {
    case IEC_IDLE:
        if (device_type == DEV_COMPUTER ||
                (dev_state == DEV_TALK &&
                 EOI_state != iec_EOI_sent))
        {
            next_state = IEC_PROCESS_USER_DATA;
            wait = true;
            msg(1,"raspbiec IEC_PROCESS_USER_DATA\n");
        }
        else
        {
            next_state = IEC_WAIT_ATN_ASSERT;
            msg(1,"raspbiec IEC_WAIT_ATN_ASSERT\n");
        }
        break;

    case IEC_WAIT_ATN_ASSERT:
        next_state = IEC_CHECK_ATN;
        wait = iec_wait_atn(IEC_LO, true);
        iec_set_timeout(500, IEC_LO);
        break;

    case IEC_WAIT_ATN_DEASSERT:
        next_state = IEC_CHECK_ATN;
        wait = iec_wait_atn(IEC_HI, true);
        iec_set_timeout(500,IEC_HI);
        break;

    case IEC_CHECK_ATN:
        if (iec_timeout == event)
        {
            /* Non-overclocked Pi may miss the ATN assert
             * after filename. Use timeout to check it. */
            if (value == iec_get_atn())
            {
                /* ATN change has slipped by, synthesize event */
                msg(1,"raspbiec: ATN timeout %d\n",value);
                iec_wait_atn_cancel();
                event = iec_atn;
            }
            else /* Continue waiting for GPIO normally */
            {
                event = iec_no_event;
                next_state = IEC_CHECK_ATN;
                wait = true;
                break;
            }
        }
        else if (iec_user == event)
        {
            /* Device is being written to while waiting for ATN */
            next_state = IEC_PROCESS_USER_DATA;
            break;
        }
        else
        {
            iec_cancel_timeout();
        }
    case IEC_NEXT_CMD_BYTE:
        /* Check the event if it exists rather than the current value,
         * because a missed ATN edge might have been synthesized */
        if ((iec_atn      == event && IEC_LO == value) ||
            (iec_no_event == event && IEC_LO == iec_get_atn()))
        {
            /* ATN asserted */
            if (!under_atn)
            {
                tmpbyte = IEC_ASSERT_ATN;
                msg(1,"raspbiec <- %c0x%02X\n",ABSHEX(tmpbyte));
                kfifo_in(&raspbiec_read_fifo, &tmpbyte, 1);
                wake_up_interruptible(&readq);
                under_atn = true;
                if (notify_error == iec_error_clearing_pending)
                {
                    notify_error = iec_no_error;
                }
                iec_status = IEC_OK; /* Clear errors upon receiving ATN */
            }
            iec_set_clk(IEC_HI);
            iec_set_data(IEC_LO);
            EOI_state = iec_no_EOI;
            next_state = IEC_RECEIVE_BYTE;
            wait = iec_wait_clk(IEC_LO);
        }
        else /* ATN deasserted */
        {
            if (under_atn)
            {
                tmpbyte = IEC_DEASSERT_ATN;
                msg(1,"raspbiec <- %c0x%02X\n",ABSHEX(tmpbyte));
                kfifo_in(&raspbiec_read_fifo, &tmpbyte, 1);
                wake_up_interruptible(&readq);
                under_atn = false;
            }

            if (DEV_LISTEN == dev_state)
            {
                next_state = IEC_RECEIVE_BYTE;
            }
            else if (DEV_TALK == dev_state)
            {
                iec_set_data(IEC_HI); /* Talk-attention-turnaround */
                iec_set_clk(IEC_LO);
                udelay(80);           /* Tda (Talk-attention ack. hold) */
                EOI_state = iec_no_EOI;
                next_state = IEC_SEND_NEXT_BYTE;
            }
            else /* DEV_IDLE */
            {
                iec_release_bus();
                next_state = IEC_IDLE;
            }
        }
        event = iec_no_event; /* Input event has been handled */
        break;

    /*-------------------------------------------------------------------*/
    case IEC_RECEIVE_BYTE:
        EOI_state = iec_no_EOI;
        iec_bit = 8;
        iec_byte = 0;
        iec_biterror = false;
        iec_set_clk(IEC_HI);  /* Release out own clock */
        next_state = IEC_REMOTE_TALKER_READY_TO_SEND;
        wait = iec_wait_clk(IEC_HI);
        break;

    case IEC_REMOTE_TALKER_READY_TO_SEND:
        /* In listen case the next byte might be a data byte after open
         * or command byte after close. Differentiate by checking ATN */
        if (!under_atn &&
                IEC_LO == iec_get_atn())
        {
            tmpbyte = IEC_ASSERT_ATN;
            msg(1,"raspbiec <- %c0x%02X\n",ABSHEX(tmpbyte));
            kfifo_in(&raspbiec_read_fifo, &tmpbyte, 1);
            wake_up_interruptible(&readq);
            under_atn = true;
            if (notify_error == iec_error_clearing_pending)
            {
                notify_error = iec_no_error;
            }
            iec_status = IEC_OK; /* Clear errors upon receiving ATN */
        }
        iec_set_data(IEC_HI); /* Listener ready-for_data */
        if (iec_wait_data_busy(IEC_HI, 100))
        {
            /* Another very slow listener on the bus, exit busywait */
            next_state = IEC_LISTENER_READY_FOR_DATA;
            wait = iec_wait_data(IEC_HI);
            break;
        }
    case IEC_LISTENER_READY_FOR_DATA:
        if (iec_wait_clk_busy(IEC_LO, 250))
        {
            if (EOI_state == iec_no_EOI) /* 1st timeout is EOI */
            {
                iec_set_data(IEC_LO);  /* EOI timeout handshake */
                EOI_state = iec_EOI_received;
                udelay(60);           /* Tei (EOI response hold time) */
                next_state = IEC_LISTENER_READY_FOR_DATA;
                iec_set_data(IEC_HI); /* Listener ready-for_data */
            }
            else /* 2nd timeout is error */
            {
                iec_release_bus();
                next_state = IEC_ERROR;
                iec_status = IEC_READ_TIMEOUT;
            }
            break;
        }

        while (iec_bit > 0)
        {
            iec_biterror |= iec_wait_clk_busy(IEC_HI, 1000);
            iec_byte >>= 1;
            iec_byte |= iec_get_data() << 7;
            --iec_bit;
            iec_biterror |= iec_wait_clk_busy(IEC_LO, 1000);
        }

        udelay(40);            /* Tf (Frame handshake) */
        iec_set_data(IEC_LO);  /* Listener data-accepted */

        /* Return ATN command bytes as negative values */
        if (under_atn)
        {
            tmpbyte = -iec_byte;
            msg(1,"raspbiec <- %c0x%02X\n",ABSHEX(tmpbyte));
        }
        else
        {
            tmpbyte = iec_byte;
            msg(2,"raspbiec <- %c0x%02X\n",ABSHEX(tmpbyte));
        }
        kfifo_in(&raspbiec_read_fifo, &tmpbyte, 1);
        wake_up_interruptible(&readq);
        if (iec_biterror)
        {
            warn("Reception bit error!\n");
            tmpbyte = IEC_PREV_BYTE_HAS_ERROR;
            kfifo_in(&raspbiec_read_fifo, &tmpbyte, 1);
            wake_up_interruptible(&readq);
        }

        if (under_atn) /* Receiving a command */
        {
            if (CMD_UNLISTEN == iec_byte ||
                    CMD_UNTALK == iec_byte)
            {
                dev_state = DEV_IDLE;
                next_state = IEC_WAIT_ATN_DEASSERT;
            }
            else if (CMD_TALK(dev_num) == iec_byte)
            {
                dev_state = DEV_TALK;
                next_state = IEC_NEXT_CMD_BYTE;
            }
            else if (CMD_LISTEN(dev_num) == iec_byte)
            {
                dev_state = DEV_LISTEN;
                next_state = IEC_NEXT_CMD_BYTE;
            }
            else if (CMD_IS_DATA_CLOSE_OPEN(iec_byte))
            {
                /* DATA, CLOSE and OPEN handled on user side */
                next_state = IEC_WAIT_ATN_DEASSERT;
            }
            else /* Command was not for this device */
            {
                iec_release_bus();
                dev_state = DEV_IDLE;
                next_state = IEC_WAIT_ATN_DEASSERT;
            }
            break;
        }

        if (EOI_state == iec_EOI_received)
        {
            iec_byte = IEC_EOI;
            msg(1,"raspbiec <- %c0x%02X\n",ABSHEX(iec_byte));
            kfifo_in(&raspbiec_read_fifo, &iec_byte, 1);
            wake_up_interruptible(&readq);
            udelay(60); /* Tfr (EOI acknowledge) */
            iec_release_bus();
            next_state = IEC_PROCESS_USER_DATA;
        }
        else
        {
            next_state = IEC_RECEIVE_BYTE;
        }
        break;

    /*-------------------------------------------------------------------*/
    case IEC_PROCESS_USER_DATA:
    case IEC_SEND_NEXT_BYTE:
        if (DEV_COMPUTER != device_type &&
            IEC_LO == iec_get_atn())
        {
            next_state = IEC_EOI_ATN_ASSERTED;
            break;
        }

        if (0 == kfifo_out(&raspbiec_write_fifo, &iec_byte, 1))
        {
            /* No data available */
            wake_up_interruptible(&writeq);
            next_state = IEC_IDLE;
            break;
        }

        /* A byte has been read and the fifo can accept more data */
        wake_up_interruptible(&writeq);

        if (iec_byte < 0)
        {
            msg(1,"raspbiec -> %c0x%02X %s\n", ABSHEX(iec_byte),
                (iec_status == IEC_OK) ? "" : "(status!=OK)");
            next_state = IEC_SEND_COMMAND;
            break;
        }
        if (iec_status != IEC_OK)
        {
            /* Wait for a command to clear error */
            msg(2,"raspbiec -> %c0x%02X (discarded)\n",ABSHEX(iec_byte));
            next_state = IEC_PROCESS_USER_DATA;
            break;
        }
        msg(2,"raspbiec -> %c0x%02X\n",ABSHEX(iec_byte));
    case IEC_SEND_BYTE:
        iec_set_data(IEC_HI);
        if (IEC_HI == iec_get_data())
        {
            iec_idle_state();
            iec_status = IEC_DEVICE_NOT_PRESENT;
            next_state = IEC_ERROR;
            break;
        }
        iec_set_clk(IEC_HI); /* Talker ready-to-send */
        if (EOI_state == iec_send_EOI)
        {
            next_state = IEC_EOI_HANDSHAKE;
        }
        else
        {
            next_state = IEC_REMOTE_LISTENER_READY_FOR_DATA;
        }
        if (iec_wait_data_atn_busy(IEC_HI, 400)) // Busywait longer than EOI time
        {
            /* Slow remote-listener-ready-for-data, exit busywait */
            wait = iec_wait_data(IEC_HI);
            if (DEV_COMPUTER != device_type)
            {
                wait = wait && iec_wait_atn(IEC_LO, false);
            }
        }
        break;

    case IEC_REMOTE_LISTENER_READY_FOR_DATA:
        udelay(80); /* Tne (Non-EOI response to RFD) */
        /* Check if listener wants to abort */
        if (DEV_COMPUTER != device_type &&
            IEC_LO == iec_get_atn())
        {
            next_state = IEC_EOI_ATN_ASSERTED;
            break;
        }
        iec_set_clk(IEC_LO);
        iec_bit = 8;
        while (iec_bit > 0)
        {
            if (IEC_LO == iec_get_data())
            {
                iec_idle_state();
                iec_status = IEC_WRITE_TIMEOUT;
                next_state = IEC_ERROR;
                break;
            }
            udelay(bit_timings[device_type].data_hi);
            iec_set_data( iec_byte & 1 ); /* LSB first */
            iec_byte >>= 1;
            udelay(bit_timings[device_type].data_settle);
            iec_set_clk(IEC_HI);
            udelay(bit_timings[device_type].data_valid);
            iec_set_clk(IEC_LO);
            iec_set_data(IEC_HI);
            --iec_bit;
        }
        iec_set_timeout(1000, -1); /* Listener data-accepted timeout */
        if (iec_wait_data_busy(IEC_LO, 100))
        {
            /* Very slow listener-data-accepted, exit busywait */
            next_state = IEC_REMOTE_LISTENER_DATA_ACCEPTED;
            wait = iec_wait_data(IEC_LO);
            break;
        }
        event = iec_no_event; /* Fall through, ensure no accidental timeout */
    case IEC_REMOTE_LISTENER_DATA_ACCEPTED:
        if (event == iec_timeout)
        {
            iec_idle_state();
            iec_status = IEC_WRITE_TIMEOUT;
            next_state = IEC_ERROR;
            break;
        }
        iec_cancel_timeout();
        next_state = IEC_SEND_NEXT_BYTE;
        /* A small breather after all the busywaits */
        tasklet_schedule(&raspbiec_tasklet);
        wait = true;
        break;

    case IEC_EOI_HANDSHAKE:
        if (iec_wait_data_busy(IEC_LO, 300))
        {
            /* EOI response time min 200us typ 250us */
            next_state = IEC_EOI_HANDSHAKE_END;
            wait = iec_wait_data(IEC_LO);
            break;
        }
    case IEC_EOI_HANDSHAKE_END:
        EOI_state = iec_EOI_sent;
        next_state = IEC_REMOTE_LISTENER_READY_FOR_DATA;
        if (iec_wait_data_busy(IEC_HI, 100))
        {
            /* Slow remote-listener-ready-for-data, exit busywait */
            wait = iec_wait_data(IEC_HI);
        }
        break;

    case IEC_EOI_ATN_ASSERTED:
        /* EOI also happens when ATN goes low while talking in drive mode */
        /* Break the wait in raspbiec_device_write() */
        talk_interrupted = true;
        wake_up_interruptible(&writeq);
        next_state = IEC_CHECK_ATN;
        break;

        /*-------------------------------------------------------------------*/

    case IEC_SEND_COMMAND:
        wait = iec_command(iec_byte, &next_state);
        if (iec_byte >= IEC_CMD_RANGE_END  &&
                iec_byte <= IEC_CMD_RANGE)
        {
            iec_byte = -iec_byte; /* Make it a normal byte for sending */
        }
        break;

    case IEC_RESET:
        iec_cancel_waits();
        iec_cancel_timeout();
        iec_idle_state();
        iec_status = IEC_OK;
        if (notify_error == iec_error_clearing_pending)
        {
            notify_error = iec_no_error;
        }
        dev_state  = DEV_IDLE;
        next_state = IEC_IDLE;
        under_atn = false;
        msg(1,"raspbiec <- IEC_RESET\n");
        break;

    case IEC_ERROR:
        /* Return error code to user */
        if (notify_error == iec_no_error)
        {
            notify_error = iec_return_eio;
            msg(1,"raspbiec <- IEC_ERROR %c0x%02X\n",ABSHEX(iec_status));
            kfifo_in(&raspbiec_read_fifo, &iec_status, 1);
            wake_up_interruptible(&readq);
        }
        next_state = IEC_PROCESS_USER_DATA;
        break;

    default:
        iec_status = IEC_ILLEGAL_STATE;
        next_state = IEC_ERROR;
        break;
    }
    /*-------------------------------------------------------------------*/

    *state = next_state;
    return wait;
}

/* iec_command()
 * return: next state
 *
 * For easier state machine the user side must break the
 * commands to smaller units
 * Example: directory loading
 * On bus:
 * /28 /F0 $ /3F
 * /48 /60 <=> data <=> /5F
 * /28 /E0 /3F
 *
 * Data written to (->) / read from (<-) raspiec device
 * -> IEC_ASSERT_ATN
 * -> CMD_LISTEN(8)
 * -> CMD_OPEN(0)
 * -> IEC_DEASSERT_ATN
 *
 * -> IEC_LAST_BYTE_NEXT
 * -> '$'
 *
 * -> IEC_ASSERT_ATN
 * -> UNLISTEN
 * -> IEC_BUS_IDLE
 *
 * -> IEC_ASSERT_ATN
 * -> CMD_TALK(8)
 * -> CMD_DATA(0)
 * -> IEC_TURNAROUND
 * <-  <data bytes from device>
 * <- IEC_EOI
 * -> IEC_ASSERT_ATN
 * -> UNTALK
 * -> IEC_BUS_IDLE
 *
 * -> IEC_ASSERT_ATN
 * -> CMD_LISTEN(8)
 * -> CMD_CLOSE(0)
 * -> IEC_DEASSERT_ATN
 *
 * -> IEC_ASSERT_ATN
 * -> UNLISTEN
 * -> IEC_BUS_IDLE
 */
static bool iec_command(int16_t cmd, int *next_state)
{
    bool wait = false;

    *next_state = IEC_SEND_NEXT_BYTE;

    if (cmd == IEC_ASSERT_ATN)
    {
        msg(3,"raspbiec: IEC_ASSERT_ATN\n");
        if (notify_error == iec_error_clearing_pending)
        {
            notify_error = iec_no_error;
        }
        iec_status = IEC_OK; /* Clear errors upon asserting ATN */
        iec_set_data(IEC_HI);
        iec_set_clk(IEC_HI);
        iec_set_atn(IEC_LO);
    }
    else if (cmd == IEC_DEASSERT_ATN)
    {
        msg(3,"raspbiec: IEC_DEASSERT_ATN\n");
        udelay(20); /* Tr (Frame to release of ATN) */
        iec_set_atn(IEC_HI);
        udelay(150); /* Ttk (Talk-attention release) */
        /* Spec says min 20/typ 30/max 100, but C64 is slower */
    }
    else if (cmd == IEC_BUS_IDLE)
    {
        msg(3,"raspbiec: IEC_BUS_IDLE\n");
        udelay(20); /* Tr (Frame to release of ATN) */
        iec_idle_state();
    }
    else if (cmd == IEC_LAST_BYTE_NEXT)
    {
        msg(3,"raspbiec: IEC_LAST_BYTE_NEXT\n");
        EOI_state = iec_send_EOI;
    }
    else if (cmd == IEC_TURNAROUND)
    {
        msg(3,"raspbiec: IEC_TURNAROUND\n");
        /* Talk-attention turnaround */
        iec_set_data(IEC_LO);
        iec_set_atn(IEC_HI);
        iec_set_clk(IEC_HI);
        wait = iec_wait_clk(IEC_LO);
        /* Wait for talk-attention acknowledge */
        *next_state = IEC_RECEIVE_BYTE;
    }
    else if (cmd >= IEC_CMD_RANGE_END &&
             cmd <= IEC_CMD_RANGE)
    {
        iec_set_clk(IEC_LO);
        iec_set_data(IEC_HI);
        wait = iec_wait(1000);
        *next_state = IEC_SEND_BYTE;
    }
    else if (cmd == IEC_IDENTITY_COMPUTER ||
             IEC_IDENTITY_IS_DRIVE(cmd))
    {
        if (cmd == IEC_IDENTITY_COMPUTER)
        {
            msg(3,"raspbiec: IEC_IDENTITY_COMPUTER\n");
            device_type = DEV_COMPUTER;
            dev_num = -1;
        }
        else
        {
            device_type = DEV_DRIVE;
            dev_num = IEC_DRIVE_DEVICE(cmd);
            msg(3,"raspbiec: IEC_IDENTITY_DRIVE(%d)\n",dev_num);
        }
        *next_state = IEC_RESET;
    }
    else if (cmd == IEC_CLEAR_ERROR)
    {
        msg(3,"raspbiec: IEC_CLEAR_ERROR\n");
        *next_state = IEC_RESET;
    }
    else
    {
        /* Unknown command, ignore */
    }
    return wait;
}

static void iec_idle_state(void)
{
    iec_set_atn(IEC_HI);
    iec_release_bus();
}

static void iec_release_bus(void)
{
    iec_set_clk(IEC_HI);
    iec_set_data(IEC_HI);
}

static bool iec_bus_is_idle(void)
{
    return iec_get_atn()  == IEC_HI &&
           iec_get_clk()  == IEC_HI &&
           iec_get_data() == IEC_HI;
}

static bool iec_wait_atn(int value, bool checkmissed)
{
    int curr = iec_get_atn();
    if (curr != value)
    {
        irqi[iec_atn].waiting = value;
        irqi[iec_atn].checkmissed = checkmissed;
        event_wait_mask |= iec_atn_mask;
        return true;
    }
    return false;
}

static bool iec_wait_data(int value)
{
    int curr = iec_get_data();
    if (curr != value)
    {
        irqi[iec_data].waiting = value;
        event_wait_mask |= iec_data_mask;
        return true;
    }
    return false;
}

static bool iec_wait_clk(int value)
{
    int curr = iec_get_clk();
    if (curr != value)
    {
        irqi[iec_clk].waiting = value;
        event_wait_mask |= iec_clk_mask;
        return true;
    }
    return false;
}

static bool iec_wait(int timeout /* microseconds */)
{
    iec_set_timeout(timeout, -1);
    return true;
}

static void iec_wait_atn_cancel(void)
{
    irqi[iec_atn].waiting  = -1;
    event_wait_mask &= ~iec_atn_mask;
}

static void iec_cancel_waits(void)
{
    int i;
    for (i=0; i < ARRAY_SIZE(irqi); ++i)
    {
        irqi[i].waiting = -1;
    }
    event_wait_mask =  0;
}


/* return true if timeout expired */
static bool iec_wait_data_busy(int value, int timeout)
{
    uint32_t starttime = stc_read_cycles();
    while (stc_read_cycles() - starttime < timeout)
    {
        if (iec_get_data() == value)
        {
            return false;
        }
        udelay(1);

    }
    return true;
}

static bool iec_wait_data_atn_busy(int value, int timeout)
{
    uint32_t starttime = stc_read_cycles();
    while (stc_read_cycles() - starttime < timeout)
    {
        if (iec_get_data() == value ||
            iec_get_atn() == IEC_LO)
        {
            return false;
        }
        udelay(1);

    }
    return true;
}

/* return true if timeout expired */
static bool iec_wait_clk_busy(int value, int timeout)
{
    uint32_t starttime = stc_read_cycles();
    while (stc_read_cycles() - starttime < timeout)
    {
        if (iec_get_clk() == value)
        {
            return false;
        }
        udelay(1);

    }
    return true;
}

//static bool iec_wait_clk_atn_busy(int value, int timeout)
//{
//    uint32_t starttime = stc_read_cycles();
//    while (stc_read_cycles() - starttime < timeout)
//    {
//        if (iec_get_clk() == value ||
//            iec_get_atn() == IEC_LO)
//        {
//            return false;
//        }
//        udelay(1);
//
//    }
//    return true;
//}

#ifdef INVERTED_OUTPUT
/* Output is inverting open-collector */
static void iec_set_atn(int value)
{
    gpio_set_value(IEC_ATN_OUT,value==0);
    udelay(3); /* Wait for the corresponding input line to stabilize */
}
static void iec_set_clk(int value)
{
    gpio_set_value(IEC_CLK_OUT,value==0);
    udelay(3);
}
static void iec_set_data(int value)
{
    gpio_set_value(IEC_DATA_OUT,value==0);
    udelay(3);
}
#else
/* Output is noninverting open-collector */
static void iec_set_atn(int value)
{
    gpio_set_value(IEC_ATN_OUT,value);
    udelay(3); /* Wait for the corresponding input line to stabilize */
}
static void iec_set_clk(int value)
{
    gpio_set_value(IEC_CLK_OUT,value);
    udelay(3);
}
static void iec_set_data(int value)
{
    gpio_set_value(IEC_DATA_OUT,value);
    udelay(3);
}
#endif

static uint32_t stc_read_cycles(void)
{
    /* STC: a free running counter that increments at the rate of 1MHz */
    return readl(__io_address(ST_BASE + 0x04));
}

static void set_debugpin(int pin, int value)
{
    /* Toggle the IEC_DEBUG3 pin to catch recursive calls */
    if (value == 0)
    {
        if (debugpins[pin].count == 0) /* extra 0 setting */
        {
            /* gpio_set_value(IEC_DEBUG3, 1); */
            /* gpio_set_value(IEC_DEBUG3, 0); */
            gpio_set_value(debugpins[pin].GPIO, 0);
        }
        else if (--debugpins[pin].count == 0)
        {
            gpio_set_value(debugpins[pin].GPIO, 0);
        }
        else /* 1 level of recursion less */
        {
            /* gpio_set_value(IEC_DEBUG3, 1); */
            /* gpio_set_value(IEC_DEBUG3, 0); */
            gpio_set_value(debugpins[pin].GPIO, 1);
        }
    }
    else
    {
        if (debugpins[pin].count > 0) /* 1 level of recursion more */
        {
            /* gpio_set_value(IEC_DEBUG3, 1); */
            /* gpio_set_value(IEC_DEBUG3, 0); */
        }
        gpio_set_value(debugpins[pin].GPIO, 1);
        ++debugpins[pin].count;
    }
}

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
 */

#ifndef RASPBIECDRV_H
#define RASPBIECDRV_H

#include "raspbiec_common.h"

#define DEVICE_NAME "device"
#define CLASS_NAME "raspbiec"
#define RASPBIEC_READ_FIFO_SIZE 1024
#define RASPBIEC_WRITE_FIFO_SIZE 1024

/* Voltages on IEC bus */
#define IEC_LO    0
#define IEC_HI    1

/* Do not wait for this GPIO */
#define IEC_NONE -1

/* GPIO assignments */
#define IEC_ATN_IN   17
#define IEC_CLK_IN   18
#define IEC_DATA_IN  27
#define IEC_ATN_OUT  22
#define IEC_CLK_OUT  23
#define IEC_DATA_OUT 24

#define IEC_DEBUG0    4
#define IEC_DEBUG1   25
#define IEC_DEBUG2    8
#define IEC_DEBUG3    7

/* IEC command ranges */
#define IEC_CMD_RANGE             -0x20
#define IEC_CMD_RANGE_END         -0xFF

#define IEC_CMD_LISTEN            -0x20
#define IEC_CMD_LISTEN_END        -0x3E
#define IEC_CMD_UNLISTEN          -0x3F
#define IEC_CMD_TALK              -0x40
#define IEC_CMD_TALK_END          -0x5E
#define IEC_CMD_UNTALK            -0x5F
#define IEC_CMD_DATA              -0x60
#define IEC_CMD_DATA_END          -0x6F
#define IEC_CMD_CLOSE             -0xE0
#define IEC_CMD_CLOSE_END         -0xEF
#define IEC_CMD_OPEN              -0xE0
#define IEC_CMD_OPEN_END          -0xEF


/* Declarations */
static irqreturn_t gpio_interrupt(int irq, void* dev_id);
static void raspbiec_state_machine(int event, int value);
static bool raspbiec_state_selector(int *state, int event, int value);
static bool iec_command(int16_t cmd, int *next_state);
static void iec_set_timeout(int usecs, int value);
static void iec_cancel_timeout(void);

static void iec_idle_state(void);
static void iec_release_bus(void);
static bool iec_bus_is_idle(void);

static void iec_set_atn(int value);
static void iec_set_clk(int value);
static void iec_set_data(int value);
                                                       /* return true if */
static bool iec_wait_atn(int value, bool checkmissed); /* waiting is required */
static bool iec_wait_data(int value); /* return true if waiting is required */
static bool iec_wait_clk(int value);  /* return true if waiting is required */
static bool iec_wait(int timeout /* microseconds */); /* unconditional wait */
static void iec_wait_atn_cancel(void);
static void iec_cancel_waits(void);

/* return true if timeout expired */
static bool iec_wait_data_busy(int value, int timeout /* microseconds */);
static bool iec_wait_data_atn_busy(int value, int timeout /* microseconds */);
static bool iec_wait_clk_busy(int value, int timeout /* microseconds */);
//static bool iec_wait_clk_atn_busy(int value, int timeout /* microseconds */);

static uint32_t stc_read_cycles(void);

/* Logging macros */
#define msg(level, format, arg...) do { if (debug>=(level)) pr_info(format, ## arg); } while (0)
#define dbg(level, format, arg...) do { if (debug>=(level)) pr_info(CLASS_NAME ": %s: " format , __FUNCTION__ , ## arg); } while (0)
#define err(format, arg...) pr_err(CLASS_NAME ": " format, ## arg)
#define info(format, arg...) pr_info(CLASS_NAME ": " format, ## arg)
#define warn(format, arg...) pr_warn(CLASS_NAME ": " format, ## arg)

#endif // RASPBIECDRV_H

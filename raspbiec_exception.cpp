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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <vector>
#include "raspbiec_exception.h"
#include "raspbiec_common.h"

raspbiec_error::raspbiec_error(const int iec_status)
{
    m_status = iec_status;
}

raspbiec_error::~raspbiec_error() throw()
{
}

int raspbiec_error::status() const
{
    return m_status;
}

const char *raspbiec_error::what() const throw()
{
    switch(m_status)
    {
    case IEC_OK:
	return "OK";
    case IEC_ILLEGAL_DEVICE_NUMBER:
	return "illegal device number";
    case IEC_MISSING_FILENAME:
	return "missing filename";
    case IEC_FILE_NOT_FOUND:
	return "file not found";
    case IEC_WRITE_TIMEOUT:
	return "write timeout";
    case IEC_READ_TIMEOUT:
	return "read timeout";
    case IEC_DEVICE_NOT_PRESENT:
	return "device not present";
    case IEC_ILLEGAL_STATE:
	return "illegal state";
    case IEC_GENERAL_ERROR:
	return "general error";
    case IEC_PREV_BYTE_HAS_ERROR:
	return "previous byte has error";
    case IEC_FILE_EXISTS:
	return "file exists";
    case IEC_DRIVER_NOT_PRESENT:
	return "driver not present";
    case IEC_OUT_OF_MEMORY:
	return "out of memory";
    case IEC_UNKNOWN_MODE:
	return "unknown mode";
    case IEC_SIGNAL:
	return "caught a signal";
    case IEC_BUS_NOT_IDLE:
	return "IEC bus is not in idle state";
    case IEC_SAVE_ERROR:
	return "save error";
    case IEC_UNKNOWN_DISK_IMAGE:
	return "unknown disk image";
    case IEC_ILLEGAL_TRACK_SECTOR:
	return "illegal track or sector";
    case IEC_DISK_IMAGE_ERROR:
	return "disk image error";
    default:
	snprintf(msg, sizeof msg, "raspbiec error %d (%c0x%X)\n",
		 m_status, m_status<0?'-':' ', m_status<0?-m_status:m_status);
	msg[(sizeof msg)-1] = '\0';
	return msg;
    }
}

struct sigaction raspbiec_sighandler::sa;
bool raspbiec_sighandler::sigactive;

void raspbiec_sighandler::setup(void)
{
    sigactive = false;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sighandler;
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
	throw raspbiec_error(IEC_GENERAL_ERROR);
    }
}

void raspbiec_sighandler::react(bool want_to_catch)
{
    sigactive = want_to_catch;
}

void raspbiec_sighandler::sighandler(int sig)
{
    if (sigactive)
    {
	if (signal(sig, SIG_DFL) != SIG_ERR)
	{
	    raise(sig);
	}
	else
	{
	    abort();
	}
    }
}

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

#include "raspbiec_device.h"
#include "raspbiec_utils.h"
#include "raspbiec_exception.h"
#include "raspbiec_common.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#if 0
#define DMSG(format, arg...) do { if (foreground) fprintf(stderr, "[%d]" format "\n", identity, ## arg); } while (0)
#else
#define DMSG(format, arg...)
#endif
/* For printing hex numbers with a sign in front of absolute value */
/* Use format "%c0x%02X" */
#define ABSHEX(val) ((val)<0)?'-':' ',((val)<0)?-(val):(val)

#define IEC_WAIT_MS 20
#define IEC_TIMEOUT_MS 10000

static const struct timespec timeout = { 0, IEC_WAIT_MS * 1000000 };

device::device(const bool foreground) :
    		identity(computer),
			buffered(false),
			buffered_byte(0),
			data_counter(0),
			lasterror(IEC_OK),
			verbose(false),
			foreground(foreground)
{
}

device::~device()
{
	clear_error();
}

void device::set_identity(const int new_identity, pipefd &bus)
{
	identity = new_identity;

	m_bus.move(bus); // get ownership

	if (!m_bus.is_open_directional()) throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);


	bool is_dev (m_bus.read_end() == m_bus.write_end());

	switch( new_identity )
	{
	case computer:
		if (is_dev) send_byte( IEC_IDENTITY_COMPUTER );
		break;
	case drive_8:
	case drive_9:
	case drive_10:
	case drive_11:
		if (is_dev) send_byte( IEC_IDENTITY_DRIVE(new_identity) );
		break;
	default:
		throw raspbiec_error(IEC_ILLEGAL_DEVICE_NUMBER);
	}
}

device::Command device::receive_command(int device_number, int &chan, int command_byte)
{
	int16_t iec_byte;
	int dev_state = DEV_IDLE;
	int command_device_number = -1;
	bool under_atn = false;
	int secondary = -1;

	Command tmpcmd = Unknown;
	int tmpchan = -1;

	try
	{
		/* Command loop: receive a command under ATN */
		for(;;)
		{
			if (command_byte < 0)
			{
				iec_byte = command_byte;
				command_byte = 0;
			}
			else
			{
				iec_byte = receive_byte(timeout_infinite);
			}

			if (IEC_ASSERT_ATN   == iec_byte)
			{
				under_atn = true;
				continue;
			}
			if (IEC_DEASSERT_ATN == iec_byte ||
                IEC_BUS_IDLE     == iec_byte ||
                IEC_TURNAROUND   == iec_byte)
			{
				under_atn = false;
				break;
			}
			if (under_atn)
			{
				iec_byte = -iec_byte;
				if (CMD_UNLISTEN == iec_byte)
				{
					dev_state = DEV_IDLE;
					tmpcmd = Unlisten;
				}
				else if (CMD_UNTALK == iec_byte)
				{
					dev_state = DEV_IDLE;
					tmpcmd = Untalk;
				}
				else if (CMD_IS_TALK(iec_byte))
				{
					dev_state = DEV_TALK;
					command_device_number = CMD_GETDEV(iec_byte);
				}
				else if (CMD_IS_LISTEN(iec_byte))
				{
					dev_state = DEV_LISTEN;
					command_device_number = CMD_GETDEV(iec_byte);
				}
				else if (CMD_IS_DATA_CLOSE_OPEN(iec_byte))
				{
					secondary = iec_byte;
				}
				else
				{
					dev_state = DEV_IDLE;
				}
			}
		} /* Command loop */

		/* Decode the command received under ATN */
		bool thisDev = (command_device_number == device_number);

		if (DEV_LISTEN == dev_state)
		{
			if (CMD_IS_OPEN(secondary))
			{
				tmpcmd = thisDev ? Open : OpenOtherDevice;
				tmpchan = CMD_GETSEC(secondary);
			}
			else if (CMD_IS_CLOSE(secondary))
			{
				tmpcmd = thisDev ? Close : CloseOtherDevice;
				tmpchan = CMD_GETSEC(secondary);
			}
			else if (CMD_IS_DATA(secondary))
			{
				tmpcmd = thisDev ? Receive : ReceiveOtherDevice;
				tmpchan = CMD_GETSEC(secondary);
			}
		}
		else if (DEV_TALK == dev_state)
		{
			if (CMD_IS_DATA(secondary))
			{
				tmpcmd = thisDev ? Send : SendOtherDevice;
				tmpchan = CMD_GETSEC(secondary);
			}
		}
	}
	catch( raspbiec_error &e )
	{
		if (e.status() != IEC_SIGNAL)
		{
			throw;
		}
		tmpcmd = Exit;
	}
	chan = tmpchan;
	return tmpcmd;
}


// load from a device
template <class OutputIterator>
OutputIterator device::load(
		OutputIterator load_buf,
		const char *const name,
		int device_number,
		int secondary_address )
{
	DMSG("> iec_load");
	if (device_number == 0 ||
			device_number == 2 ||
			device_number == 3)
	{
		DMSG("< iec_load");
		throw raspbiec_error(IEC_ILLEGAL_DEVICE_NUMBER);
	}

	if (device_number == 1)
	{
		/* Do cassette load */
		DMSG("< iec_load");
		throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);
	}

	/* IEC bus load */
	if (!name || strlen(name) == 0)
	{
		DMSG("< iec_load");
		throw raspbiec_error(IEC_MISSING_FILENAME);
	}

	printf("searching for %s\n",name);
	open_file( name, device_number, 0 );
	printf( "loading\n");
	OutputIterator loaded = load_buf;
	verbose = true;
	try
	{
		loaded = receive_data(load_buf, device_number, timeout_infinite);
	}
	catch (raspbiec_error &e)
	{
		verbose = false;
		close_file( device_number, 0 );
		if (IEC_READ_TIMEOUT == e.status())
		{
			throw raspbiec_error(IEC_FILE_NOT_FOUND);
		}
		throw;
	}
	verbose = false;
	close_file( device_number, 0 );

	DMSG("< iec_load");
	return loaded;
}

// Explicit instantiation
template databuf_back_insert device::load<databuf_back_insert>(
		databuf_back_insert load_buf,
		const char *const name,
		int device_number,
		int secondary_address );

// save ram to a device
databuf_iter device::save(
		databuf_iter first,
		databuf_iter last,
		const char *const name,
		int device_number,
		int secondary_address )
{
	DMSG("> iec_save");
	if (device_number == 0 ||
			device_number == 2 ||
			device_number == 3)
	{
		DMSG("< iec_save");
		throw raspbiec_error(IEC_ILLEGAL_DEVICE_NUMBER);
	}

	if (device_number == 1)
	{
		/* Do cassette save */
		DMSG("< iec_save");
		throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);
	}

	/* IEC bus save */
	if (!name || strlen(name) == 0)
	{
		DMSG("< iec_save");
		throw raspbiec_error(IEC_MISSING_FILENAME);
	}

	printf("saving %s\n",name);
	open_file( name, device_number, 1 );
	databuf_iter saved = first;
	verbose = true;
	try
	{
		saved = send_data(first, last, device_number, 1);
	}
	catch (raspbiec_error &e)
	{
		verbose = false;
		close_file( device_number, 1 );
		if (IEC_DEVICE_NOT_PRESENT == e.status())
		{
			throw raspbiec_error(IEC_SAVE_ERROR);
		}
		throw;
	}
	verbose = false;
	close_file( device_number, 1 );

	DMSG("< iec_save");
	return saved;
}

void device::open_file(
		const char *name,
		int device,
		int secondary_address )
{
	DMSG("> iec_open_file");
	listen( device );
	open_cmd( secondary_address );

	send_byte_buffered_init();
	for ( ; *name != '\0'; ++name )
	{
		send_byte_buffered( ascii2petscii(*name) );
	}
	send_last_byte();

	unlisten();
	DMSG("< iec_open_file");
}

void device::close_file(int device, int secondary_address)
{
	DMSG("> iec_close_file");
	listen( device );
	close_cmd( secondary_address );
	unlisten();
	DMSG("< iec_close_file");
}

databuf_iter device::send_data(
		databuf_iter first,
		databuf_iter last,
		int device_number,
		int channel )
{
	listen( device_number );
	data_listen( channel );

	databuf_iter sent = first;
	try
	{
		sent = send_to_bus(first, last);
	}
	catch (raspbiec_error &e)
	{
		unlisten();
		throw;
	}
	unlisten();


	return sent;
}

template <class OutputIterator>
OutputIterator device::receive_data(
		OutputIterator data_buf,
		int device_number,
		int channel )
{
	talk( device_number );
	data_talk( channel );

	OutputIterator received = data_buf;
	try
	{
		received = receive_from_bus(data_buf);
	}
	catch (raspbiec_error &e)
	{
		untalk();
		throw;
	}
	untalk();

	return received;
}

databuf_iter device::send_to_bus(databuf_iter first, databuf_iter last)
{
	int blocks = -1;
	size_t sent = 0;
	databuf_iter it = first;
	try
	{
		send_byte_buffered_init();
		bool aborted = false;
		for(; it != last; ++it)
		{
			int16_t byte = *it;
			int ret = send_byte_buffered(byte);
			//fprintf(stderr, "send_byte_buffered %d\n", ret);
			if (ret == 0 && it != first && identity != computer )
			{
				aborted = true;
				break; // Listened ended data transport
			}
			sent += ret;
			if (verbose &&
					(int)(sent/254) > blocks)
			{
				blocks = sent/254;
				printf("\r%d blocks", blocks);
				fflush(stdout);
			}
		}
		if (!aborted)
		{
			sent += send_last_byte();
		}
	}
	catch (raspbiec_error &e)
	{
		if (verbose && blocks != -1) printf("\r%ld blocks\n",(sent+253)/254);
		throw;
	}
	if (verbose && blocks != -1) printf("\r%ld blocks\n", (sent+253)/254);
	return it;
}

template <class OutputIterator>
OutputIterator device::receive_from_bus(OutputIterator data_buf, long timeout_ms)
{
	int blocks = -1;
	size_t received = 0;
	bool last_byte = false;
	try
	{
	    for (;;)
        {
            int16_t rbyte = receive_byte(timeout_ms);
            // Byte stream from a real device is different to a stream from a virtual one
            if (m_bus.is_device())
            {
                if (IEC_EOI == rbyte) break;
            }
            else
            {
                if (IEC_LAST_BYTE_NEXT == rbyte)
                {
                    last_byte = true;
                    continue;
                }
            }
			if (IEC_PREV_BYTE_HAS_ERROR == rbyte)
			{
				printf("error at byte #0x%04lX\n", received); //..but continue
			}
			else if ( rbyte < 0 ) // Some other error
			{
				throw raspbiec_error(rbyte);
			}
			else
			{
				*data_buf++ = rbyte;
				++received;
				if (verbose &&
						(int)(received/254) > blocks)
				{
					blocks = received/254;
					printf("\r%d blocks", blocks);
					fflush(stdout);
				}
                if (last_byte) break;
			}
		}
	}
	catch (raspbiec_error &e)
	{
		if (verbose && blocks != -1) printf("\r%ld blocks\n",(received+253)/254);
		throw;
	}
	if (verbose && blocks != -1) printf("\r%ld blocks\n",(received+253)/254);

	return data_buf;
}

//Explicit instantiation
template databuf_back_insert device::receive_from_bus<databuf_back_insert>(databuf_back_insert data_buf, long timeout_ms);


databuf_iter device::send_to_bus_verbose(databuf_iter first, databuf_iter last)
{
	verbose = true;
	databuf_iter r = send_to_bus(first, last);
	verbose = false;
	return r;
}

template <class OutputIterator>
OutputIterator device::receive_from_bus_verbose(OutputIterator data_buf, long timeout_ms)
{
	verbose = true;
	OutputIterator r = receive_from_bus(data_buf, timeout_ms);
	verbose = false;
	return r;
}

//Explicit instantiation
template databuf_back_insert device::receive_from_bus_verbose<databuf_back_insert>(databuf_back_insert data_buf, long timeout_ms);

void device::talk( int device )
{
	DMSG("> iec_talk");
	command( CMD_TALK(device) );
	DMSG("< iec_talk");
}

void device::listen( int device )
{
	DMSG("> iec_listen");
	command( CMD_LISTEN(device) );
	DMSG("< iec_listen");
}

void device::untalk()
{
	DMSG("> iec_untalk");
	send_last_byte();
	command(CMD_UNTALK);
	send_byte(IEC_BUS_IDLE);
	DMSG("< iec_untalk");
}

void device::unlisten()
{
	DMSG("> iec_unlisten");
	send_last_byte();
	command(CMD_UNLISTEN);
	send_byte(IEC_BUS_IDLE);
	DMSG("< iec_unlisten");
}

void device::open_cmd(int secondary_address)
{
	DMSG("> iec_open");
	secondary_command( CMD_OPEN(secondary_address), false );
	DMSG("< iec_open");
}

void device::close_cmd(int secondary_address)
{
	DMSG("> iec_close");
	secondary_command( CMD_CLOSE(secondary_address), false );
	DMSG("< iec_close");
}

void device::data_listen(int secondary_address)
{
	DMSG("> iec_data_listen");
	secondary_command( CMD_DATA(secondary_address), false );
	DMSG("< iec_data_listen");
}

void device::data_talk(int secondary_address)
{
	DMSG("> iec_data_talk");
	secondary_command( CMD_DATA(secondary_address), true );
	DMSG("< iec_data_talk");
}

void device::command( int command )
{
	//DMSG("> iec_command");
	send_last_byte();
	send_byte(IEC_ASSERT_ATN);
	send_byte( -command );
	//DMSG("< iec_command");
}

void device::secondary_command(int secondary_command, bool talk)
{
	//DMSG("> iec_secondary_command");
	send_byte( -secondary_command );
	if (talk)
	{
		send_byte(IEC_TURNAROUND);
	}
	else
	{
		send_byte(IEC_DEASSERT_ATN);
	}
	//DMSG("< iec_secondary_command");
}

void device::send_byte_buffered_init(void)
{
	buffered = false;
	data_counter = 0;
}

int device::send_byte_buffered( int16_t byte )
{
	int sent = 0;
	if (buffered)
	{
		sent = send_byte(buffered_byte);
	}
	buffered_byte = byte;
	buffered = true;
	return sent;
}

int device::send_last_byte()
{
	//DMSG("> iec_send_last_byte");
	int sent = 0;
	if (buffered)
	{
		send_byte(IEC_LAST_BYTE_NEXT);
		sent = send_byte(buffered_byte);
		buffered = false;
	}
	//DMSG("< iec_send_last_byte");
	return sent;
}

int device::send_byte( int16_t byte )
{
	/* In a tight send loop a second exception may be triggered
	 * before the first one has been reached a handler ->
	 * "terminate called after throwing an instance of 'raspbiec_error'"
	 */
	//if (lasterror != IEC_OK) return;

	for(long msec = 0; msec < IEC_TIMEOUT_MS; msec+=IEC_WAIT_MS)
	{
        DMSG("-> %c0x%02X",ABSHEX(byte));
		int ret = write(m_bus.write_end(), &byte, sizeof byte);
		if ( ret == 0 && identity != computer )
		{
			// Listener ended data transport
			return 0;
		}
		else if ( ret > 0 ) // Normal write
		{
			return 1;
		}
		else if (ret < 0)
		{
			if (errno == EIO)
			{
				receive_byte(); // Read the IEC bus error code
			}
			if (lasterror < 0)
			{
				throw raspbiec_error(lasterror);
			}
			else
			{
				lasterror = IEC_GENERAL_ERROR;
				throw raspbiec_error(IEC_GENERAL_ERROR);
			}
		}
		nanosleep(&timeout, NULL);
	}
	throw raspbiec_error(IEC_READ_TIMEOUT);
}

int16_t device::receive_byte(long timeout_ms)
{
	// Note:  timeout will work only if fd_read is in non-blocking mode
	if (timeout_ms == timeout_default) timeout_ms = IEC_TIMEOUT_MS;

	long msec = 0;
	for(;;)
	{
		int16_t readbyte;
		int ret = read(m_bus.read_end(), &readbyte, sizeof readbyte);
		if (ret > 0)
		{
			if (readbyte < 0) lasterror = readbyte;
            DMSG("<- %c0x%02X",ABSHEX(readbyte));
			return readbyte;
		}
		else if (ret < 0)
		{
			if (errno == EIO)
			{
				DMSG("EIO");
				receive_byte(); // Read the IEC bus error code
			}
			else if (errno == EINTR)
			{
				DMSG("EINTR");
				throw raspbiec_error(IEC_SIGNAL);
			}

			if (lasterror < 0)
			{
				throw raspbiec_error(lasterror);
			}
			else
			{
				lasterror = IEC_GENERAL_ERROR;
				throw raspbiec_error(IEC_GENERAL_ERROR);
			}
		}
		else // ret == 0, EOF
		{
			throw raspbiec_error(IEC_SIGNAL);
		}
		if (timeout_ms != timeout_infinite)
		{
			if (msec >= timeout_ms) break;
			msec+=IEC_WAIT_MS;
		}
		nanosleep(&timeout, NULL);
	}
	throw raspbiec_error(IEC_WRITE_TIMEOUT);
}

void device::clear_error(void)
{
	send_byte(IEC_CLEAR_ERROR);
	lasterror = IEC_OK;
}

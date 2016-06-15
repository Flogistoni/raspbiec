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
#include "raspbiec_common.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#if 0
#define DMSG(x) fprintf(stderr,"%s\n",x);
#else
#define DMSG(x)
#endif

#define IEC_WAIT_MS 20
#define IEC_TIMEOUT_MS 10000

static const char* raspbiecdevname = "/dev/raspbiec";
static const struct timespec timeout = { 0, IEC_WAIT_MS * 1E6 };

device::device() :
    devfd(-1),
    buffered(false),
    lasterror(IEC_OK),
    verbose(false)
{
    devfd = open(raspbiecdevname, O_RDWR);
    if (devfd < 0)
    {
	int deverr = errno;
	fprintf(stderr,"Cannot open %s\n",raspbiecdevname);
	if (deverr == EREMOTEIO)
	    throw raspbiec_error(IEC_BUS_NOT_IDLE);
	else
	    throw raspbiec_error(IEC_DRIVER_NOT_PRESENT);
    }
}

device::~device()
{
    clear_error();
    close(devfd);
}

void device::set_identity(const int identity)
{
    switch( identity )
    {
    case computer:
	send_byte( IEC_IDENTITY_COMPUTER );
	break;
    case drive_8:
    case drive_9:
    case drive_10:
    case drive_11:
	send_byte( IEC_IDENTITY_DRIVE(identity) );
	break;
    default:
	throw raspbiec_error(IEC_ILLEGAL_DEVICE_NUMBER);
    }
}

device::Command device::receive_command(int device_number, int &chan)
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
	    iec_byte = receive_byte(timeout_infinite);
	    
	    if (IEC_ASSERT_ATN == iec_byte)
	    {
		under_atn = true;
		continue;
	    }
	    if (IEC_DEASSERT_ATN == iec_byte)
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
		tmpcmd = thisDev ? Save : SaveOtherDevice;
		tmpchan = CMD_GETSEC(secondary);
	    }
	}
	else if (DEV_TALK == dev_state)
	{
	    if (CMD_IS_DATA(secondary))
	    {
		tmpcmd = thisDev ? Load : LoadOtherDevice;
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
size_t device::load( std::vector<unsigned char>& load_buf,
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
	return 0;
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
    size_t loaded = 0;
    verbose = true;
    try
    {
	loaded = receive_data( load_buf, device_number, timeout_infinite );
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

// save ram to a device
size_t device::save( const std::vector<unsigned char>& save_buf, 
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
        return 0;
    }
       
    /* IEC bus save */
    if (!name || strlen(name) == 0)
    {
	DMSG("< iec_save");
        throw raspbiec_error(IEC_MISSING_FILENAME);
    }
 
    printf("saving %s\n",name);
    open_file( name, device_number, 1 );
    size_t saved = 0;
    verbose = true;
    try
    {
	saved = send_data( save_buf, device_number, 1 );
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

void device::open_file( const char *name,
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

size_t device::send_data(const std::vector<unsigned char>& data_buf,
			    int device_number,
			    int channel )
{
    listen( device_number );
    data_listen( channel );

    size_t sent = 0;
    try
    {
	sent = send_to_bus(data_buf);
    }
    catch (raspbiec_error &e)
    {
	unlisten();
	throw;
    }
    unlisten();


    return sent;
}

size_t device::receive_data(std::vector<unsigned char>& data_buf,
			       int device_number,
			       int channel )
{
    talk( device_number );
    data_talk( channel );

    size_t received = 0;
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

size_t device::send_to_bus(const std::vector<unsigned char>& data_buf)
{
    int blocks = -1;
    size_t sent = 0;
    try
    {
	send_byte_buffered_init();
	for(; sent < data_buf.size(); ++sent)
	{
	    int16_t byte = data_buf[sent];
	    send_byte_buffered(byte);
	    if (verbose &&
		(int)(sent/254) > blocks)
	    {
		blocks = sent/254;
		printf("\r%d blocks", blocks);
		fflush(stdout);
	    }
	}
	send_last_byte();
    }
    catch (raspbiec_error &e)
    {
	if (verbose && blocks != -1) printf("\r%d blocks\n", (sent+253)/254);
	throw;
    }
    if (verbose && blocks != -1) printf("\r%d blocks\n", (sent+253)/254);

    return sent;
}

size_t device::receive_from_bus(std::vector<unsigned char>& data_buf,
				long timeout_ms)
{
    int blocks = -1;
    size_t received = 0;
    int16_t byte;
    try
    {
	while(IEC_EOI != (byte = receive_byte(timeout_ms)))
	{
	    if (IEC_PREV_BYTE_HAS_ERROR == byte)
	    {
		printf("error at byte #0x%04X\n", received); //..but continue
	    }
	    else if ( byte < 0 ) // Some other error
	    {
		throw raspbiec_error(byte);
	    }
	    else
	    {
		data_buf.push_back(byte);
		++received;
		if (verbose &&
		    (int)(received/254) > blocks)
		{
		    blocks = received/254;
		    printf("\r%d blocks", blocks);
		    fflush(stdout);
		}
	    }
	}
    }
    catch (raspbiec_error &e)
    {
	if (verbose && blocks != -1) printf("\r%d blocks\n",(received+253)/254);
	throw;
    }
    if (verbose && blocks != -1) printf("\r%d blocks\n",(received+253)/254);

    return received;
}

size_t device::send_to_bus_verbose(const std::vector<unsigned char>& data_buf)
{
    verbose = true;
    size_t r = send_to_bus(data_buf);
    verbose = false;
    return r;
}

size_t device::receive_from_bus_verbose(std::vector<unsigned char>& data_buf,
					long timeout_ms)
{
    verbose = true;
    size_t r = receive_from_bus(data_buf, timeout_ms);
    verbose = false;
    return r;
}

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
}

void device::send_byte_buffered( int16_t byte )
{
    if (buffered)
    {
        send_byte(buffered_byte);
    }
    buffered_byte = byte;
    buffered = true;
}

void device::send_last_byte()
{
    //DMSG("> iec_send_last_byte");
    if (buffered)
    {
    	send_byte(IEC_LAST_BYTE_NEXT);
	send_byte(buffered_byte);
        buffered = false;
    }
    //DMSG("< iec_send_last_byte");
}
 
void device::send_byte( int16_t byte )
{
    /* In a tight send loop a second exception may be triggered
     * before the first one has been reached a handler ->
     * "terminate called after throwing an instance of 'raspbiec_error'"
     */
    //if (lasterror != IEC_OK) return;

    for(long msec = 0; msec < IEC_TIMEOUT_MS; msec+=IEC_WAIT_MS)
    {
	int ret = write(devfd, &byte, sizeof byte);
	if ( ret > 0 )
	{
	    return;
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
    // Note:  timeout will work only if devfd is in non-blocking mode
    if (timeout_ms == timeout_default) timeout_ms = IEC_TIMEOUT_MS;

    long msec = 0;
    for(;;)
    {
	int16_t readbyte;
	int ret = read(devfd, &readbyte, sizeof readbyte);
	if (ret > 0)
	{
	    if (readbyte < 0) lasterror = readbyte;
	    return readbyte;
	}
	else if (ret < 0)
	{
	    if (errno == EIO)
	    {
		receive_byte(); // Read the IEC bus error code
	    }
	    else if (errno == EINTR)
	    {
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

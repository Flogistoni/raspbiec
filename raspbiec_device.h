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

#ifndef RASPBIEC_DEVICE_H
#define RASPBIEC_DEVICE_H

#include <cstddef>
#include <stdint.h>
#include <vector>
#include <iterator>
#include "raspbiec_common.h"
#include "raspbiec_types.h"
#include "raspbiec_utils.h"

class device
{
public:
    device(const bool foreground);
    ~device();

    enum Identity
    {
	computer = -1,
	drive_8  =  8,
	drive_9  =  9,
	drive_10 = 10,
	drive_11 = 11
    };

    void set_identity(const int new_identity, pipefd &bus);

    // When functioning as a drive
    enum Command
    {
	Unknown,
	Open,
	Close,
	Receive,
	Send,
	Unlisten,
	Untalk,
	Exit,
	OpenOtherDevice,
	CloseOtherDevice,
	ReceiveOtherDevice,
	SendOtherDevice
    };

    Command receive_command(int device_number, int &chan, int command_byte);

    // When functioning as a computer
    template <class OutputIterator>
    OutputIterator load(
    		OutputIterator load_buf,
			const char *const name,
			int device_number,
			int secondary_address );

    databuf_iter save(
    		databuf_iter first,
			databuf_iter last,
			const char *const name,
			int device_number,
			int secondary_address );

    void open_file( const char *name, int device, int secondary_address );
    void close_file(int device, int secondary_address);

    databuf_iter send_data (
    		databuf_iter first,
			databuf_iter last,
		     int device_number,
		     int channel );

    template <class OutputIterator>
    OutputIterator receive_data(
   		    OutputIterator data_buf,
			int device_number,
			int channel );

    enum {
	timeout_default = -1,
	timeout_infinite = 0
    };

    // Common routines
    databuf_iter send_to_bus(databuf_iter first, databuf_iter last);
    databuf_iter send_to_bus_verbose (databuf_iter first, databuf_iter last);
    template <class OutputIterator>
    OutputIterator receive_from_bus(OutputIterator data_buf, long timeout_ms = timeout_default);
    template <class OutputIterator>
    OutputIterator receive_from_bus_verbose(OutputIterator data_buf, long timeout_ms = timeout_default);
    void talk( int device );
    void listen( int device );
    void untalk();
    void unlisten();
    void open_cmd(int secondary_address);
    void close_cmd(int secondary_address);
    void data_listen(int secondary_address);
    void data_talk(int secondary_address);
    void command( int command );
    void secondary_command(int secondary_command, bool talk);
    void send_byte_buffered_init(void);
    // Return # of bytes actually sent to bus
    int send_byte_buffered( int16_t byte );
    int send_last_byte();
    int send_byte( int16_t byte );
    int16_t receive_byte( long timeout_ms = timeout_default );
    void clear_error(void);

private:
    int identity;
    pipefd m_bus;
    bool buffered;
    int16_t buffered_byte;
    size_t data_counter;
    int16_t lasterror;
    bool verbose;
    bool foreground;
};

#endif // RASPBIEC_DEVICE_H
